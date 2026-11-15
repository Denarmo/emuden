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
#include "den/plugins/feature_achievements.h"
#include "den/plugins/feature_tune.h"
#include "den/plugins/feature_ui.h"
#include "den/plugins/sync_link.h"
#include "feature/http.h"
#include "feature/sandbox.h"

namespace den::pal {
class Secrets;
}

namespace den::plugins::feature {

struct RunnerConfig {
    FeatureManifest manifest;
    std::filesystem::path package_dir; // the scripts, read-only
    std::filesystem::path data_dir;    // <data>/plugin-data/<id>
    std::filesystem::path downloads_dir;
    std::filesystem::path bookmarks_file; // the browser page's bookmarks, for browse.any
    std::string app_version;
    std::string platform;
    bool developer = false; // from the developer folder: may show a test site of this machine
    std::function<bool()> web_available; // may be empty: no in-app browser
    HttpGetter http;
    std::shared_ptr<FeatureServices> services; // may be null
    std::shared_ptr<TuneServices> tune;         // den.device.info, den.game, den.steam; may be null
    std::shared_ptr<AchievementServices> achievements; // den.achievements; may be null
    std::function<std::int64_t()> now;
    long long instruction_limit = 200'000'000;
    pal::Secrets* secrets = nullptr;      // den.secrets; null: pal::GetSecrets()
    std::shared_ptr<SyncLink> sync;       // den.sync, for a plugin with the sync permission
    /// A file the host lent for one provider call ("sync:<token>"), or nothing.
    std::function<std::optional<std::filesystem::path>(const std::string& token)> lent;
};

/// What an invoked function returned: its values, or why there are none (it failed, or
/// returned nil and a message).
using InvokeDone = std::function<void(Result<std::vector<JsonValue>>)>;
/// A progress function handed to an invoked function: (done, total).
using InvokeProgress = std::function<void(double, double)>;

/// Pushes the results of a finished wait on the coroutine's stack; returns how many.
using Outcome = std::function<int(lua_State*)>;
/// The waited-for work, on a helper thread. It may not touch Lua.
using AsyncOp = std::function<Outcome()>;
/// The screen's answer to an ask, on the worker thread.
using AnswerFn = std::function<int(lua_State*, bool accepted, const std::string& value)>;
/// One of the answers to an ask that is answered many times, on the worker thread.
using StreamFn = std::function<void(bool accepted, const std::string& value)>;

/// The plugin's visit to the in-app browser (den.web): the page that is open and the files
/// that arrived from it and were not handed to the script yet.
struct WebVisit {
    struct File {
        std::string path, name, host, title;
    };
    bool open = false;
    bool any = false; // den.web.open{any = true}: every file, kept in the Downloads folder
    std::uint64_t waiting = 0; // the coroutine inside den.web.open
    std::vector<std::string> extensions;
    std::deque<File> ready;
};

class Runner {
public:
    explicit Runner(RunnerConfig config);
    ~Runner(); // cancels waits, stops the thread
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    // From any thread.
    /// Runs plugin.<path...>(args...) in a fresh coroutine; a missing function is skipped.
    void Call(std::vector<std::string> path, std::vector<JsonValue> args);
    /// The same, and hands what it returned to `done` (on the worker thread). With `progress`
    /// the function gets a progress function as its last argument.
    void Invoke(std::vector<std::string> path, std::vector<JsonValue> args, InvokeDone done,
                InvokeProgress progress = {});
    /// Invoked functions that have not returned yet.
    int Invoking() const {
        return invoking_.load();
    }
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
    /// Posts an ask the screen answers every time something happens ("accepted") and once
    /// more when it is over ("not accepted").
    void PostStream(Ask ask, StreamFn on_answer);
    /// Whether the calling function runs because the user pressed something on the plugin's
    /// page (an action), not because the plugin was opened or resumed.
    bool UserTriggered(lua_State* L);
    /// The id of the calling coroutine, 0 outside the plugin's own functions.
    std::uint64_t CoroutineOf(lua_State* L);
    /// Suspends the calling coroutine until Wake. Does not return.
    int Park(lua_State* L);
    void Wake(std::uint64_t coroutine, const Outcome& outcome);
    WebVisit& Web() {
        return web_;
    }
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
    /// A file den.http.request and den.files.read take: a host file lent for this call
    /// ("sync:<token>", `lent` true) or one of the plugin's folder.
    Status ResolveFile(std::string_view script_path, std::filesystem::path& out, bool& lent) const;
    /// The server the user confirmed for http.user_host, "scheme://host[:port]", or empty.
    std::string UserServer() const;
    bool SetUserServer(const std::string& origin);
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
        bool from_user = false; // started by something the user pressed (plugin.actions.*)
        InvokeDone done;        // Invoke: who waits for the results
        int progress_key = 0;   // Invoke: its entry in progress_fns_
    };

