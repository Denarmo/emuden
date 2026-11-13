// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One running feature plugin: its Lua state on its own worker thread. Every entry point of
// the script runs in a fresh coroutine; a den call that waits (http, a file picker, patching)
// suspends that coroutine, the work is done on another thread and the coroutine goes on with
// the answer. The main thread only posts calls and reads the page stack.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "den/base/json.h"
#include "den/plugins/feature_manifest.h"
#include "den/plugins/feature_services.h"
#include "den/plugins/feature_ui.h"
#include "feature/http.h"
#include "feature/sandbox.h"

namespace den::plugins::feature {

struct RunnerConfig {
    FeatureManifest manifest;
    std::filesystem::path package_dir; // the scripts, read-only
    std::filesystem::path data_dir;    // <data>/plugin-data/<id>
    std::filesystem::path downloads_dir;
    std::string app_version;
    std::string platform;
    HttpGetter http;
    std::shared_ptr<FeatureServices> services; // may be null
    std::function<std::int64_t()> now;
    long long instruction_limit = 200'000'000;
};

/// Pushes the results of a finished wait on the coroutine's stack; returns how many.
using Outcome = std::function<int(lua_State*)>;
/// The waited-for work, on a helper thread. It may not touch Lua.
using AsyncOp = std::function<Outcome()>;
/// The screen's answer to an ask, on the worker thread.
using AnswerFn = std::function<int(lua_State*, bool accepted, const std::string& value)>;

class Runner {
public:
    explicit Runner(RunnerConfig config);
    ~Runner(); // cancels waits, stops the thread
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    // From any thread.
    /// Runs plugin.<path...>(args...) in a fresh coroutine; a missing function is skipped.
    void Call(std::vector<std::string> path, std::vector<JsonValue> args);
    std::vector<PageModel> Pages(std::uint64_t& revision) const;
    std::uint64_t Revision() const {
        return revision_.load();
    }
    /// Pops a pushed page; false when one page (or none) is left.
    bool Back();
    std::vector<Ask> TakeAsks();
    /// False when the ask is not this runner's.
    bool Answer(std::uint64_t ask_id, bool accepted, const std::string& value);
    std::string Error() const;
    bool Stopped() const; // the script did not load, or failed three times in a row
    /// Blocks until every posted call has finished or is waiting (tests).
    void Settle();
    std::vector<std::string> Log() const; // the last den.log lines, for developer plugins
    /// Runs plugin.close after the calls already posted, then lets the thread end.
    void Finish();
    /// Moves the revision: something the pages show (an image) is ready.
    void Touch() {
        ++revision_;
    }
    bool HasPages() const;

    // For the den functions, on the worker thread.
    const RunnerConfig& Config() const {
        return config_;
    }
    HttpPolicy& Http() {
        return http_;
    }
    const std::atomic<bool>& Cancel() const {
        return cancel_;
    }
    /// Suspends the calling coroutine until `op` has run on a helper thread.
    int Suspend(lua_State* L, AsyncOp op);
    /// Suspends the calling coroutine until the screen answered `ask`.
    int SuspendForAsk(lua_State* L, Ask ask, AnswerFn answer);
    void PostAsk(Ask ask);
    /// Runs the function `ref` (a registry reference) with a number, for den.download's
    /// on_progress. Posted from a helper thread.
    void CallProgress(int ref, float fraction);
    void ReleaseRef(int ref);
    void AddLog(std::string line);
    /// `fn` changes the page stack under the lock; the revision moves when it returns true.
    void ChangePages(const std::function<bool(std::vector<PageModel>&)>& fn);
    /// The file a script path names: under the data folder, or under the package for
    /// "package/..." when `package` allows it. Fails for anything that leaves the folder.
    Status Resolve(std::string_view script_path, bool package, std::filesystem::path& out) const;
    /// Bytes the data folder may still take within the plugin's storage limit.
    std::uint64_t StorageLeft() const;
    JsonValue& Storage() {
        return storage_;
    }
    bool SaveStorage();
    std::map<std::string, std::filesystem::path>& InboxTokens() {
        return inbox_tokens_;
    }

    static Runner& Of(lua_State* L);

private:
    struct Coroutine {
        std::uint64_t id = 0;
        lua_State* thread = nullptr;
        int ref = 0;
        long long remaining = 0;
        std::string name;
        AsyncOp pending;
        bool waits_for_ask = false;
    };

    void ThreadMain();
    void Post(std::function<void()> task);
    void Boot();
    void StartCall(const std::vector<std::string>& path, const std::vector<JsonValue>& args);
    void Resume(std::uint64_t id, int arguments);
    void ResumeWith(std::uint64_t id, const Outcome& outcome);
    void Drop(std::uint64_t id);
    void Failed(const std::string& name, const std::string& message);
    void Succeeded();
    Coroutine* Current(lua_State* L);

    RunnerConfig config_;
    std::atomic<bool> cancel_{false};
    HttpPolicy http_;

    // Worker thread only.
    std::unique_ptr<Sandbox> sandbox_;
    int plugin_ref_ = 0; // the table the entry script returned
    std::map<std::uint64_t, Coroutine> coroutines_;
    std::uint64_t next_coroutine_ = 1;
    std::map<std::uint64_t, std::pair<std::uint64_t, AnswerFn>> waiting_asks_; // ask -> coroutine
    JsonValue storage_;
    std::map<std::string, std::filesystem::path> inbox_tokens_;
    int failures_ = 0;

    mutable std::mutex mutex_; // everything below
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> tasks_;
    bool quit_ = false;
    bool busy_ = false;
    int helpers_ = 0;
    std::vector<PageModel> pages_;
    std::vector<Ask> asks_;
    std::vector<std::uint64_t> open_asks_;
    std::string error_;
    bool stopped_ = false;
    std::deque<std::string> log_;
    std::atomic<std::uint64_t> revision_{1};

    std::thread thread_;
};

/// Copies a file from outside into `data_dir`/inbox under a safe name. Returns the script
/// path ("inbox/<name>"), or the reason: not a file, not one of `extensions` (empty = any),
/// larger than `room`.
Result<std::string> CopyIntoInbox(const std::filesystem::path& data_dir,
                                  const std::filesystem::path& file,
                                  const std::vector<std::string>& extensions, std::uint64_t room);

/// Each adds its part of the `den` table (on top of the stack).
void RegisterJson(lua_State* L);
void RegisterCore(lua_State* L, Runner& runner);    // constants, log, sleep, now, include, storage
void RegisterNetwork(lua_State* L, Runner& runner); // http, download, open_url
void RegisterFiles(lua_State* L, Runner& runner);   // files
void RegisterUi(lua_State* L, Runner& runner);      // ui, app
void RegisterLibrary(lua_State* L, Runner& runner); // patch, library

/// Sets `name` in the table on top of the stack to the function, with the runner as upvalue.
void SetFunction(lua_State* L, Runner& runner, const char* name, lua_CFunction function);
/// The option table argument's string field, "" when absent; raises when it is not text.
std::string FieldText(lua_State* L, int table, const char* key);

} // namespace den::plugins::feature
