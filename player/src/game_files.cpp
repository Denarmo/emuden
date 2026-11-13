// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>

#include "den/base/files.h"
#include "den/base/archive.h"
#include "game_files.h"

namespace den::player {

namespace fs = std::filesystem;

namespace {

constexpr char STATE_MAGIC[8] = {'D', 'E', 'N', 'S', 'T', 'A', 'T', '1'};

std::string Extension(const fs::path& path) {
    std::string ext = ToLower(PathToUtf8(path.extension()));
    return ext.empty() ? ext : ext.substr(1);
}

} // namespace

std::string SafeFileName(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        const bool bad = u < 0x20 || std::strchr("<>:\"/\\|?*", c) != nullptr;
        out.push_back(bad ? '_' : c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    if (out.size() > 120) {
        out.resize(120);
    }
    return out.empty() ? "game" : out;
}

fs::path GamePaths::BatterySave() const {
    return save_dir / PathFromUtf8(base + ".srm");
}

fs::path GamePaths::State(int slot) const {
    return state_dir / PathFromUtf8(base + ".slot" + std::to_string(slot) + ".state");
}

fs::path GamePaths::Thumbnail(int slot) const {
    return state_dir / PathFromUtf8(base + ".slot" + std::to_string(slot) + ".png");
}

fs::path GamePaths::Resume() const {
    return state_dir / PathFromUtf8(base + ".resume.state");
}

GamePaths MakeGamePaths(const fs::path& data_dir, const std::string& system,
                        const std::string& game_id, const fs::path& content) {
    GamePaths paths;
    const fs::path folder = PathFromUtf8(SafeFileName(system.empty() ? "other" : system));
    paths.save_dir = data_dir / "saves" / folder;
    paths.state_dir = data_dir / "states" / folder;
    paths.base = SafeFileName(game_id.empty() ? PathToUtf8(content.stem()) : game_id);
    return paths;
}

std::vector<std::uint8_t> WrapState(const std::string& core_name,
                                    std::span<const std::uint8_t> state) {
    const std::string name = core_name.substr(0, 255);
    std::vector<std::uint8_t> out;
    out.reserve(sizeof(STATE_MAGIC) + 1 + name.size() + state.size());
    out.insert(out.end(), STATE_MAGIC, STATE_MAGIC + sizeof(STATE_MAGIC));
    out.push_back(static_cast<std::uint8_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), state.begin(), state.end());
    return out;
}

Result<std::span<const std::uint8_t>> UnwrapState(const std::string& core_name,
                                                  std::span<const std::uint8_t> file) {
    using Out = Result<std::span<const std::uint8_t>>;
    if (file.size() < sizeof(STATE_MAGIC) + 1 ||
        std::memcmp(file.data(), STATE_MAGIC, sizeof(STATE_MAGIC)) != 0) {
        return Out::Fail("This file is not a save state.");
    }
    const std::size_t name_size = file[sizeof(STATE_MAGIC)];
    const std::size_t header = sizeof(STATE_MAGIC) + 1 + name_size;
    if (file.size() <= header) {
        return Out::Fail("The save state is cut short.");
    }
    const std::string_view name(
        reinterpret_cast<const char*>(file.data()) + sizeof(STATE_MAGIC) + 1, name_size);
    if (name != std::string_view(core_name).substr(0, 255)) {
        return Out::Fail("The save state was made with another core (" + std::string(name) + ").");
    }
    return file.subspan(header);
}

Result<fs::path> ResolveContent(const fs::path& content,
                                const std::vector<std::string>& extensions,
                                const fs::path& cache_dir) {
    using Out = Result<fs::path>;
    const auto accepted = [&](const std::string& ext) {
        return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
    };
    const std::string packed = Extension(content);
    if (!IsArchiveExtension(packed) || accepted(packed)) {
        return content;
    }
    const auto entries = ListArchive(content);
    if (!entries || entries->empty()) {
        return Out::Fail("The " + packed + " file could not be opened: " + PathToUtf8(content.filename()));
    }
    const ZipEntry* pick = nullptr;
    for (const ZipEntry& entry : *entries) {
        if (entry.encrypted || entry.symlink || !CanExtractArchiveEntry(entry)) {
            continue;
        }
        if (accepted(Extension(PathFromUtf8(entry.name)))) {
            pick = &entry;
            break;
        }
    }
    if (!pick) {
        return Out::Fail("The " + packed + " file holds no game this console can play.");
    }
    std::error_code ec;
    fs::remove_all(cache_dir, ec);
    fs::create_directories(cache_dir, ec);
    const fs::path name = PathFromUtf8(SafeFileName(PathToUtf8(PathFromUtf8(pick->name).filename())));
    const fs::path target = cache_dir / name;
    if (const Status status = ExtractArchiveEntry(content, *pick, target); !status) {
        return Out::Fail("The game could not be unpacked: " + status.error);
    }
    return target;
}

} // namespace den::player
