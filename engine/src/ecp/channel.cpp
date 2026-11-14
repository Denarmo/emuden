// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>

#include "den/base/log.h"
#include "den/platform/pal.h"
#include "ecp/channel.h"

namespace den::engine::ecp {

namespace {

using Fields = std::vector<std::pair<std::string, JsonValue>>;

JsonValue Strings(const std::vector<std::string>& values) {
    std::vector<JsonValue> items;
    for (const std::string& value : values) {
        items.push_back(JsonString(value));
    }
    return JsonArray(std::move(items));
}

StateInfo ReadState(const JsonValue& value) {
    StateInfo state;
    state.slot = static_cast<int>(NumberOf(value, "slot", 0));
    state.time = static_cast<std::int64_t>(NumberOf(value, "time", 0));
    state.thumbnail = ReadPicture(value.Find("thumbnail"));
    return state;
}

bool Ok(const JsonValue& result) {
    return BoolOf(result, "ok", false);
}

} // namespace

const std::vector<std::string>& FrontendCapabilities() {
    static const std::vector<std::string> capabilities = {
        "pause", "menu", "states", "state_thumbnails", "reset", "fast_forward",
        "rewind", "settings", "perf", "volume",
    };
    return capabilities;
}

Channel::Channel(Opening opening, LaunchInfo launch, std::string frontend_version)
    : opening_(std::move(opening)), launch_(std::move(launch)), frontend_version_(std::move(frontend_version)) {
    reader_ = std::thread([this] { Run(); });
}

Channel::~Channel() {
    Close();
    {
        std::lock_guard lock(mutex_);
        answered_.notify_all();
    }
    if (reader_.joinable()) {
        reader_.join();
    }
}

void Channel::Close() {
    closing_ = true;
    if (opening_.listener) {
        opening_.listener->Close();
    }
    if (pal::Stream* stream = stream_.load()) {
        stream->Close();
    }
}

void Channel::Fail(std::string message, std::string detail) {
    log::Warn("engine", "ecp: {} ({})", message, detail);
    std::lock_guard lock(mutex_);
    events_.push_back(Message("__failed", 0, {{"message", JsonString(std::move(message))}}));
}

bool Channel::Open() {
    if (opening_.stream) {
        stream_ = opening_.stream;
        return true;
    }
    if (!opening_.listener) {
        return false;
    }
    auto accepted = opening_.listener->Accept(opening_.connect_seconds);
    if (!accepted) {
        if (!closing_) {
            Fail("The engine did not connect.", accepted.error());
        }
        return false;
    }
    owned_ = accepted.take();
    const std::optional<JsonValue> auth = ReadFrame(*owned_);
    if (!auth || auth->String("type") != "auth" || !SameToken(auth->String("token"), opening_.token)) {
        Fail("The engine could not be started.", "the first frame was not the session's auth");
        return false;
    }
    stream_ = owned_.get();
    if (closing_) {
        owned_->Close();
    }
    return true;
}

bool Channel::Handshake() {
    pal::Stream& stream = *stream_.load();
    const std::optional<JsonValue> hello = ReadFrame(stream);
    if (!hello || hello->String("type") != "hello") {
        if (!closing_) {
            Fail("The engine could not be started.", "no hello");
        }
        return false;
    }
    if (NumberOf(*hello, "protocol", 0) != PROTOCOL) {
        Fail("The engine speaks another version of the control protocol.",
             "protocol " + std::to_string(static_cast<int>(NumberOf(*hello, "protocol", 0))));
        return false;
    }
    std::vector<std::string> used;
    {
        std::lock_guard lock(mutex_);
        level_ = std::clamp(static_cast<int>(NumberOf(*hello, "level", 1)), 1, 2);
        if (level_ >= 2) {
            for (const std::string& capability : StringsOf(*hello, "capabilities")) {
                const auto& ours = FrontendCapabilities();
                if (std::find(ours.begin(), ours.end(), capability) != ours.end()) {
                    capabilities_.insert(capability);
                    used.push_back(capability);
                }
            }
        }
    }
    log::Info("engine", "ecp: {} {} at level {} ({} capabilities)", hello->String("id"), hello->String("version"),
              level_, used.size());
    if (!WriteNow(Message("hello", 0,
                          {{"protocol", JsonNumber(PROTOCOL)},
                           {"frontend", JsonString("EmuDEN")},
                           {"version", JsonString(frontend_version_)},
                           {"level", JsonNumber(level_)},
                           {"capabilities", Strings(used)}}))) {
        return false;
    }
    hello_done_ = true;

    // The schema first, so the launch carries typed values.
    if (Has("settings")) {
        std::int64_t id;
        {
            std::lock_guard lock(mutex_);
            id = next_id_++;
        }
        if (!WriteNow(Message("get_schema", id))) {
            return false;
        }
        for (;;) {
            std::optional<JsonValue> frame = ReadFrame(stream);
            if (!frame) {
                return false;
            }
            if (frame->String("type") == "result" && NumberOf(*frame, "id", 0) == static_cast<double>(id)) {
                std::lock_guard lock(mutex_);
                if (const JsonValue* settings = frame->Find("settings")) {
                    for (const JsonValue& value : settings->items) {
                        if (auto setting = ReadSetting(value)) {
                            schema_.push_back(std::move(*setting));
                        }
                    }
                }
                break;
            }
            std::lock_guard lock(mutex_);
            events_.push_back(std::move(*frame));
        }
    }

    Fields fields = {{"content", JsonString(launch_.content)},
                     {"system", JsonString(launch_.system)},
                     {"game_id", JsonString(launch_.game_id)},
                     {"title", JsonString(launch_.title)},
                     {"locale", JsonString(launch_.locale)},
                     {"fullscreen", JsonBool(launch_.fullscreen)}};
    if (Has("menu") && !launch_.menu_chord.empty()) {
        fields.emplace_back("menu_chord", Strings(launch_.menu_chord));
    }
    {
        std::lock_guard lock(mutex_);
        for (const SettingDef& setting : schema_) {
            values_.emplace_back(setting.key, setting.default_value);
        }
        Fields settings;
        for (const auto& [key, value] : launch_.settings) {
            const SettingDef* setting = FindSetting(key);
            if (level_ < 2 || (!schema_.empty() && setting == nullptr)) {
                continue; // an engine that does not have the setting is not sent it
            }
            settings.emplace_back(key, TypedValue(setting, value));
            for (auto& current : values_) {
                if (current.first == key) {
                    current.second = value;
                }
            }
        }
        if (!settings.empty()) {
            fields.emplace_back("settings", JsonObject(std::move(settings)));
        }
        launch_id_ = next_id_++;
    }
    return WriteNow(Message("launch", launch_id_, std::move(fields)));
}

void Channel::Run() {
    if (Open() && !closing_ && Handshake()) {
        pal::Stream& stream = *stream_.load();
        while (std::optional<JsonValue> frame = ReadFrame(stream)) {
            std::lock_guard lock(mutex_);
            const auto id = static_cast<std::int64_t>(NumberOf(*frame, "id", 0));
            if (frame->String("type") == "result" && waiting_.count(id) != 0) {
                results_[id] = std::move(*frame);
                answered_.notify_all();
            } else {
                events_.push_back(std::move(*frame));
            }
        }
    }
    closed_ = true;
    std::lock_guard lock(mutex_);
    answered_.notify_all();
}

bool Channel::WriteNow(const JsonValue& message) {
    pal::Stream* stream = stream_.load();
    return stream != nullptr && !closed_ && WriteFrame(*stream, message);
}

std::int64_t Channel::Send(const std::string& type, Fields fields) {
    std::int64_t id;
    {
        std::lock_guard lock(mutex_);
        id = next_id_++;
    }
    if (!hello_done_ || !WriteNow(Message(type, id, std::move(fields)))) {
        log::Warn("engine", "ecp: {} could not be sent", type);
        return 0;
    }
    return id;
}

std::optional<JsonValue> Channel::Call(const std::string& type, Fields fields) {
    std::int64_t id;
    {
        std::lock_guard lock(mutex_);
        id = next_id_++;
        waiting_.insert(id);
    }
    if (!hello_done_ || !WriteNow(Message(type, id, std::move(fields)))) {
        std::lock_guard lock(mutex_);
        waiting_.erase(id);
        return std::nullopt;
    }
    std::unique_lock lock(mutex_);
    answered_.wait_for(lock, std::chrono::duration<double>(CALL_SECONDS),
                       [&] { return results_.count(id) != 0 || closed_ || closing_; });
    waiting_.erase(id);
    const auto it = results_.find(id);
    if (it == results_.end()) {
        log::Warn("engine", "ecp: {} got no answer", type);
        return std::nullopt;
    }
    JsonValue result = std::move(it->second);
    results_.erase(it);
    return result;
}

std::vector<Event> Channel::Drain(const std::string& game_id) {
    std::deque<JsonValue> messages;
    {
        std::lock_guard lock(mutex_);
        messages.swap(events_);
    }
    std::vector<Event> out;
    for (const JsonValue& message : messages) {
        Handle(message, out, game_id);
    }
    return out;
}

void Channel::Handle(const JsonValue& message, std::vector<Event>& out, const std::string& game_id) {
    const std::string type = message.String("type");
    Event event;
    event.game_id = game_id;
    if (type == "__failed") {
        event.type = Event::Type::Failed;
        event.message = message.String("message");
        out.push_back(std::move(event));
    } else if (type == "result") {
        const auto id = static_cast<std::int64_t>(NumberOf(message, "id", 0));
        if (id == launch_id_) {
            if (Ok(message)) {
                ready_ = true;
            } else {
                event.type = Event::Type::Failed;
                event.message = ErrorText(message);
                out.push_back(std::move(event));
            }
        } else if (!Ok(message)) {
            log::Warn("engine", "ecp: request {} failed: {}", id, ErrorText(message));
        }
    } else if (type == "loading") {
        event.type = Event::Type::Loading;
        event.progress = NumberOf(message, "progress", -1);
        event.message = message.String("text");
        out.push_back(std::move(event));
    } else if (type == "first_frame") {
        playing_ = true;
        first_frame_at_ = pal::NowSeconds();
        event.type = Event::Type::FirstFrame;
        out.push_back(std::move(event));
    } else if (type == "paused" || type == "resumed") {
        paused_ = type == "paused";
        event.type = paused_ ? Event::Type::Paused : Event::Type::Resumed;
        event.message = message.String("reason");
        out.push_back(std::move(event));
    } else if (type == "menu_request" && Has("menu")) {
        paused_ = true;
        menu_ = ReadPicture(message.Find("frame"));
        if (on_front) {
            on_front(false);
        }
        event.type = Event::Type::MenuRequested;
        out.push_back(std::move(event));
    } else if (type == "perf") {
        perf_.fps = NumberOf(message, "fps", 0);
        perf_.target_fps = NumberOf(message, "target_fps", 0);
        perf_.speed = NumberOf(message, "speed", 0);
        perf_.frame_ms = NumberOf(message, "frame_ms", 0);
        perf_.shaders_building = BoolOf(message, "shaders_building", false);
        perf_.valid = true;
    } else if (type == "stopped") {
        stopped_ = true;
        if (message.String("reason") == "error") {
            stop_error_ = ErrorText(message);
            event.type = Event::Type::Failed;
            event.message = stop_error_;
            out.push_back(std::move(event));
        }
    }
}

bool Channel::RequestStop() {
    if (!hello_done_ || closed_) {
        return false;
    }
    return Send("stop", {}) != 0;
}

const SettingDef* Channel::FindSetting(const std::string& key) const {
    const auto it = std::find_if(schema_.begin(), schema_.end(), [&](const SettingDef& s) { return s.key == key; });
    return it == schema_.end() ? nullptr : &*it;
}

int Channel::Level() const {
    std::lock_guard lock(mutex_);
    return level_;
}

bool Channel::Has(std::string_view capability) const {
    std::lock_guard lock(mutex_);
    return capabilities_.find(capability) != capabilities_.end();
}

bool Channel::Ready() const {
    return ready_;
}

bool Channel::Playing() const {
    return playing_;
}

bool Channel::Paused() const {
    return paused_;
}

Perf Channel::GetPerf() const {
    return perf_;
}

std::vector<SettingDef> Channel::Schema() const {
    std::lock_guard lock(mutex_);
    return schema_;
}

std::vector<std::pair<std::string, std::string>> Channel::Values() const {
    std::lock_guard lock(mutex_);
    return values_;
}

void Channel::Pause() {
    if (Has("pause") && !paused_) {
        Send("pause", {});
        paused_ = true;
        // EmuDEN asked (its own menu): it comes to the front over the engine.
        if (on_front) {
            on_front(false);
        }
    }
}

void Channel::Resume() {
    if (!Has("pause") && !Has("menu")) {
        return;
    }
    // The engine comes to the front first (Android: its activity again), then goes on.
    if (on_front) {
        on_front(true);
    }
    Send("resume", {});
    paused_ = false;
}

void Channel::Reset(bool hard) {
    if (Has("reset")) {
        Send("reset", {{"hard", JsonBool(hard)}});
    }
}

void Channel::SetSpeed(bool fast_forward, double factor) {
    if (Has("fast_forward")) {
        Fields fields = {{"fast_forward", JsonBool(fast_forward)}};
        if (factor > 1) {
            fields.emplace_back("factor", JsonNumber(factor));
        }
        Send("set_speed", std::move(fields));
    }
}

void Channel::SetRewind(bool on) {
    if (Has("rewind")) {
        Send("set_rewind", {{"on", JsonBool(on)}});
    }
}

void Channel::SetVolume(int percent, bool mute) {
    if (Has("volume")) {
        Send("set_volume", {{"percent", JsonNumber(std::clamp(percent, 0, 100))}, {"mute", JsonBool(mute)}});
    }
}

void Channel::SetSetting(const std::string& key, const std::string& value) {
    if (!Has("settings")) {
        return;
    }
    JsonValue typed;
    {
        std::lock_guard lock(mutex_);
        typed = TypedValue(FindSetting(key), value);
        for (auto& current : values_) {
            if (current.first == key) {
                current.second = value;
            }
        }
    }
    Send("set_setting", {{"key", JsonString(key)}, {"value", std::move(typed)}});
}

Result<StateInfo> Channel::SaveState(int slot) {
    if (!Has("states")) {
        return Status::Fail("This engine cannot save states.");
    }
    const std::optional<JsonValue> result = Call("save_state", {{"slot", JsonNumber(slot)}});
    if (!result) {
        return Status::Fail("The engine did not answer.");
    }
    if (!Ok(*result)) {
        return Status::Fail(ErrorText(*result));
    }
    StateInfo state = ReadState(*result);
    state.slot = slot;
    return state;
}

Status Channel::LoadState(int slot) {
    if (!Has("states")) {
        return Status::Fail("This engine cannot load states.");
    }
    const std::optional<JsonValue> result = Call("load_state", {{"slot", JsonNumber(slot)}});
    if (!result) {
        return Status::Fail("The engine did not answer.");
    }
    return Ok(*result) ? Status{} : Status::Fail(ErrorText(*result));
}

Result<std::vector<StateInfo>> Channel::ListStates() {
    if (!Has("states")) {
        return Status::Fail("This engine has no states.");
    }
    const std::optional<JsonValue> result = Call("list_states", {});
    if (!result) {
        return Status::Fail("The engine did not answer.");
    }
    if (!Ok(*result)) {
        return Status::Fail(ErrorText(*result));
    }
    std::vector<StateInfo> states;
    if (const JsonValue* list = result->Find("states")) {
        for (const JsonValue& value : list->items) {
            states.push_back(ReadState(value));
        }
    }
    return states;
}

std::optional<Picture> Channel::TakeMenuRequest() {
    std::optional<Picture> out;
    out.swap(menu_);
    return out;
}

} // namespace den::engine::ecp
