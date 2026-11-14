// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// player::Session over an engine app: the game runs in the engine's own window or activity;
// the pause menu, the states and the options reach it through the engine host's protocol
// controls. EmuDEN's slots 0..9 are the engine's 1..10; its slot 0 is the resume state.
// Option values are kept as lists of "key=value" under engine_options.<core> and
// engine_game_options.<system>.<game>.

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <set>
#include <system_error>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/gfx/images.h"
#include "den/platform/pal.h"
#include "den/player/session.h"
#include "den/store/settings.h"

namespace den::player {

namespace {

constexpr int RESUME_SLOT = 0;
constexpr std::size_t MAX_RANGE_VALUES = 101;

int EngineSlot(int slot) {
    return slot + 1;
}

class EngineSession final : public Session {
public:
    EngineSession(const Config& config, engine::Host& host) : config_(config), host_(host) {}
    ~EngineSession() override {
        DropFrame();
    }

    Status Open(const OpenRequest& request) override {
        Close();
        request_ = request;
        error_.clear();
        frames_ = 0;
        resumed_ = false;
        resume_checked_ = false;
        states_.clear();
        if (!request.engine) {
            return Fail("No engine was chosen for this game.");
        }
        core_id_ = request.engine->core_id.empty() ? request.system : request.engine->core_id;
        engine::LaunchRequest launch;
        launch.system = request.system;
        launch.game_id = request.game_id;
        launch.title = request.title;
        launch.content = request.content;
        launch.fullscreen = request.fullscreen;
        launch.install = request.engine;
        launch.controlled = true;
        launch.settings = request.option_hints;
        for (const auto& [key, value] : StoredValues()) {
            Put(launch.settings, key, value);
        }
        if (const Status started = host_.Start(launch); !started) {
            // The host already said why; drop its event so it is not shown twice.
            host_.PollEvents();
            return Fail(started.error);
        }
        state_ = State::Running;
        return {};
    }

    void Close() override {
        if (state_ == State::Running || state_ == State::Paused) {
            engine::Control* control = Live();
            // The resume state, as a libretro session writes one when it closes.
            if (control != nullptr && control->Playing() && control->Has("states") && ResumeAllowed()) {
                if (const auto saved = control->SaveState(RESUME_SLOT); !saved) {
                    log::Warn("player", "the engine kept no resume state: {}", saved.error());
                }
            }
            if (host_.Running()) {
                host_.Stop();
            }
        }
        state_ = State::Closed;
        DropFrame();
    }

    void Pause() override {
        if (engine::Control* control = Live()) {
            control->Pause();
        }
        if (state_ == State::Running) {
            state_ = State::Paused;
        }
    }

    void Resume() override {
        DropFrame();
        if (engine::Control* control = Live()) {
            control->Resume();
        }
        if (state_ == State::Paused) {
            state_ = State::Running;
        }
    }

    void SetPad(int, const input::PadState&) override {}
    void SetPointer(float, float, bool) override {}

    void Advance(double seconds) override {
        for (const engine::Event& event : host_.PollEvents()) {
            Handle(event);
        }
        engine::Control* control = Live();
        if (control == nullptr) {
            return;
        }
        if (!resume_checked_ && control->Playing()) {
            resume_checked_ = true;
            ReadStates(*control);
            if (request_.resume && ResumeAllowed() && control->Has("states") && states_.count(RESUME_SLOT) != 0) {
                resumed_ = control->LoadState(RESUME_SLOT).ok;
            }
        }
        if (state_ == State::Running && control->Playing() && !control->Paused()) {
            const engine::Perf perf = control->GetPerf();
            frames_ += (perf.valid && perf.fps > 0 ? perf.fps : 60.0) * seconds;
        }
    }

    void Draw(gfx::Canvas& canvas, const gfx::Rect& area) override {
        canvas.FillRect(area, gfx::Color{0, 0, 0, 255});
        if (!frame_) {
            return;
        }
        // The paused frame, letterboxed, behind the menu.
        const float aspect = static_cast<float>(frame_.width) / static_cast<float>(std::max(1, frame_.height));
        float w = area.w, h = area.w / aspect;
        if (h > area.h) {
            h = area.h;
            w = area.h * aspect;
        }
        canvas.DrawImage({area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h}, frame_);
    }

