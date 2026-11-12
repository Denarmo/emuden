// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/base/files.h"
#include "den/store/settings.h"
#include "host/locate.h"

namespace den::engine {

namespace {

struct KnownEngine {
    const char* system;
    // Executable names, most capable first, with the protocol level each is driven at.
    struct Name {
        const char* file;
        int level;
    };
    std::vector<Name> names;
    const char* desktop_folder; // a copy the user keeps on the desktop
};

// The Switch 2 engine of this stage is the previous emulator's own executable.
const std::vector<KnownEngine>& Known() {
    static const std::vector<KnownEngine> known = {
        {"switch2", {{"lichii.exe", 0}, {"lichii", 0}}, "Lichii"},
    };
    return known;
}

const KnownEngine* FindKnown(const std::string& system) {
    for (const KnownEngine& engine : Known()) {
        if (system == engine.system) {
            return &engine;
        }
    }
    return nullptr;
}

std::optional<Install> InFolder(const KnownEngine& engine, const std::filesystem::path& folder,
                                const char* source) {
    if (folder.empty() || !IsDirectory(folder)) {
        return std::nullopt;
    }
    for (const KnownEngine::Name& name : engine.names) {
        const std::filesystem::path file = folder / name.file;
        if (IsRegularFile(file)) {
            return Install{file, source, name.level};
        }
    }
    return std::nullopt;
}

std::filesystem::path HomeDir() {
    std::string home = GetEnv("USERPROFILE");
    if (home.empty()) {
        home = GetEnv("HOME");
    }
    return PathFromUtf8(home);
}

} // namespace

std::string PathKey(const std::string& system) {
    return "engine." + system + ".path";
}

std::string ArgsKey(const std::string& system) {
    return "engine." + system + ".args";
}

bool KnownSystem(const std::string& system) {
    return FindKnown(system) != nullptr;
}

std::optional<Install> Locate(const std::string& system, const std::filesystem::path& data_dir,
                              const store::Settings* settings) {
    const KnownEngine* engine = FindKnown(system);
    if (!engine) {
        return std::nullopt;
    }
    const std::string chosen = settings ? Trim(settings->GetString(PathKey(system))) : std::string();
    if (!chosen.empty()) {
        const std::filesystem::path path = PathFromUtf8(chosen);
        if (IsRegularFile(path)) {
            return Install{path, "setting", 0};
        }
        return InFolder(*engine, path, "setting");
    }
    for (const char* folder : {"engines", "emulators"}) {
        if (auto found = InFolder(*engine, data_dir / folder / system, "plugin")) {
            return found;
        }
    }
    const std::filesystem::path home = HomeDir();
    if (!home.empty()) {
        return InFolder(*engine, home / "Desktop" / engine->desktop_folder, "desktop");
    }
    return std::nullopt;
}

std::vector<std::string> LaunchArgs(const Install&, const LaunchRequest& request,
                                    const store::Settings* settings) {
    const std::string content = PathToUtf8(request.content);
    std::vector<std::string> args =
        settings ? settings->GetList(ArgsKey(request.system)) : std::vector<std::string>();
    if (args.empty()) {
        // The previous emulator's command line: -f full screen, -g <game file>.
        if (request.fullscreen) {
            args.push_back("-f");
        }
        args.push_back("-g");
        args.push_back(content);
        return args;
    }
    for (std::string& arg : args) {
        const std::string token = "{content}";
        for (std::size_t at = arg.find(token); at != std::string::npos;
             at = arg.find(token, at + content.size())) {
            arg.replace(at, token.size(), content);
        }
    }
    return args;
}

} // namespace den::engine
