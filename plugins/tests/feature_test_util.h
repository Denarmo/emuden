// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared by the feature plugin tests: a scratch folder, a fake network, a fake library.

#pragma once

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "den/base/files.h"
#include "den/base/sha256.h"
#include "den/plugins/feature_services.h"

namespace den::plugins::testing {

namespace fs = std::filesystem;

struct ScratchDir {
    fs::path dir;
    ScratchDir() {
        static std::atomic<int> next{0};
        dir = fs::temp_directory_path() /
              ("den-feature-test-" + std::to_string(std::rand()) + "-" + std::to_string(next++));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void Write(const std::string& name, const std::string& data) const {
        fs::create_directories((dir / name).parent_path());
        std::ofstream(dir / name, std::ios::binary) << data;
    }
};

inline std::string HashOf(const std::string& data) {
    Sha256 hash;
    hash.Update(data.data(), data.size());
    return hash.FinishHex();
}

// A zip of stored members.
inline std::string StoredZipOf(const std::vector<std::pair<std::string, std::string>>& files) {
    const auto put16 = [](std::string& out, std::uint16_t v) {
        out.push_back(static_cast<char>(v & 0xff));
        out.push_back(static_cast<char>(v >> 8));
    };
    const auto put32 = [&](std::string& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v & 0xffff));
        put16(out, static_cast<std::uint16_t>(v >> 16));
    };
    std::string zip, central;
    for (const auto& [name, data] : files) {
        const auto offset = static_cast<std::uint32_t>(zip.size());
        const auto fields = [&](std::string& out) {
            put16(out, 20);
            put16(out, 0);
            put16(out, 0);
            put32(out, 0);
            put32(out, Crc32(data.data(), data.size()));
            put32(out, static_cast<std::uint32_t>(data.size()));
            put32(out, static_cast<std::uint32_t>(data.size()));
            put16(out, static_cast<std::uint16_t>(name.size()));
            put16(out, 0);
        };
        put32(zip, 0x04034b50);
        fields(zip);
        zip += name + data;
        put32(central, 0x02014b50);
        put16(central, 20);
        fields(central);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central += name;
    }
    const auto central_at = static_cast<std::uint32_t>(zip.size());
    zip += central;
    put32(zip, 0x06054b50);
    put16(zip, 0);
    put16(zip, 0);
    put16(zip, static_cast<std::uint16_t>(files.size()));
    put16(zip, static_cast<std::uint16_t>(files.size()));
    put32(zip, static_cast<std::uint32_t>(central.size()));
    put32(zip, central_at);
    put16(zip, 0);
    return zip;
}

// Answers from memory and remembers what was asked.
struct FakeWeb {
    std::map<std::string, std::string> files;
    std::map<std::string, int> statuses; // a status other than 200 for a URL
    std::vector<std::string> asked;
    std::vector<std::string> agents;
    std::mutex mutex;

    pal::HttpResponse Get(const pal::HttpRequest& request) {
        std::lock_guard lock(mutex);
        asked.push_back(request.url);
        for (const auto& [name, value] : request.headers) {
            if (name == "User-Agent") {
                agents.push_back(value);
            }
        }
        pal::HttpResponse response;
        if (const auto status = statuses.find(request.url); status != statuses.end()) {
            response.status = status->second;
            return response;
        }
        const auto it = files.find(request.url);
        if (it == files.end()) {
            response.status = 404;
            return response;
        }
        response.status = 200;
        response.received = it->second.size();
        if (request.progress && !request.progress(it->second.size(), it->second.size())) {
            response.error = "cancelled";
            return response;
        }
        if (request.to_file.empty()) {
            response.body = it->second;
        } else {
            std::ofstream(request.to_file, std::ios::binary) << it->second;
        }
        return response;
    }
    HttpGetter Getter() {
        return [this](const pal::HttpRequest& request) { return Get(request); };
    }
    int Count() {
        std::lock_guard lock(mutex);
        return static_cast<int>(asked.size());
    }
};

// A library of two games; a "patch" is any file starting with BPS1 made for the first.
struct FakeServices final : FeatureServices {
    std::vector<GameFacts> games = {
        {"g-emerald", "Pokemon Emerald", "gba", "", "", 16u << 20},
        {"g-zelda", "Zelda", "snes", "", "", 1u << 20},
    };
    std::vector<HackFacts> hacks;
    std::vector<HackOrder> orders;
    std::mutex mutex;

    Result<PatchFacts> InspectPatch(const fs::path& patch) override {
        const auto text = ReadTextFile(patch);
        if (!text || !text->starts_with("BPS1")) {
            return Status::Fail("This file is not a patch.");
        }
        PatchFacts facts;
        facts.format = "bps";
        facts.source_crc32 = 0x1f1c08fb;
        facts.source_size = 16u << 20;
        facts.target_size = 32u << 20;
        return facts;
    }
    std::optional<GameFacts> FindBase(const BaseWanted& wanted,
                                      const std::atomic<bool>&) override {
        for (const GameFacts& game : games) {
            const bool seen = std::find(wanted.systems.begin(), wanted.systems.end(),
                                        game.system) != wanted.systems.end();
            if (seen && game.id == "g-emerald" && wanted.crc32 == 0x1f1c08fbu) {
                return game;
            }
        }
        return std::nullopt;
    }
    std::vector<GameFacts> Games(const std::vector<std::string>& systems) override {
        std::vector<GameFacts> out;
        for (const GameFacts& game : games) {
            if (std::find(systems.begin(), systems.end(), game.system) != systems.end()) {
                out.push_back(game);
            }
        }
        return out;
    }
    std::optional<GameFacts> Game(const std::string& id) override {
        for (const GameFacts& game : games) {
            if (game.id == id) {
                return game;
            }
        }
        return std::nullopt;
    }
    std::vector<HackFacts> Hacks(const std::string&) override {
        std::lock_guard lock(mutex);
        return hacks;
    }
    Result<HackMade> InstallHack(const HackOrder& order,
                                 const std::function<bool(float)>&) override {
        std::lock_guard lock(mutex);
        orders.push_back(order);
        hacks.push_back({"h-" + order.title, order.title, order.base_id, order.version,
                         order.source, 1});
        HackMade made;
        made.game = {"h-" + order.title, order.title, "gba", "", order.base_id, 32u << 20};
        made.path = "hacks/gba/" + order.title + ".gba";
        return made;
    }
    Status UpdateHack(const std::string&, const std::string&, const std::optional<std::string>&,
                      const std::optional<std::string>&, const fs::path&) override {
        return {};
    }
    Status RemoveHack(const std::string& id, const std::string&) override {
        std::lock_guard lock(mutex);
        std::erase_if(hacks, [&](const HackFacts& hack) { return hack.id == id; });
        return {};
    }
};

} // namespace den::plugins::testing
