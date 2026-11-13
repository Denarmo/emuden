// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "den/base/files.h"
#include "den/base/log.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr const char* RUNNER_KEY = "den.runner";
constexpr int FAILURES_TO_STOP = 3;
constexpr std::size_t LOG_LINES = 200;
constexpr std::size_t MAX_SCRIPT = 1u << 20;

std::atomic<std::uint64_t> g_next_ask{1};

HttpSettings HttpSettingsOf(const RunnerConfig& config, const std::atomic<bool>* cancel) {
    HttpSettings settings;
    settings.http = config.http;
    const FeatureAccess& access = config.manifest.access;
    if (access.Has(PERMISSION_HTTP) || access.Has(PERMISSION_DOWNLOAD)) {
        settings.hosts = access.http_hosts;
    }
    settings.min_interval_ms = config.manifest.min_interval_ms;
    settings.cache_dir = config.data_dir / "cache" / "http";
    settings.user_agent = "EmuDEN/" + config.app_version + " (plugin " + config.manifest.id +
                          "/" + config.manifest.version.Text() + ")";
    settings.now = config.now;
    settings.cancel = cancel;
    return settings;
}

std::string ErrorText(lua_State* L) {
    const char* message = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
    return message != nullptr ? message : "the script failed";
}

} // namespace

Runner& Runner::Of(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, RUNNER_KEY);
    auto* runner = static_cast<Runner*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return *runner;
}

void SetFunction(lua_State* L, Runner&, const char* name, lua_CFunction function) {
    lua_pushcfunction(L, function);
    lua_setfield(L, -2, name);
}

std::string FieldText(lua_State* L, int table, const char* key) {
    std::string text;
    lua_getfield(L, table, key);
    if (lua_type(L, -1) == LUA_TSTRING) {
        std::size_t size = 0;
        const char* data = lua_tolstring(L, -1, &size);
        text.assign(data, size);
    } else if (!lua_isnil(L, -1)) {
        luaL_error(L, "%s must be text", key);
    }
    lua_pop(L, 1);
    return text;
}

Runner::Runner(RunnerConfig config)
    : config_(std::move(config)), http_(HttpSettingsOf(config_, &cancel_)) {
    busy_ = true; // until the script has loaded
    thread_ = std::thread([this] { ThreadMain(); });
}

Runner::~Runner() {
    cancel_.store(true);
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return helpers_ == 0; });
}

void Runner::Post(std::function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    wake_.notify_all();
}

void Runner::ThreadMain() {
    try {
        Boot();
    } catch (const std::exception& e) {
        Failed("load", e.what());
        std::lock_guard lock(mutex_);
        stopped_ = true;
    }
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            busy_ = false;
            idle_.notify_all();
            wake_.wait(lock, [this] { return quit_ || !tasks_.empty(); });
            if (quit_) {
                break;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
            busy_ = true;
        }
        try {
            task();
        } catch (const std::exception& e) {
            // A Lua error outside a protected call: the host's own use of the state.
            Failed("host", e.what());
            if (sandbox_ && sandbox_->L() != nullptr) {
                lua_settop(sandbox_->L(), 0);
            }
        }
    }
    coroutines_.clear();
    waiting_asks_.clear();
    sandbox_.reset();
}

