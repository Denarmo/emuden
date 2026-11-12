// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "den/base/files.h"
#include "den/platform/child.h"
#include "den/platform/pal.h"

namespace den::pal {

namespace {

class PosixChild final : public Child {
public:
    explicit PosixChild(pid_t pid) : pid_(pid) {}
    ~PosixChild() override {
        Kill();
    }

    std::optional<int> Poll() override {
        return Reap(WNOHANG);
    }
    void RequestClose() override {
        if (!Poll()) {
            kill(-pid_, SIGTERM);
        }
    }
    void Kill() override {
        if (!Poll()) {
            kill(-pid_, SIGKILL);
            Reap(0);
        }
    }

private:
    std::optional<int> Reap(int flags) {
        if (!exit_) {
            int status = 0;
            const pid_t got = waitpid(pid_, &status, flags);
            if (got == pid_) {
                exit_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            } else if (got < 0 && errno != EINTR) {
                exit_ = -1;
            }
        }
        return exit_;
    }

    pid_t pid_;
    std::optional<int> exit_;
};

} // namespace

Result<std::unique_ptr<Child>> StartChild(const ChildDesc& desc) {
    if (!GetCaps().child_processes) {
        return Status::Fail("this device cannot start child processes");
    }
    const std::string exe = PathToUtf8(desc.executable);
    const std::string cwd =
        PathToUtf8(desc.working_dir.empty() ? desc.executable.parent_path() : desc.working_dir);
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(exe.c_str()));
    for (const std::string& arg : desc.args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t parent = getpid();
    const pid_t pid = fork();
    if (pid < 0) {
        return Status::Fail("could not start " + exe + ": " + std::strerror(errno));
    }
    if (pid == 0) {
        // Its own process group, so a stop reaches what it starts; it still dies with the app.
        setpgid(0, 0);
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif
        if (getppid() != parent) {
            _exit(127);
        }
        for (const auto& [name, value] : desc.env) {
            setenv(name.c_str(), value.c_str(), 1);
        }
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            _exit(127);
        }
        execv(exe.c_str(), argv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    return std::unique_ptr<Child>(new PosixChild(pid));
}

} // namespace den::pal
