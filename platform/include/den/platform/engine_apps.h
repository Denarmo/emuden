// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Engines that are apps of their own (Android): checking the installed package, installing
// its APK through the system installer, and starting a game in it with an explicit intent
// (Emulator Control Protocol, section 3). On the desktop engines are child processes
// (den/platform/child.h) and none of this is available.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "den/base/result.h"
#include "den/platform/apps.h"

namespace den::pal::engine_apps {

/// The platform starts engines as apps. Constant for the life of the process.
bool Available();

struct Installed {
    bool present = false;
    std::string cert_sha256; // lower-case hex of the signing certificate (the current one)
    std::int64_t version_code = 0;
    std::string version_name;
    bool plays = false; // it has an activity for io.github.ecp.action.PLAY
};
/// What is installed under the package name. Safe on any thread.
Installed Query(const std::string& package);

/// Hands the APK to the system installer, which asks the user. Returns once it was handed
/// over; Query tells when it is installed. Any thread.
Status Install(const std::filesystem::path& apk);

/// Opens a store page or a download page in the system's app for it.
Status OpenStore(const std::string& url);

struct PlayRequest {
    std::string package;
    std::string cert_sha256; // checked again right before the start
    std::filesystem::path content;
    int port = 0;
    std::string token;
    std::string session;
    bool fullscreen = true;
    apps::Display display = apps::Display::Main;
};
/// Starts the engine's PLAY activity for a game (the game goes to it as a content URI it
/// may read, and the menu intent points back at this app). Main thread.
Status Play(const PlayRequest& request);
/// Level 0: hands the game to the app's launcher activity to view (port, token and session
/// are not used). Main thread.
Status Open(const PlayRequest& request);
/// Brings the engine's PLAY activity back to the front after the menu. Main thread.
Status Resume(const std::string& package, apps::Display display);

} // namespace den::pal::engine_apps
