// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <fcntl.h>
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

// The app's ends of a child's standard input and output.
class PipeStream final : public Stream {
public:
    PipeStream(int read, int write) : read_(read), write_(write) {}
    ~PipeStream() override {
        Close();
        ::close(read_);
    }

    bool Read(void* data, std::size_t size) override {
        auto* out = static_cast<char*>(data);
        while (size > 0) {
            const ssize_t got = ::read(read_, out, size);
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                return false;
            }
            out += got;
            size -= static_cast<std::size_t>(got);
        }
        return true;
    }

    bool Write(const void* data, std::size_t size) override {
        std::lock_guard lock(write_lock_);
        const auto* in = static_cast<const char*>(data);
        while (size > 0 && write_ >= 0) {
            const ssize_t sent = ::write(write_, in, size);
            if (sent < 0 && errno == EINTR) {
                continue;
            }
            if (sent <= 0) {
                return false;
            }
            in += sent;
            size -= static_cast<std::size_t>(sent);
        }
        return size == 0;
    }

    void Close() override {
        std::lock_guard lock(write_lock_);
        if (write_ >= 0) {
            ::close(write_); // the child reads the end of its input
            write_ = -1;
        }
    }

private:
    int read_;
    int write_;
    std::mutex write_lock_;
};

class PosixChild final : public Child {
public:
    PosixChild(pid_t pid, std::unique_ptr<PipeStream> channel) : pid_(pid), channel_(std::move(channel)) {}
    ~PosixChild() override {
        Kill();
    }

    Stream* Channel() override {
        return channel_.get();
    }

    std::optional<int> Poll() override {
        return Reap(WNOHANG);
    }
    bool RequestClose() override {
        return !Poll() && kill(-pid_, SIGTERM) == 0;
    }
    bool ConfirmClose() override {
        return false;
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
    std::unique_ptr<PipeStream> channel_;
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

    // The channel: the child's input and output, and its errors in the log file.
    int to_child[2] = {-1, -1}, from_child[2] = {-1, -1};
    int log = -1;
    if (desc.channel) {
        if (pipe(to_child) != 0 || pipe(from_child) != 0) {
            return Status::Fail("could not start " + exe + ": " + std::strerror(errno));
        }
        fcntl(to_child[1], F_SETFD, FD_CLOEXEC);
        fcntl(from_child[0], F_SETFD, FD_CLOEXEC);
        log = desc.log_file.empty() ? open("/dev/null", O_WRONLY)
                                    : open(PathToUtf8(desc.log_file).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    // SIGPIPE would end the app when an engine goes away mid-write.
    signal(SIGPIPE, SIG_IGN);

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
        if (desc.channel) {
            dup2(to_child[0], 0);
            dup2(from_child[1], 1);
            if (log >= 0) {
                dup2(log, 2);
            }
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
    std::unique_ptr<PipeStream> channel;
    if (desc.channel) {
        ::close(to_child[0]);
        ::close(from_child[1]);
        if (log >= 0) {
            ::close(log);
        }
        channel = std::make_unique<PipeStream>(from_child[0], to_child[1]);
    }
    return std::unique_ptr<Child>(new PosixChild(pid, std::move(channel)));
}

} // namespace den::pal