    Status SaveState(int slot) override {
        engine::Control* control = Live();
        if (control == nullptr) {
            return Status::Fail("The engine is not running.");
        }
        auto saved = control->SaveState(EngineSlot(slot));
        if (!saved) {
            return saved.status();
        }
        states_.insert(EngineSlot(slot));
        WriteThumbnail(EngineSlot(slot), saved.value().thumbnail);
        return {};
    }

    Status LoadState(int slot) override {
        engine::Control* control = Live();
        return control != nullptr ? control->LoadState(EngineSlot(slot)) : Status::Fail("The engine is not running.");
    }

    bool HasState(int slot) const override {
        return states_.count(EngineSlot(slot)) != 0;
    }

    std::filesystem::path StateThumbnail(int slot) const override {
        const std::filesystem::path file = ThumbnailFile(EngineSlot(slot));
        return HasState(slot) && IsRegularFile(file) ? file : std::filesystem::path();
    }

    void SetFastForward(bool on) override {
        if (engine::Control* control = Live()) {
            const double factor =
                config_.settings ? config_.settings->GetNumber("player.fast_forward_speed", 3) : 3;
            control->SetSpeed(on, factor);
            fast_ = on;
        }
    }

    void SetRewind(bool on) override {
        if (engine::Control* control = Live()) {
            control->SetRewind(on);
        }
    }

    void Reset() override {
        if (engine::Control* control = Live()) {
            control->Reset(false);
        }
    }

    void FlushSaves() override {}

    std::vector<CoreOption> Options() const override {
        std::vector<CoreOption> out;
        const engine::Control* control = Live();
        if (control == nullptr || !control->Has("settings")) {
            return out;
        }
        const auto values = control->Values();
        for (const engine::SettingDef& setting : control->Schema()) {
            CoreOption option;
            option.key = setting.key;
            option.label = setting.label;
            option.default_value = setting.default_value;
            option.value = setting.default_value;
            for (const auto& [key, value] : values) {
                if (key == setting.key) {
                    option.value = value;
                }
            }
            option.values = ValuesOf(setting);
            if (option.values.empty()) {
                continue; // free text has no row in the options page
            }
            option.for_game = config_.settings != nullptr && HasGameValue(setting.key);
            out.push_back(std::move(option));
        }
        return out;
    }

    void SetOption(const std::string& key, const std::string& value) override {
        SetOption(key, value, OptionScope::Console);
    }

    void SetOption(const std::string& key, const std::string& value, OptionScope scope) override {
        if (config_.settings != nullptr) {
            if (scope == OptionScope::Game) {
                StoreValue(GameList(), key, value);
            } else {
                StoreValue(ConsoleList(), key, value);
                StoreValue(GameList(), key, std::nullopt);
            }
        }
        if (engine::Control* control = Live()) {
            control->SetSetting(key, value);
        }
    }

    void ClearGameOptions() override {
        if (config_.settings != nullptr) {
            config_.settings->Remove(GameList());
        }
    }

    void SetVolume(float volume) override {
        engine::Control* control = Live();
        if (control == nullptr) {
            return;
        }
        const int percent = static_cast<int>(std::lround(std::clamp(volume, 0.0f, 1.0f) * 100));
        if (control->Has("volume")) {
            control->SetVolume(percent, percent == 0);
        } else if (HasSetting(*control, "audio.volume")) {
            control->SetSetting("audio.volume", std::to_string(percent));
        }
    }

