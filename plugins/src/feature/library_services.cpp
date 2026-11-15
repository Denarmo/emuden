// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.patch and den.library over the app's library: the patching module reads patches,
// the library's hacks find base games and add the patched copies.

#include <algorithm>
#include <mutex>

#include "den/base/files.h"
#include "den/library/downloads.h"
#include "den/library/hacks.h"
#include "den/patching/patch.h"
#include "den/plugins/feature_services.h"

namespace den::plugins {

namespace {

bool Every(const std::vector<std::string>& systems) {
    return std::find(systems.begin(), systems.end(), "*") != systems.end();
}

GameFacts FactsOf(const library::Entry& entry) {
    GameFacts game;
    game.id = entry.id;
    game.name = entry.name;
    game.system = entry.system;
    game.cover = entry.cover;
    game.hack_of = entry.hack_of;
    game.size = entry.size_bytes;
    return game;
}

DownloadFacts FactsOf(const library::downloads::Record& record) {
    DownloadFacts facts;
    facts.id = record.id;
    facts.outcome = library::downloads::OutcomeName(record.outcome);
    facts.message = record.message;
    facts.download = record.download;
    facts.console = record.system_name;
    facts.time = record.time;
    facts.replaced = record.replaced;
    facts.undone = record.undone;
    for (const library::downloads::AddedGame& game : record.games) {
        GameFacts added;
        added.id = game.id;
        added.name = game.name;
        added.system = game.system;
        facts.games.push_back(std::move(added));
    }
    return facts;
}

class LibraryServices final : public FeatureServices {
public:
    LibraryServices(library::Library& library, const std::filesystem::path& data_dir)
        : library_(library), data_dir_(data_dir) {}

    std::filesystem::path DownloadsFolder() override {
        return data_dir_.empty() ? std::filesystem::path() : data_dir_ / "downloads";
    }

    Result<DownloadFacts> SetUpDownload(const DownloadOrder& order, const std::atomic<bool>& cancel) override {
        library::downloads::Setup* setup = Downloads();
        if (setup == nullptr) {
            return Status::Fail("This app has no Downloads folder");
        }
        library::downloads::Request request;
        request.file = order.file;
        request.page_title = order.page_title;
        request.host = order.host;
        request.base_id = order.base_id;
        request.plugin = order.plugin;
        const library::downloads::Outcomes done = setup->Run(request, [&cancel] { return cancel.load(); });
        DownloadFacts facts = FactsOf(done.record);
        for (const library::Entry& entry : done.choices) {
            facts.choices.push_back(FactsOf(entry));
        }
        return facts;
    }

    std::vector<DownloadFacts> DownloadHistory() override {
        std::vector<DownloadFacts> out;
        if (library::downloads::Setup* setup = Downloads()) {
            for (const library::downloads::Record& record : setup->History()) {
                out.push_back(FactsOf(record));
            }
        }
        return out;
    }

    Status UndoDownload(const std::string& id) override {
        library::downloads::Setup* setup = Downloads();
        return setup != nullptr ? setup->Undo(id) : Status::Fail("This app has no Downloads folder");
    }

    Result<PatchFacts> InspectPatch(const std::filesystem::path& patch) override {
        const Result<patching::PatchInfo> info = patching::Inspect(patch);
        if (!info) {
            return info.status();
        }
        PatchFacts facts;
        facts.format = info.value().format_name;
        facts.source_size = info.value().source_size;
        facts.target_size = info.value().target_size;
        facts.source_crc32 = info.value().source_crc32;
        facts.target_crc32 = info.value().target_crc32;
        return facts;
    }

    std::optional<GameFacts> FindBase(const BaseWanted& wanted,
                                      const std::atomic<bool>& cancel) override {
        if (cancel.load()) {
            return std::nullopt;
        }
        library::BaseQuery query;
        if (!Every(wanted.systems)) {
            query.systems = wanted.systems;
        }
        query.crc32 = wanted.crc32;
        query.sha1 = wanted.sha1;
        query.md5 = wanted.md5;
        query.size = wanted.size;
        const std::optional<library::BaseMatch> match = library::HacksOf(library_).FindBase(query);
        return match ? std::optional<GameFacts>(FactsOf(match->entry)) : std::nullopt;
    }

    std::vector<GameFacts> Games(const std::vector<std::string>& systems) override {
        const bool every = Every(systems);
        std::vector<GameFacts> games;
        for (const library::Entry& entry : library_.Entries()) {
            if (every ||
                std::find(systems.begin(), systems.end(), entry.system) != systems.end()) {
                games.push_back(FactsOf(entry));
            }
        }
        return games;
    }

    std::optional<GameFacts> Game(const std::string& id) override {
        const std::optional<library::Entry> entry = library_.Find(id);
        return entry ? std::optional<GameFacts>(FactsOf(*entry)) : std::nullopt;
    }

    std::vector<HackFacts> Hacks(const std::string& plugin) override {
        std::vector<HackFacts> hacks;
        for (const library::Hack& hack : library::HacksOf(library_).List(plugin)) {
            hacks.push_back({hack.id, hack.title, hack.base_id, hack.version, hack.source,
                             hack.installed});
        }
        return hacks;
    }

    Result<HackMade> InstallHack(const HackOrder& order,
                                 const std::function<bool(float)>& progress) override {
        library::HackRequest request;
        request.base_id = order.base_id;
        request.patch = order.patch;
        request.title = order.title;
        request.version = order.version;
        request.description = order.description;
        request.plugin = order.plugin;
        request.source = order.source;
        request.cover = order.cover;
        request.keep_old = order.keep_old;
        const Result<library::Entry> entry =
            library::HacksOf(library_).InstallHack(request, progress);
        if (!entry) {
            return entry.status();
        }
        HackMade made;
        made.game = FactsOf(entry.value());
        made.path = PathFromUtf8(entry.value().path);
        return made;
    }

    Status UpdateHack(const std::string& id, const std::string& plugin,
                      const std::optional<std::string>& title,
                      const std::optional<std::string>& description,
                      const std::filesystem::path& cover) override {
        // A plugin always names itself: the empty name is the app's own and may change any hack.
        if (plugin.empty()) {
            return Status::Fail("A plugin changes only the hacks it installed.");
        }
        return library::HacksOf(library_).Update(id, plugin, title, description, cover);
    }

    Status RemoveHack(const std::string& id, const std::string& plugin) override {
        if (plugin.empty()) {
            return Status::Fail("A plugin changes only the hacks it installed.");
        }
        return library::HacksOf(library_).Remove(id, plugin);
    }

private:
    library::downloads::Setup* Downloads() {
        std::lock_guard lock(mutex_);
        if (!downloads_ && !data_dir_.empty()) {
            downloads_ = std::make_unique<library::downloads::Setup>(library_, DownloadsFolder(), data_dir_);
        }
        return downloads_.get();
    }

    library::Library& library_;
    std::filesystem::path data_dir_;
    std::mutex mutex_;
    std::unique_ptr<library::downloads::Setup> downloads_;
};

} // namespace

std::shared_ptr<FeatureServices> CreateLibraryServices(library::Library* library,
                                                       const std::filesystem::path& data_dir) {
    return library != nullptr ? std::make_shared<LibraryServices>(*library, data_dir) : nullptr;
}

} // namespace den::plugins
