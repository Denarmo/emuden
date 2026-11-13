// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.patch and den.library over the app's library: the patching module reads patches,
// the library's hacks find base games and add the patched copies.

#include <algorithm>

#include "den/base/files.h"
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

class LibraryServices final : public FeatureServices {
public:
    explicit LibraryServices(library::Library& library) : library_(library) {}

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
    library::Library& library_;
};

} // namespace

std::shared_ptr<FeatureServices> CreateLibraryServices(library::Library* library) {
    return library != nullptr ? std::make_shared<LibraryServices>(*library) : nullptr;
}

} // namespace den::plugins