    void ThreadMain();
    /// Needs mutex_: whether the ask is one that is answered many times; `last` ends it.
    bool IsStream(std::uint64_t ask_id, bool last);
    /// Hands an answer to the stream's function, on the worker thread.
    void AnswerStream(std::uint64_t ask_id, bool accepted, const std::string& value);
    void Post(std::function<void()> task);
    void Boot();
    void StartCall(const std::vector<std::string>& path, const std::vector<JsonValue>& args,
                   InvokeDone done = {}, InvokeProgress progress = {});
    /// Hands an invoked coroutine's `results` values (or its failure) to its waiter.
    void Answer(Coroutine& co, int results, const std::string& failure);
    static int ProgressThunk(lua_State* L);
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
    std::map<std::uint64_t, StreamFn> streams_;
    WebVisit web_;
    JsonValue storage_;
    std::map<std::string, std::filesystem::path> inbox_tokens_;
    std::map<int, InvokeProgress> progress_fns_;
    int next_progress_ = 1;
    int failures_ = 0;
    std::atomic<int> invoking_{0};

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
    std::vector<std::uint64_t> stream_asks_;
    std::string error_;
    bool stopped_ = false;
    std::deque<std::string> log_;
    std::string user_server_;
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
void RegisterWeb(lua_State* L, Runner& runner);     // web
void RegisterDownloads(lua_State* L, Runner& runner); // downloads, for downloads.setup
void RegisterRequest(lua_State* L, Runner& runner); // http.request (after RegisterNetwork)
void RegisterSecrets(lua_State* L, Runner& runner); // secrets
void RegisterSync(lua_State* L, Runner& runner);    // sync, for the sync permission
void RegisterTune(lua_State* L, Runner& runner);    // device.info, game, steam (after RegisterCore)
void RegisterAchievements(lua_State* L, Runner& runner); // achievements

/// The credential store's name of a plugin's secret.
std::string SecretName(const std::string& plugin, const std::string& key);
/// Removes every secret the plugin kept (it is removed with its data).
void RemoveSecrets(const std::string& plugin, const std::filesystem::path& data_dir);
/// den.secrets.set and remove for the host's own calls (den.achievements keeps its token so).
Status KeepSecret(Runner& runner, const std::string& key, const JsonValue& value);
void DropSecret(Runner& runner, const std::string& key);

/// Sets `name` in the table on top of the stack to the function, with the runner as upvalue.
void SetFunction(lua_State* L, Runner& runner, const char* name, lua_CFunction function);
/// The option table argument's string field, "" when absent; raises when it is not text.
std::string FieldText(lua_State* L, int table, const char* key);
/// The option table argument's list of texts in lower case, 32 at most.
std::vector<std::string> FieldList(lua_State* L, int table, const char* key);
/// A file's extension in lower case without the dot.
std::string ExtensionOf(const std::filesystem::path& file);

} // namespace den::plugins::feature