    Snapshot GetSnapshot() const override {
        Snapshot snapshot;
        snapshot.state = state_;
        snapshot.error = error_;
        snapshot.core_name = core_id_;
        snapshot.game_id = request_.game_id;
        snapshot.external = true;
        snapshot.can_picture = false;
        snapshot.frames = static_cast<std::uint64_t>(frames_);
        snapshot.fast_forward = fast_;
        snapshot.resumed = resumed_;
        const engine::Control* control = Live();
        snapshot.can_save_states = control != nullptr && control->Has("states");
        snapshot.can_rewind = control != nullptr && control->Has("rewind");
        snapshot.can_pause = control != nullptr && (control->Has("pause") || control->Has("menu"));
        snapshot.can_fast_forward = control != nullptr && control->Has("fast_forward");
        snapshot.can_reset = control != nullptr && control->Has("reset");
        snapshot.can_options = control != nullptr && control->Has("settings");
        snapshot.can_volume = control != nullptr && (control->Has("volume") || HasSetting(*control, "audio.volume"));
        if (control != nullptr) {
            const engine::Perf perf = control->GetPerf();
            snapshot.measured_fps = perf.fps;
            snapshot.core_fps = perf.target_fps > 0 ? perf.target_fps : 60.0;
        }
        snapshot.frame_width = frame_.width;
        snapshot.frame_height = frame_.height;
        if (frame_.height > 0) {
            snapshot.aspect = static_cast<float>(frame_.width) / static_cast<float>(frame_.height);
        }
        return snapshot;
    }

    bool TakeMenuRequest() override {
        const bool requested = menu_requested_;
        menu_requested_ = false;
        return requested;
    }

private:
    Status Fail(std::string message) {
        state_ = State::Failed;
        error_ = message;
        return Status::Fail(std::move(message));
    }

    engine::Control* Live() const {
        return state_ == State::Closed || state_ == State::Failed ? nullptr : host_.Control();
    }

    void Handle(const engine::Event& event) {
        using Type = engine::Event::Type;
        if (event.game_id != request_.game_id) {
            return;
        }
        switch (event.type) {
        case Type::Failed:
            error_ = event.message;
            log::Warn("player", "{}: {}", request_.game_id, event.message);
            break;
        case Type::Exited:
            state_ = error_.empty() ? State::Closed : State::Failed;
            DropFrame();
            break;
        case Type::MenuRequested:
            if (engine::Control* control = host_.Control()) {
                if (auto picture = control->TakeMenuRequest()) {
                    ShowFrame(*picture);
                }
            }
            if (state_ == State::Running) {
                state_ = State::Paused;
            }
            menu_requested_ = true;
            break;
        case Type::Resumed:
            if (state_ == State::Paused) {
                state_ = State::Running;
            }
            break;
        default:
            break;
        }
    }

    void ReadStates(engine::Control& control) {
        states_.clear();
        if (!control.Has("states")) {
            return;
        }
        const auto listed = control.ListStates();
        if (!listed) {
            return;
        }
        for (const engine::StateInfo& state : listed.value()) {
            states_.insert(state.slot);
            if (state.slot != RESUME_SLOT) {
                WriteThumbnail(state.slot, state.thumbnail);
            }
        }
    }

    std::filesystem::path ThumbnailFile(int engine_slot) const {
        return config_.data_dir / "states" / "engines" / PathFromUtf8(core_id_) / PathFromUtf8(request_.game_id) /
               PathFromUtf8(std::format("slot{}.png", engine_slot));
    }

    void WriteThumbnail(int engine_slot, const engine::Picture& picture) const {
        const std::filesystem::path file = ThumbnailFile(engine_slot);
        std::error_code ec;
        if (picture.Empty()) {
            std::filesystem::remove(file, ec);
            return;
        }
        std::filesystem::create_directories(file.parent_path(), ec);
        WriteFileAtomic(file, std::string_view(reinterpret_cast<const char*>(picture.png.data()), picture.png.size()));
    }

    void ShowFrame(const engine::Picture& picture) {
        DropFrame();
        if (picture.Empty() || config_.device == nullptr) {
            return;
        }
        auto image = gfx::DecodeImage(picture.png.data(), picture.png.size());
        if (!image) {
            log::Warn("player", "the engine's paused frame could not be read");
            return;
        }
        frame_ = config_.device->CreateTexture(image->width, image->height, gfx::PixelFormat::Rgba8,
                                               image->rgba.data());
    }

