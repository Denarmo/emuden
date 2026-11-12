// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>
#include <vector>

#include "den/base/jobs.h"

namespace den {

std::string Job::Message() const {
    std::lock_guard lock(mutex_);
    return message_;
}

std::string Job::Error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

void Job::SetMessage(std::string text) {
    std::lock_guard lock(mutex_);
    message_ = std::move(text);
}

struct Jobs::Impl {
    struct Item {
        JobPtr job;
        JobFn work;
        std::function<void(Job&)> on_done;
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Item> queue;
    std::mutex main_mutex;
    std::deque<std::function<void()>> main_queue;
    std::vector<std::thread> threads;
    bool stopping = false;

    void Worker() {
        for (;;) {
            Item item;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !queue.empty(); });
                if (queue.empty()) {
                    return;
                }
                item = std::move(queue.front());
                queue.pop_front();
            }
            Job& job = *item.job;
            Status status;
            if (job.CancelRequested()) {
                status = Status::Fail("cancelled");
            } else {
                job.state_.store(JobState::Running);
                status = item.work(job);
            }
            {
                std::lock_guard lock(job.mutex_);
                job.error_ = status.error;
            }
            const JobState end = status.ok                 ? JobState::Done
                                 : job.CancelRequested() ? JobState::Cancelled
                                                         : JobState::Failed;
            if (item.on_done) {
                std::lock_guard lock(main_mutex);
                main_queue.push_back([job = item.job, end, done = std::move(item.on_done)] {
                    job->state_.store(end);
                    done(*job);
                });
            } else {
                job.state_.store(end);
            }
        }
    }
};

Jobs::Jobs(int threads) : impl_(std::make_unique<Impl>()) {
    if (threads <= 0) {
        threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 2, 8);
    }
    for (int i = 0; i < threads; ++i) {
        impl_->threads.emplace_back([this] { impl_->Worker(); });
    }
}

Jobs::~Jobs() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
        for (auto& item : impl_->queue) {
            item.job->Cancel();
        }
    }
    impl_->wake.notify_all();
    for (auto& thread : impl_->threads) {
        thread.join();
    }
}

JobPtr Jobs::Run(std::string name, JobFn work, std::function<void(Job&)> on_done) {
    auto job = std::make_shared<Job>(std::move(name));
    {
        std::lock_guard lock(impl_->mutex);
        impl_->queue.push_back({job, std::move(work), std::move(on_done)});
    }
    impl_->wake.notify_one();
    return job;
}

void Jobs::PostToMain(std::function<void()> fn) {
    std::lock_guard lock(impl_->main_mutex);
    impl_->main_queue.push_back(std::move(fn));
}

void Jobs::DrainMain() {
    std::deque<std::function<void()>> pending;
    {
        std::lock_guard lock(impl_->main_mutex);
        pending.swap(impl_->main_queue);
    }
    for (auto& fn : pending) {
        fn();
    }
}

void Jobs::Wait(const JobPtr& job) {
    while (!job->Finished()) {
        DrainMain();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    DrainMain();
}

} // namespace den
