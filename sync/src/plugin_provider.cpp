// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A plugin's plugin.provider, called on its script through the feature host. The engine's
// files are lent to the script for the one call that needs them.

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "den/base/files.h"
#include "den/plugins/feature_host.h"
#include "den/sync/names.h"
#include "den/sync/provider.h"

namespace den::sync {

namespace {

namespace fs = std::filesystem;
using Values = Result<std::vector<JsonValue>>;

constexpr auto CALL_LIMIT = std::chrono::hours(2); // a game upload may take that long

struct Waiting {
    std::mutex mutex;
    std::condition_variable done;
    std::optional<Values> result;
};

// A call's progress reaches the caller only while it waits: a call given up (cancelled, or
// past its limit) goes on in the plugin, and its caller may be gone by its next report.
struct ProgressGuard {
    std::mutex mutex;
    Progress progress;
    bool live = true;
};

class PluginProvider final : public Provider {
public:
    PluginProvider(plugins::FeatureHost* host, std::string id, std::set<std::string> pc,
                   const std::atomic<bool>* cancel)
        : host_(host), id_(std::move(id)), pc_(std::move(pc)), cancel_(cancel) {}

    std::string Id() const override {
        return id_;
    }

    Result<std::vector<RemoteFile>> List(const std::string& prefix) override {
        Values values = Call("list", {prefix.empty() ? JsonValue() : JsonString(prefix)});
        if (!values) {
            return values.status();
        }
        std::vector<RemoteFile> out;
        if (values->empty() || values->front().kind != JsonValue::Kind::Array) {
            return Status::Fail("the plugin's list is not a list of files");
        }
        for (const JsonValue& item : values->front().items) {
            RemoteFile file;
            file.path = item.String("path");
            if (!IsPortablePath(file.path) || Refused(file.path)) {
                continue;
            }
            if (const JsonValue* size = item.Find("size"); size && size->kind == JsonValue::Kind::Number) {
                file.size = static_cast<std::uint64_t>(size->number);
            }
            if (const JsonValue* modified = item.Find("modified"); modified && modified->kind == JsonValue::Kind::Number) {
                file.modified = static_cast<std::int64_t>(modified->number);
            }
            file.rev = Text(item.Find("rev"));
            out.push_back(std::move(file));
        }
        return out;
    }

    Status Get(const std::string& path, const fs::path& local, const Progress& progress) override {
        if (Refused(path)) {
            return Status::Fail("this provider is never given that file");
        }
        const std::string token = host_->LendFile(id_, local);
        Values values = Call("get", {JsonString(path), JsonString(token)}, progress ? progress : [](double, double) {});
        host_->ReturnFile(token);
        return values ? Status() : values.status();
    }

    Result<std::string> Put(const std::string& path, const fs::path& local, const std::string& rev,
                            std::int64_t modified, const Progress& progress) override {
        if (Refused(path)) {
            return Status::Fail("PC consoles never sync");
        }
        const std::string token = host_->LendFile(id_, local);
        Values values = Call("put", {JsonString(path), JsonString(token), rev.empty() ? JsonValue() : JsonString(rev),
                                     JsonNumber(static_cast<double>(modified))},
                             progress ? progress : [](double, double) {});
        host_->ReturnFile(token);
        if (!values) {
            return values.status();
        }
        return values->empty() ? std::string() : Text(&values->front());
    }

    Status Delete(const std::string& path) override {
        if (Refused(path)) {
            return Status::Fail("this provider is never given that file");
        }
        Values values = Call("delete", {JsonString(path)});
        return values ? Status() : values.status();
    }

    JsonValue Info() override {
        Values values = Call("status", {});
        return values && !values->empty() && values->front().kind == JsonValue::Kind::Object ? values->front()
                                                                                              : JsonObject();
    }

private:
    bool Refused(const std::string& path) const {
        return IsPcPath(path, pc_);
    }

    static std::string Text(const JsonValue* value) {
        if (value == nullptr) {
            return {};
        }
        if (value->kind == JsonValue::Kind::Number) {
            return std::to_string(static_cast<long long>(value->number));
        }
        return value->kind == JsonValue::Kind::String ? value->string : std::string();
    }

    Values Call(const char* function, std::vector<JsonValue> args, Progress progress = {}) {
        auto waiting = std::make_shared<Waiting>();
        auto guard = std::make_shared<ProgressGuard>();
        guard->progress = std::move(progress);
        struct Closed {
            ProgressGuard& guard;
            ~Closed() {
                std::lock_guard lock(guard.mutex);
                guard.live = false;
            }
        } closed{*guard};
        host_->Invoke(
            id_, {"provider", function}, std::move(args),
            [waiting](Values values) {
                std::lock_guard lock(waiting->mutex);
                waiting->result = std::move(values);
                waiting->done.notify_all();
            },
            [guard](double done, double total) {
                std::lock_guard lock(guard->mutex);
                if (guard->live && guard->progress) {
                    guard->progress(done, total);
                }
            });
        std::unique_lock lock(waiting->mutex);
        const auto until = std::chrono::steady_clock::now() + CALL_LIMIT;
        while (!waiting->result) {
            if ((cancel_ != nullptr && cancel_->load()) || std::chrono::steady_clock::now() >= until) {
                return Status::Fail("cancelled");
            }
            waiting->done.wait_for(lock, std::chrono::milliseconds(200));
        }
        return std::move(*waiting->result);
    }

    plugins::FeatureHost* host_;
    std::string id_;
    std::set<std::string> pc_;
    const std::atomic<bool>* cancel_;
};

} // namespace

std::unique_ptr<Provider> CreatePluginProvider(plugins::FeatureHost* host, std::string id, std::set<std::string> pc,
                                               const std::atomic<bool>* cancel) {
    return std::make_unique<PluginProvider>(host, std::move(id), std::move(pc), cancel);
}

} // namespace den::sync