void Runner::Boot() {
    std::error_code ec;
    fs::create_directories(config_.data_dir, ec);
    if (const auto text = ReadTextFile(config_.data_dir / "storage.json")) {
        if (auto stored = ParseJson(*text); stored && stored->kind == JsonValue::Kind::Object) {
            storage_ = std::move(*stored);
        }
    }
    if (storage_.kind != JsonValue::Kind::Object) {
        storage_ = JsonObject();
    }
    sandbox_ = std::make_unique<Sandbox>(
        static_cast<std::size_t>(config_.manifest.memory_mb) << 20, config_.instruction_limit);
    lua_State* L = sandbox_->L();
    if (L == nullptr) {
        throw std::runtime_error("There is not enough memory to start the plugin.");
    }
    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, RUNNER_KEY);
    lua_newtable(L);
    RegisterJson(L);
    RegisterCore(L, *this);
    RegisterNetwork(L, *this);
    RegisterFiles(L, *this);
    RegisterUi(L, *this);
    RegisterLibrary(L, *this);
    lua_getfield(L, -1, "log");
    lua_setglobal(L, "print");
    lua_setglobal(L, "den");

    fs::path entry;
    if (const Status found = Resolve("package/" + config_.manifest.entry, true, entry); !found.ok) {
        throw std::runtime_error(found.error);
    }
    const auto script = ReadTextFile(entry);
    if (!script || script->size() > MAX_SCRIPT) {
        throw std::runtime_error("The plugin's script could not be read.");
    }
    Coroutine main;
    main.id = next_coroutine_++;
    main.name = "load";
    main.remaining = sandbox_->InstructionLimit();
    main.thread = lua_newthread(L);
    main.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    const std::uint64_t id = main.id;
    Coroutine& loading = coroutines_.emplace(id, std::move(main)).first->second;
    const Status loaded =
        sandbox_->LoadText(loading.thread, *script, "@" + config_.manifest.entry);
    if (!loaded.ok) {
        Drop(id);
        throw std::runtime_error(loaded.error);
    }
    sandbox_->SetBudget(&loading.remaining);
    int results = 0;
    const int status = lua_resume(loading.thread, L, 0, &results);
    sandbox_->SetBudget(nullptr);
    std::string failure;
    if (status == LUA_YIELD) {
        failure = "The script may not wait while it loads; do that in plugin.open.";
    } else if (status != LUA_OK) {
        failure = ErrorText(loading.thread);
    } else if (results < 1 || !lua_istable(loading.thread, -results)) {
        failure = "The script must return its table of entry points.";
    } else {
        lua_pushvalue(loading.thread, -results);
        lua_xmove(loading.thread, L, 1);
        plugin_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    Drop(id);
    if (!failure.empty()) {
        throw std::runtime_error(failure);
    }
}

void Runner::Call(std::vector<std::string> path, std::vector<JsonValue> args) {
    Post([this, path = std::move(path), args = std::move(args)] { StartCall(path, args); });
}

void Runner::StartCall(const std::vector<std::string>& path, const std::vector<JsonValue>& args) {
    if (plugin_ref_ == 0 || Stopped()) {
        return;
    }
    lua_State* L = sandbox_->L();
    lua_settop(L, 0);
    lua_rawgeti(L, LUA_REGISTRYINDEX, plugin_ref_);
    std::string name;
    for (const std::string& part : path) {
        if (!lua_istable(L, -1)) {
            break;
        }
        // Raw, so that looking an entry point up runs no script code.
        lua_pushlstring(L, part.data(), part.size());
        lua_rawget(L, -2);
        lua_remove(L, -2);
        name += (name.empty() ? "" : ".") + part;
    }
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, 0);
        if (!path.empty() && path.front() == "actions") {
            log::Warn("plugins", "{} has no {}", config_.manifest.id, name);
        }
        return;
    }
    Coroutine call;
    call.id = next_coroutine_++;
    call.name = name;
    call.remaining = sandbox_->InstructionLimit();
    call.thread = lua_newthread(L);
    call.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, call.thread, 1);
    const std::uint64_t id = call.id;
    lua_State* thread = call.thread;
    coroutines_.emplace(id, std::move(call));
    for (const JsonValue& arg : args) {
        if (arg.kind == JsonValue::Kind::Null) {
            lua_pushnil(thread); // an argument that was not given
        } else {
            PushJson(thread, arg);
        }
    }
    Resume(id, static_cast<int>(args.size()));
}

void Runner::CallProgress(int ref, float fraction) {
    Post([this, ref, fraction] {
        if (Stopped()) {
            return;
        }
        lua_State* L = sandbox_->L();
        lua_settop(L, 0);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, 0);
            return;
        }
        Coroutine call;
        call.id = next_coroutine_++;
        call.name = "on_progress";
        call.remaining = sandbox_->InstructionLimit();
        call.thread = lua_newthread(L);
        call.ref = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_xmove(L, call.thread, 1);
        const std::uint64_t id = call.id;
        lua_pushnumber(call.thread, fraction);
        coroutines_.emplace(id, std::move(call));
        Resume(id, 1);
    });
}

