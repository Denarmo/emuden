// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sync tests' devices: a provider in memory shared by them, each with a data folder of
// its own.

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <mutex>

#include "den/base/files.h"
#include "den/base/jobs.h"
#include "den/base/json.h"
#include "den/store/settings.h"
#include "den/sync/engine.h"
#include "den/sync/names.h"
#include "den/sync/plan.h"
#include "den/sync/provider.h"

using namespace den;
using namespace den::sync;
namespace fs = std::filesystem;

namespace sync_test {

struct Scratch {
    fs::path dir;
    Scratch() {
        static std::atomic<int> next{0};
        dir = fs::temp_directory_path() / ("den-sync-test-" + std::to_string(std::rand()) + "-" + std::to_string(next++));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

inline void Write(const fs::path& file, const std::string& data, std::int64_t time = 0) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << data;
    if (time > 0) {
        SetFileTime(file, time);
    }
}

inline std::string Read(const fs::path& file) {
    return ReadTextFile(file).value_or("<none>");
}

// The remote side in memory, shared by the "devices" of a test.
struct Cloud {
    struct File {
        std::string data;
        std::int64_t modified = 0;
        int rev = 0;
    };
    std::map<std::string, File> files;
    std::mutex mutex;
    int next_rev = 1;
    std::vector<std::string> puts;
    std::function<void(const std::string& path)> on_put; // before the file is stored
};

class MemoryProvider final : public Provider {
public:
    explicit MemoryProvider(Cloud& cloud) : cloud_(cloud) {}
    std::string Id() const override {
        return "test.cloud";
    }
    Result<std::vector<RemoteFile>> List(const std::string& prefix) override {
        std::lock_guard lock(cloud_.mutex);
        std::vector<RemoteFile> out;
        for (const auto& [path, file] : cloud_.files) {
            if (path.starts_with(prefix)) {
                out.push_back({path, file.data.size(), file.modified, std::to_string(file.rev)});
            }
        }
        return out;
    }
    Status Get(const std::string& path, const fs::path& local, const Progress&) override {
        std::lock_guard lock(cloud_.mutex);
        const auto found = cloud_.files.find(path);
        if (found == cloud_.files.end()) {
            return Status::Fail("missing");
        }
        Write(local, found->second.data);
        return {};
    }
    Result<std::string> Put(const std::string& path, const fs::path& local, const std::string& rev,
                            std::int64_t modified, const Progress&) override {
        if (cloud_.on_put) {
            cloud_.on_put(path);
        }
        std::lock_guard lock(cloud_.mutex);
        auto& file = cloud_.files[path];
        if (!rev.empty() && file.rev != 0 && std::to_string(file.rev) != rev) {
            return Status::Fail("conflict");
        }
        file.data = Read(local);
        file.modified = modified;
        file.rev = cloud_.next_rev++;
        cloud_.puts.push_back(path);
        return std::to_string(file.rev);
    }
    Status Delete(const std::string& path) override {
        std::lock_guard lock(cloud_.mutex);
        cloud_.files.erase(path);
        return {};
    }

private:
    Cloud& cloud_;
};

inline library::Entry Game(const std::string& id, const std::string& system, const fs::path& file) {
    library::Entry entry;
    entry.id = id;
    entry.system = system;
    entry.path = PathToUtf8(file);
    entry.name = PathToUtf8(file.stem());
    return entry;
}

// One device: its data folder, settings and library.
struct Device {
    Scratch scratch;
    std::unique_ptr<store::Settings> settings;
    std::vector<library::Entry> entries;
    std::shared_ptr<Engine> engine;
    std::int64_t clock = 1'800'000'000;

    std::map<std::string, library::PlayTotals> own_play, other_play;

    Device(Cloud& cloud, const std::string& name, std::vector<std::string> categories = {"saves", "states"}) {
        settings = std::make_unique<store::Settings>(scratch.dir / "settings.json");
        EngineConfig config;
        config.data_dir = scratch.dir;
        config.settings = settings.get();
        config.device = name;
        config.now = [this] { return clock; };
        config.entries = [this] { return entries; };
        config.providers = {{"test.cloud", "Cloud", std::move(categories)}};
        config.provider_factory = [&cloud](const std::string&) { return std::make_unique<MemoryProvider>(cloud); };
        config.own_play = [this] { return own_play; };
        config.set_other_play = [this](const std::map<std::string, library::PlayTotals>& others) {
            other_play = others;
        };
        engine = CreateEngine(config);
        settings->SetString("sync.active", "test.cloud");
    }
    fs::path Save(const std::string& id) const {
        return scratch.dir / "saves" / "gba" / (SafeFileName(id) + ".srm");
    }
    Status Sync() {
        return engine->RunOnce({}, nullptr);
    }
};

} // namespace sync_test

using namespace sync_test;
