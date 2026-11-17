// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The app updater: a check of GitHub's releases once a day (and when asked), a toast when a
// newer EmuDEN is out, and Download and install for this system (den/update,
// den/platform/self_update.h).

#include <chrono>
#include <format>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/system/info.h"
#include "den/system/settings.h"
#include "den/update/portable.h"
#include "den/update/updater.h"
#include "den/update/version.h"
#include "update_state.h"

namespace den::app {

// Device-only: whether to look, and when it last did, belong to this copy of the app.
const system::Key<bool> AUTO_CHECK_KEY{
    "update.auto_check", true, {.label = "Check automatically", .section = "Updates", .device_only = true}};
const system::Key<bool> BETA_KEY{"update.beta", false, {.label = "Beta updates", .section = "Updates", .device_only = true}};
const system::Key<double> LAST_CHECK_KEY{"update.last_check", 0.0, {.section = "Updates", .device_only = true}};

namespace {

namespace fs = std::filesystem;
using pal::self_update::Method;

constexpr double POLL_SECONDS = 60;     // how often the frame hook looks whether a check is due
constexpr std::int64_t MANUAL_GAP = 60; // a check asked for within this of the last reuses it

UpdateState g_state;
bool g_toast_pending = false;
double g_next_poll = 0;

std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

fs::path WorkDir() {
    return Svc().data_dir / "updates";
}

fs::path CacheFile() {
    return WorkDir() / "latest.json";
}

// Only a development build honours them, and the address only on this device.
update::TestOverrides TestOverrides() {
    const update::TestOverrides raw = RawTestOverrides();
    return update::AllowedTestOverrides(system::ReleaseVersion(), raw.version, raw.url);
}

update::Source MakeSource() {
    update::Source source;
    source.url = TestOverrides().url;
    source.allow_loopback = !source.url.empty();
    return source;
}

void Adopt(const std::optional<update::Release>& release) {
    UpdateState& state = Updates();
    state.release = release;
    state.newer = release && update::IsNewer(release->version, state.running);
}

bool Showing(const char* route) {
    const ui::Page* top = Svc().stage->Top();
    return top && top->Route() == route;
}

void RebuildIfShown() {
    if (Showing("settings") || Showing("update")) {
        RebuildTop();
    }
}

// The toast waits while a game or the first setup is on screen.
void ToastWhenFree() {
    if (!g_toast_pending || !Updates().newer) {
        return;
    }
    const ui::Page* top = Svc().stage->Top();
    if (top && (top->Route().starts_with("player") || top->Route() == "setup")) {
        return;
    }
    g_toast_pending = false;
    Svc().stage->Toast(TrF("EmuDEN {} is available", Updates().release->version), 5.0f);
}

void Startup() {
    UpdateState& state = Updates();
    const std::string test_version = TestOverrides().version;
    state.running = !test_version.empty() ? test_version : system::ReleaseVersion();
    if (!test_version.empty()) {
        log::Info("update", "Test only: running as version {}", test_version);
    }
    const update::TestOverrides raw = RawTestOverrides();
    if ((!raw.version.empty() && test_version.empty()) || (!raw.url.empty() && TestOverrides().url.empty())) {
        log::Info("update", "Test overrides ignored: a release build, or an address not on this device");
    }
    state.target = pal::self_update::Detect();
    state.asset = update::AssetNameFor(pal::GetCaps().platform_key);
    state.checked_at = static_cast<std::int64_t>(system::Store().Get(LAST_CHECK_KEY));

    // What the helper did while the app was closed (Windows).
    if (state.target.method == Method::WindowsPortable) {
        const update::PortableLayout layout = update::LayoutFor(state.target.location);
        if (const auto result = update::TakeHelperResult(layout)) {
            Svc().stage->Toast(result->ok ? TrF("Updated to EmuDEN {}", result->version)
                                          : "The update failed: " + result->error,
                               6.0f);
            log::Info("update", "helper: {} {} {}", result->ok ? "ok" : "failed", result->version, result->error);
        }
        update::CleanUpAfterUpdate(layout);
    }
    if (!UpdatesOffered()) {
        return;
    }
    if (const auto cached = ReadTextFile(CacheFile())) {
        Adopt(update::PickRelease(*cached, system::Store().Get(BETA_KEY)));
    }
    if (!state.newer) {
        // An APK handed to the installer earlier is not needed once it is installed.
        std::error_code ec;
        fs::remove(WorkDir() / state.asset, ec);
    }
}

void Frame() {
    ToastWhenFree();
    const double now = pal::NowSeconds();
    if (now < g_next_poll) {
        return;
    }
    g_next_poll = now + POLL_SECONDS;
    UpdateState& state = Updates();
    if (!UpdatesOffered() || !system::Store().Get(AUTO_CHECK_KEY) || state.check_job || state.install_job ||
        !update::CheckDue(state.checked_at, Now())) {
        return;
    }
    CheckForUpdates(true);
}

// The last step once the download (and for Windows the unpacking) is done: the installer, or
// the restart.
void Finish(const update::Release& release) {
    UpdateState& state = Updates();
    const std::string version = release.version;
    switch (state.target.method) {
    case Method::AndroidApk:
        if (const Status handed = pal::self_update::InstallPackage(WorkDir() / state.asset); !handed) {
            Svc().stage->Toast(handed.error, 6.0f);
        }
        break;
    case Method::AppImage:
        state.restart_pending = true;
        Svc().stage->Confirm(
            TrF("EmuDEN {} is installed", version), "It starts the next time EmuDEN opens.", "Restart now",
            [] {
                if (const Status started = pal::self_update::StartAfterExit(Updates().target.location); !started) {
                    Svc().stage->Toast(started.error, 6.0f);
                    return;
                }
                Svc().quit = true;
            },
            "Later", true);
        break;
    case Method::WindowsPortable:
        state.restart_pending = true;
        Svc().stage->Confirm(
            TrF("Restart to update to {}?", version),
            "EmuDEN closes, puts the new version in place and starts again. Your games, saves and settings stay.",
            "Restart now",
            [version] {
                const update::PortableLayout layout = update::LayoutFor(Updates().target.location);
                const Status started = update::StartHelper(layout, layout.install / "emuden.exe",
                                                           pal::self_update::ProcessId(), version);
                if (!started) {
                    Svc().stage->Toast(started.error, 6.0f);
                    return;
                }
                Svc().quit = true;
            },
            "Later", true);
        break;
    case Method::ReleasePage:
        break;
    }
    RebuildIfShown();
}

void Installed(Job& job, const update::Release& release) {
    Updates().install_job.reset();
    if (job.State() != JobState::Done) {
        if (job.State() == JobState::Failed) {
            log::Info("update", "install failed: {}", job.Error());
            Svc().stage->Toast(job.Error(), 6.0f);
        }
        RebuildIfShown();
        return;
    }
    Finish(release);
}

Status InstallWork(Job& job, const update::Release& release, const pal::self_update::Target& target,
                   const std::string& asset, const fs::path& work_dir) {
    const update::Source source = MakeSource();
    job.SetMessage("Downloading");
    switch (target.method) {
    case Method::AndroidApk:
        return update::DownloadAsset(source, release, asset, work_dir / asset, &job);
    case Method::AppImage: {
        const fs::path file = target.location.parent_path() / ("." + PathToUtf8(target.location.filename()) + ".new");
        if (const Status got = update::DownloadAsset(source, release, asset, file, &job); !got) {
            return got;
        }
        return update::ReplaceAppImage(file, target.location);
    }
    case Method::WindowsPortable: {
        const update::PortableLayout layout = update::LayoutFor(target.location);
        update::CleanUpAfterUpdate(layout);
        const fs::path zip = layout.download / asset;
        if (const Status got = update::DownloadAsset(source, release, asset, zip, &job); !got) {
            return got;
        }
        job.SetMessage("Unpacking");
        return update::StageZip(zip, layout.staged, &job);
    }
    case Method::ReleasePage:
        break;
    }
    return Status::Fail("This copy of EmuDEN cannot update itself.");
}

} // namespace

UpdateState& Updates() {
    return g_state;
}

bool UpdatesOffered() {
    return update::CanUpdateFrom(Updates().running);
}

void CheckForUpdates(bool automatic) {
    UpdateState& state = Updates();
    if (!UpdatesOffered() || (state.check_job && !state.check_job->Finished())) {
        return;
    }
    if (!automatic && state.check_error.empty() && state.checked_at > 0 && Now() - state.checked_at < MANUAL_GAP &&
        Now() >= state.checked_at) {
        Svc().stage->Toast(state.newer ? TrF("EmuDEN {} is available", state.release->version)
                                       : "EmuDEN is up to date");
        return;
    }
    const bool beta = system::Store().Get(BETA_KEY);
    const update::Source source = MakeSource();
    auto result = std::make_shared<std::optional<update::CheckResult>>();
    state.check_error.clear();
    state.check_job = Svc().jobs->Run(
        "Check for updates",
        [source, beta, result](Job&) {
            auto fetched = update::FetchLatest(source, beta);
            if (!fetched) {
                return fetched.status();
            }
            *result = std::move(fetched.value());
            return Status{};
        },
        [automatic, result](Job& job) {
            UpdateState& state = Updates();
            state.check_job.reset();
            if (job.State() != JobState::Done || !*result) {
                state.check_error = job.Error();
                log::Info("update", "check failed: {}", state.check_error);
                if (!automatic) {
                    Svc().stage->Toast(state.check_error, 5.0f);
                }
                RebuildIfShown();
                return;
            }
            state.checked_at = Now();
            system::Store().Set(LAST_CHECK_KEY, static_cast<double>(state.checked_at));
            std::error_code ec;
            fs::create_directories(WorkDir(), ec);
            WriteFileAtomic(CacheFile(), (*result)->body);
            Adopt((*result)->release);
            log::Info("update", "checked: newest {}, running {}",
                      state.release ? state.release->version : std::string("none"), state.running);
            if (automatic) {
                g_toast_pending = state.newer;
            } else {
                Svc().stage->Toast(state.newer ? TrF("EmuDEN {} is available", state.release->version)
                                               : "EmuDEN is up to date");
            }
            RebuildIfShown();
        });
}

void DownloadAndInstall() {
    UpdateState& state = Updates();
    if (!state.newer || !state.release) {
        return;
    }
    if (state.target.method == Method::ReleasePage || state.asset.empty() || !state.release->Find(state.asset)) {
        if (const Status opened = pal::OpenUrl(!state.release->page.empty() ? state.release->page
                                                                            : update::ReleasesPage(update::UpdateRepo()));
            !opened) {
            Svc().stage->Toast(opened.error, 5.0f);
        }
        return;
    }
    if (state.install_job && !state.install_job->Finished()) {
        return;
    }
    if (state.restart_pending && state.target.method != Method::AndroidApk) {
        Finish(*state.release); // installed or staged already: only the restart is left
        return;
    }
    const update::Release release = *state.release;
    state.install_job = Svc().jobs->Run(
        "Update EmuDEN",
        [release, target = state.target, asset = state.asset, dir = WorkDir()](Job& job) {
            return InstallWork(job, release, target, asset, dir);
        },
        [release](Job& job) { Installed(job, release); });
    RebuildIfShown();
}

std::string InstallLabel() {
    const UpdateState& state = Updates();
    if (state.target.method == Method::ReleasePage || state.asset.empty() ||
        (state.release && !state.release->Find(state.asset))) {
        return "Open the release page";
    }
    return state.restart_pending && state.target.method != Method::AndroidApk ? "Restart to finish the update"
                                                                              : "Download and install";
}

std::string CheckStatusText() {
    const UpdateState& state = Updates();
    if (!UpdatesOffered()) {
        return "Development build";
    }
    if (state.check_job) {
        return "Checking";
    }
    if (!state.check_error.empty()) {
        return "Could not check";
    }
    if (state.newer) {
        return TrF("EmuDEN {} is available", state.release->version);
    }
    return state.checked_at > 0 ? "Up to date" : "Not checked yet";
}

std::string LastCheckText() {
    const UpdateState& state = Updates();
    if (!UpdatesOffered()) {
        return "A build without a release version is never offered updates";
    }
    if (!state.check_error.empty()) {
        return state.check_error;
    }
    if (state.checked_at <= 0) {
        return "Asks GitHub for the newest release";
    }
    const std::int64_t ago = std::max<std::int64_t>(0, Now() - state.checked_at);
    if (ago < 60) {
        return "Checked just now";
    }
    if (ago < 3600) {
        return TrF("Checked {} min ago", ago / 60);
    }
    if (ago < 86400) {
        return TrF("Checked {} h ago", ago / 3600);
    }
    return TrF("Checked {} days ago", ago / 86400);
}

DEN_REGISTER_STARTUP(Startup);
DEN_REGISTER_FRAME(Frame);

} // namespace den::app