void Runner::ReleaseRef(int ref) {
    Post([this, ref] { luaL_unref(sandbox_->L(), LUA_REGISTRYINDEX, ref); });
}

void Runner::Resume(std::uint64_t id, int arguments) {
    const auto found = coroutines_.find(id);
    if (found == coroutines_.end()) {
        return;
    }
    Coroutine& co = found->second;
    sandbox_->SetBudget(&co.remaining);
    int results = 0;
    const int status = lua_resume(co.thread, sandbox_->L(), arguments, &results);
    sandbox_->SetBudget(nullptr);
    if (status == LUA_OK) {
        Succeeded();
        Drop(id);
        return;
    }
    if (status != LUA_YIELD) {
        Failed(co.name, ErrorText(co.thread));
        Drop(id);
        return;
    }
    lua_pop(co.thread, results);
    if (co.pending) {
        AsyncOp op = std::move(co.pending);
        co.pending = nullptr;
        {
            std::lock_guard lock(mutex_);
            ++helpers_;
        }
        std::thread([this, id, op = std::move(op)] {
            Outcome outcome;
            try {
                outcome = op();
            } catch (const std::exception& e) {
                outcome = [message = std::string(e.what())](lua_State* L) {
                    return Refuse(L, message);
                };
            }
            Post([this, id, outcome = std::move(outcome)] { ResumeWith(id, outcome); });
            std::lock_guard lock(mutex_);
            --helpers_;
            idle_.notify_all();
        }).detach();
    } else if (!co.waits_for_ask) {
        Failed(co.name, "The script paused outside a den call.");
        Drop(id);
    }
}

void Runner::ResumeWith(std::uint64_t id, const Outcome& outcome) {
    const auto found = coroutines_.find(id);
    if (found == coroutines_.end()) {
        return;
    }
    const int pushed = outcome ? outcome(found->second.thread) : 0;
    Resume(id, pushed);
}

void Runner::Drop(std::uint64_t id) {
    const auto found = coroutines_.find(id);
    if (found != coroutines_.end()) {
        luaL_unref(sandbox_->L(), LUA_REGISTRYINDEX, found->second.ref);
        coroutines_.erase(found);
    }
}

void Runner::Failed(const std::string& name, const std::string& message) {
    log::Warn("plugins", "{} {}: {}", config_.manifest.id, name, message);
    std::lock_guard lock(mutex_);
    error_ = message;
    if (++failures_ >= FAILURES_TO_STOP) {
        stopped_ = true;
    }
    ++revision_;
}

void Runner::Succeeded() {
    std::lock_guard lock(mutex_);
    failures_ = 0;
    if (!error_.empty()) {
        error_.clear();
        ++revision_;
    }
}

Runner::Coroutine* Runner::Current(lua_State* L) {
    for (auto& [id, co] : coroutines_) {
        if (co.thread == L) {
            return &co;
        }
    }
    return nullptr;
}

int Runner::Suspend(lua_State* L, AsyncOp op) {
    Coroutine* co = Current(L);
    if (co == nullptr) {
        return luaL_error(L, "a den call that waits must be made from the plugin's own function, "
                             "not from a coroutine of the script");
    }
    co->pending = std::move(op);
    return lua_yield(L, 0);
}

int Runner::SuspendForAsk(lua_State* L, Ask ask, AnswerFn answer) {
    Coroutine* co = Current(L);
    if (co == nullptr) {
        return luaL_error(L, "a den call that waits must be made from the plugin's own function, "
                             "not from a coroutine of the script");
    }
    ask.id = g_next_ask.fetch_add(1);
    waiting_asks_[ask.id] = {co->id, std::move(answer)};
    co->waits_for_ask = true;
    {
        std::lock_guard lock(mutex_);
        open_asks_.push_back(ask.id);
        asks_.push_back(std::move(ask));
        ++revision_;
    }
    return lua_yield(L, 0);
}

void Runner::PostAsk(Ask ask) {
    ask.id = g_next_ask.fetch_add(1);
    std::lock_guard lock(mutex_);
    if (asks_.size() < 64) {
        asks_.push_back(std::move(ask));
        ++revision_;
    }
}

