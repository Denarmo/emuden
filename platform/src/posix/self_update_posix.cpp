// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Linux and the other desktop systems: an AppImage replaces its own file.

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "den/base/files.h"
#include "den/platform/pal.h"
#include "den/platform/self_update.h"

namespace den::pal::self_update {

namespace {

bool Alive(int pid) {
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

// Runs argv in a grandchild that init adopts, so it outlives the app and leaves no zombie.
Status Spawn(const std::vector<std::string>& argv) {
    std::vector<char*> args;
    for (const std::string& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) {
        return Status::Fail(std::string("Could not start the program: ") + std::strerror(errno));
    }
    if (child == 0) {
        setsid();
        if (fork() == 0) {
            const int null = open("/dev/null", O_RDWR);
            if (null >= 0) {
                dup2(null, 0);
                dup2(null, 1);
                dup2(null, 2);
            }
            execv(args[0], args.data());
            _exit(127);
        }
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return {};
}

} // namespace

Target Detect() {
    Target target;
    // The AppImage runtime names its own file; anything else (a build folder, a package of the
    // distribution) is not replaced by the app.
    const std::string appimage = GetEnv("APPIMAGE");
    const std::filesystem::path file = PathFromUtf8(appimage);
    if (appimage.empty() || !IsRegularFile(file)) {
        target.reason = "Only the AppImage can update itself.";
        return target;
    }
    if (access(file.parent_path().c_str(), W_OK) != 0) {
        target.reason = "EmuDEN may not write to the AppImage's folder.";
        return target;
    }
    target.method = Method::AppImage;
    target.location = file;
    return target;
}

int ProcessId() {
    return static_cast<int>(getpid());
}

bool WaitForExit(int pid, int timeout_ms) {
    for (int waited = 0; Alive(pid); waited += 100) {
        if (waited >= timeout_ms) {
            return false;
        }
        usleep(100 * 1000);
    }
    return true;
}

Status StartDetached(const std::filesystem::path& executable, const std::vector<std::string>& args) {
    std::vector<std::string> argv{executable.string()};
    argv.insert(argv.end(), args.begin(), args.end());
    return Spawn(argv);
}

Status StartAfterExit(const std::filesystem::path& executable) {
    // A shell outside the AppImage waits for this process, then starts the new file.
    return Spawn({"/bin/sh", "-c", "while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; exec \"$2\"", "sh",
                  std::to_string(getpid()), executable.string()});
}

Status MakeExecutable(const std::filesystem::path& file) {
    struct stat info {};
    if (stat(file.c_str(), &info) != 0) {
        return Status::Fail(std::string("Could not read the file: ") + std::strerror(errno));
    }
    mode_t mode = info.st_mode | S_IXUSR;
    mode |= (info.st_mode & S_IRGRP) ? S_IXGRP : 0;
    mode |= (info.st_mode & S_IROTH) ? S_IXOTH : 0;
    if (chmod(file.c_str(), mode) != 0) {
        return Status::Fail(std::string("Could not make the file executable: ") + std::strerror(errno));
    }
    return {};
}

Status InstallPackage(const std::filesystem::path&) {
    return Status::Fail("Only Android installs packages.");
}

} // namespace den::pal::self_update
