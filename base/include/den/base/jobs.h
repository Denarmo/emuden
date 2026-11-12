// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "den/base/result.h"

namespace den {

enum class JobState { Queued, Running, Done, Failed, Cancelled };

/// Shared between the worker that runs a job and whoever watches it. All members are safe to
/// use from any thread.
class Job {
public:
    explicit Job(std::string name) : name_(std::move(name)) {}

    const std::string& Name() const {
        return name_;
    }
    JobState State() const {
        return state_.load();
    }
    bool Finished() const {
        const JobState s = State();
        return s == JobState::Done || s == JobState::Failed || s == JobState::Cancelled;
    }
    /// 0..1, or negative when the amount of work is unknown.
    float Progress() const {
        return progress_.load();
    }
    std::string Message() const;
    /// The failure text once the state is Failed.
    std::string Error() const;

    void Cancel() {
        cancel_.store(true);
    }
    bool CancelRequested() const {
        return cancel_.load();
    }

    // For the running job.
    void SetProgress(float fraction) {
        progress_.store(fraction);
    }
    void SetMessage(std::string text);

private:
    friend class Jobs;
    std::string name_;
    std::atomic<JobState> state_{JobState::Queued};
    std::atomic<float> progress_{-1.0f};
    std::atomic<bool> cancel_{false};
    mutable std::mutex mutex_;
    std::string message_;
    std::string error_;
};

using JobPtr = std::shared_ptr<Job>;
using JobFn = std::function<Status(Job&)>;

/// A small thread pool. Work that touches files or the network runs here, never on the main
/// thread. `on_done` and everything given to PostToMain run inside DrainMain, which the app
/// calls once per frame.
class Jobs {
public:
    explicit Jobs(int threads = 0);
    ~Jobs();
    Jobs(const Jobs&) = delete;
    Jobs& operator=(const Jobs&) = delete;

    JobPtr Run(std::string name, JobFn work, std::function<void(Job&)> on_done = {});
    void PostToMain(std::function<void()> fn);
    void DrainMain();
    /// Blocks until `job` has finished, running main-thread callbacks meanwhile (tests, shutdown).
    void Wait(const JobPtr& job);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace den