bool Runner::Answer(std::uint64_t ask_id, bool accepted, const std::string& value) {
    {
        std::lock_guard lock(mutex_);
        const auto found = std::find(open_asks_.begin(), open_asks_.end(), ask_id);
        if (found == open_asks_.end()) {
            return false;
        }
        open_asks_.erase(found);
    }
    Post([this, ask_id, accepted, value] {
        const auto waiting = waiting_asks_.find(ask_id);
        if (waiting == waiting_asks_.end()) {
            return;
        }
        const std::uint64_t id = waiting->second.first;
        const AnswerFn answer = std::move(waiting->second.second);
        waiting_asks_.erase(waiting);
        const auto found = coroutines_.find(id);
        if (found == coroutines_.end()) {
            return;
        }
        found->second.waits_for_ask = false;
        const int pushed = answer(found->second.thread, accepted, value);
        Resume(id, pushed);
    });
    return true;
}

std::vector<PageModel> Runner::Pages(std::uint64_t& revision) const {
    std::lock_guard lock(mutex_);
    revision = revision_.load();
    return pages_;
}

void Runner::ChangePages(const std::function<bool(std::vector<PageModel>&)>& fn) {
    std::lock_guard lock(mutex_);
    if (fn(pages_)) {
        ++revision_;
    }
}

bool Runner::Back() {
    std::lock_guard lock(mutex_);
    if (pages_.size() <= 1) {
        return false;
    }
    pages_.pop_back();
    ++revision_;
    return true;
}

std::vector<Ask> Runner::TakeAsks() {
    std::lock_guard lock(mutex_);
    return std::exchange(asks_, {});
}

void Runner::Finish() {
    Post([this] { StartCall({"close"}, {}); });
    Post([this] {
        std::lock_guard lock(mutex_);
        quit_ = true;
    });
}

bool Runner::HasPages() const {
    std::lock_guard lock(mutex_);
    return !pages_.empty();
}

std::string Runner::Error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

bool Runner::Stopped() const {
    std::lock_guard lock(mutex_);
    return stopped_;
}

void Runner::Settle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return tasks_.empty() && !busy_ && helpers_ == 0; });
}

void Runner::AddLog(std::string line) {
    log::Info("plugins", "[{}] {}", config_.manifest.id, line);
    std::lock_guard lock(mutex_);
    log_.push_back(std::move(line));
    if (log_.size() > LOG_LINES) {
        log_.pop_front();
    }
}

std::vector<std::string> Runner::Log() const {
    std::lock_guard lock(mutex_);
    return {log_.begin(), log_.end()};
}

Status Runner::Resolve(std::string_view script_path, bool package, fs::path& out) const {
    fs::path root = config_.data_dir;
    if (script_path == "package" || script_path.starts_with("package/")) {
        if (!package) {
            return Status::Fail("the package cannot be changed");
        }
        root = config_.package_dir;
        script_path.remove_prefix(script_path.size() > 7 ? 8 : 7);
    }
    if (script_path == ".") {
        script_path = {};
    }
    if (!script_path.empty() && !IsSafeRelativePath(script_path)) {
        return Status::Fail("the path leaves the plugin's folder");
    }
    out = script_path.empty() ? root : root / PathFromUtf8(script_path);
    // A link inside the folder must not lead out of it either.
    std::error_code ec;
    const fs::path real_root = fs::weakly_canonical(root, ec);
    const fs::path real = fs::weakly_canonical(out, ec);
    const fs::path relative = real.lexically_relative(real_root);
    if (ec || relative.empty() || *relative.begin() == "..") {
        return Status::Fail("the path leaves the plugin's folder");
    }
    return {};
}

std::uint64_t Runner::StorageLeft() const {
    std::uint64_t used = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(config_.data_dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            used += it->file_size(ec);
        }
    }
    const std::uint64_t limit = static_cast<std::uint64_t>(config_.manifest.storage_mb) << 20;
    return used >= limit ? 0 : limit - used;
}

bool Runner::SaveStorage() {
    return WriteFileAtomic(config_.data_dir / "storage.json", CompactJson(storage_));
}

} // namespace den::plugins::feature
