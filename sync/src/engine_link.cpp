// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.sync of the provider plugins, answered by the engine.

#include "den/base/files.h"
#include "den/plugins/sync_link.h"
#include "engine_impl.h"

namespace den::sync {

namespace {

namespace fs = std::filesystem;

class ProviderLink final : public plugins::SyncLink {
public:
    explicit ProviderLink(std::weak_ptr<EngineImpl> engine) : engine_(std::move(engine)) {}

    // {active, running, last_sync, done, total, uploaded, downloaded, conflicts, waiting,
    //  errors = {...}, error?, current? = {path, fraction}, folder?}
    JsonValue StatusOf(const std::string& provider) override {
        const auto engine = engine_.lock();
        if (!engine) {
            return JsonObject();
        }
        const bool active = engine->Active() == provider;
        const SyncState state = active ? engine->State() : SyncState{};
        JsonValue errors = JsonArray();
        for (const std::string& error : state.errors) {
            errors.items.push_back(JsonString(error));
        }
        JsonValue out = JsonObject({
            {"active", JsonBool(active)},
            {"running", JsonBool(state.running)},
            {"last_sync", JsonNumber(static_cast<double>(state.last_sync))},
            {"done", JsonNumber(state.done)},
            {"total", JsonNumber(state.total)},
            {"uploaded", JsonNumber(state.uploaded)},
            {"downloaded", JsonNumber(state.downloaded)},
            {"conflicts", JsonNumber(active ? static_cast<double>(engine->Conflicts().size()) : 0)},
            {"waiting", JsonNumber(state.waiting)},
            {"errors", std::move(errors)},
        });
        if (!state.errors.empty()) {
            out.members.emplace_back("error", JsonString(state.errors.front()));
        }
        if (!state.current.empty()) {
            out.members.emplace_back("current", JsonObject({{"path", JsonString(state.current)},
                                                            {"fraction", JsonNumber(state.fraction)}}));
        }
        if (const fs::path folder = engine->Folder(provider); !folder.empty()) {
            out.members.emplace_back("folder", JsonString(PathToUtf8(folder)));
        }
        return out;
    }

    Status SyncNow(const std::string& provider) override {
        const auto engine = engine_.lock();
        if (!engine) {
            return Status::Fail("sync is not running");
        }
        if (engine->Active() != provider) {
            return Status::Fail("This plugin is not the active sync provider; choose it under Settings, Sync.");
        }
        engine->SyncNow();
        return {};
    }

    std::vector<std::string> EnabledCategories(const std::string& provider) override {
        const auto engine = engine_.lock();
        return engine ? engine->Categories(provider) : std::vector<std::string>();
    }

    fs::path Folder(const std::string& provider) override {
        const auto engine = engine_.lock();
        return engine ? engine->Folder(provider) : fs::path();
    }

    Status CheckFolder(const fs::path& folder) override {
        const auto engine = engine_.lock();
        return engine ? engine->CheckFolder(folder) : Status::Fail("sync is not running");
    }

    Status UseFolder(const std::string& provider, const fs::path& folder) override {
        const auto engine = engine_.lock();
        return engine ? engine->UseFolder(provider, folder) : Status::Fail("sync is not running");
    }

    fs::path Home() override {
        const auto engine = engine_.lock();
        return engine ? engine->Home() : fs::path();
    }

    Status DownloadGame(const std::string& provider, const std::vector<std::string>& paths) override {
        const auto engine = engine_.lock();
        return engine ? engine->DownloadGame(provider, paths, nullptr) : Status::Fail("sync is not running");
    }

private:
    std::weak_ptr<EngineImpl> engine_;
};

} // namespace

std::shared_ptr<plugins::SyncLink> EngineImpl::Link() {
    std::lock_guard lock(mutex_);
    if (!link_) {
        link_ = std::make_shared<ProviderLink>(weak_from_this());
    }
    return link_;
}

} // namespace den::sync