    void DropFrame() {
        if (frame_ && config_.device != nullptr) {
            config_.device->DestroyTexture(frame_);
        }
        frame_ = {};
    }

    bool ResumeAllowed() const {
        return config_.settings == nullptr || config_.settings->GetBool("player.resume", true);
    }

    static bool HasSetting(const engine::Control& control, const std::string& key) {
        const auto schema = control.Schema();
        return std::any_of(schema.begin(), schema.end(), [&](const engine::SettingDef& s) { return s.key == key; });
    }

    static std::vector<std::string> ValuesOf(const engine::SettingDef& setting) {
        std::vector<std::string> values;
        if (setting.type == "toggle") {
            return {"false", "true"};
        }
        if (setting.type == "choice") {
            for (const auto& choice : setting.choices) {
                values.push_back(choice.first);
            }
        } else if (setting.type == "range" && setting.step > 0 && setting.max >= setting.min) {
            for (double v = setting.min; v <= setting.max + 1e-9 && values.size() < MAX_RANGE_VALUES; v += setting.step) {
                values.push_back(v == std::floor(v) ? std::to_string(static_cast<long long>(v)) : std::format("{}", v));
            }
        }
        return values;
    }

    static void Put(std::vector<std::pair<std::string, std::string>>& values, const std::string& key,
                    const std::string& value) {
        for (auto& existing : values) {
            if (existing.first == key) {
                existing.second = value;
                return;
            }
        }
        values.emplace_back(key, value);
    }

    // The user's values are kept per engine and per game as lists of "key=value".
    std::string ConsoleList() const {
        return "engine_options." + core_id_;
    }
    std::string GameList() const {
        return "engine_game_options." + request_.system + "." + request_.game_id;
    }

    std::vector<std::pair<std::string, std::string>> ReadList(const std::string& list) const {
        std::vector<std::pair<std::string, std::string>> out;
        for (const std::string& entry : config_.settings->GetList(list)) {
            const std::size_t eq = entry.find('=');
            if (eq != std::string::npos && eq > 0) {
                out.emplace_back(entry.substr(0, eq), entry.substr(eq + 1));
            }
        }
        return out;
    }

    void WriteList(const std::string& list, const std::vector<std::pair<std::string, std::string>>& values) {
        std::vector<std::string> entries;
        for (const auto& [key, value] : values) {
            entries.push_back(key + "=" + value);
        }
        if (entries.empty()) {
            config_.settings->Remove(list);
        } else {
            config_.settings->SetList(list, entries);
        }
    }

    bool HasGameValue(const std::string& key) const {
        const auto values = ReadList(GameList());
        return std::any_of(values.begin(), values.end(), [&](const auto& v) { return v.first == key; });
    }

    void StoreValue(const std::string& list, const std::string& key, const std::optional<std::string>& value) {
        auto values = ReadList(list);
        values.erase(std::remove_if(values.begin(), values.end(), [&](const auto& v) { return v.first == key; }),
                     values.end());
        if (value) {
            values.emplace_back(key, *value);
        }
        WriteList(list, values);
    }

    // The console's values, then this game's over them.
    std::vector<std::pair<std::string, std::string>> StoredValues() const {
        std::vector<std::pair<std::string, std::string>> out;
        if (config_.settings == nullptr) {
            return out;
        }
        for (const std::string& list : {ConsoleList(), GameList()}) {
            for (const auto& [key, value] : ReadList(list)) {
                Put(out, key, value);
            }
        }
        return out;
    }

    Config config_;
    engine::Host& host_;
    OpenRequest request_;
    std::string core_id_;
    State state_ = State::Closed;
    std::string error_;
    double frames_ = 0;
    bool fast_ = false;
    bool resumed_ = false;
    bool resume_checked_ = false;
    bool menu_requested_ = false;
    std::set<int> states_; // the engine's slots that hold a state
    gfx::Texture frame_;
};

} // namespace

std::unique_ptr<Session> CreateEngineSession(const Config& config, engine::Host& host) {
    return std::make_unique<EngineSession>(config, host);
}

} // namespace den::player
