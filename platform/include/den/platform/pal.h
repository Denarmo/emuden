// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The platform abstraction: everything the rest of the app may ask the operating system.
// Device differences are answered by Caps, never by an #ifdef outside platform/.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "den/base/result.h"

namespace den::pal {

struct Caps {
    std::string os;           // "windows", "linux", "android", "macos"
    std::string arch;         // "x86_64", "arm64"
    std::string platform_key; // plugin manifests' key, e.g. "windows-x86_64"
    bool gles = false;        // the GL context is OpenGL ES 3
    bool touch = false;       // a touch screen is the main input
    bool windowed = true;     // windows can be resized and left
    bool child_processes = true;
    bool handheld = false; // SteamOS-style handheld or Android
};

/// Starts SDL (video, gamepads). Call once before anything else in pal.
Status Init();
void Shutdown();
const Caps& GetCaps();

struct WindowDesc {
    std::string title = "EmuDEN";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool hidden = false;
    /// The window's own icon (title bar, task switcher), square RGBA; empty for none.
    std::vector<std::uint8_t> icon_rgba;
    int icon_size = 0;
};

/// A window with its OpenGL (3.3 core) or OpenGL ES 3 context, current on the calling thread.
class Window {
public:
    virtual ~Window() = default;
    virtual void PixelSize(int& width, int& height) const = 0;
    /// Pixels per logical unit of the display the window is on (1.0 at 96 dpi).
    virtual float DisplayScale() const = 0;
    virtual void Swap() = 0;
    virtual void SetVsync(bool on) = 0;
    virtual void SetFullscreen(bool on) = 0;
    virtual bool Fullscreen() const = 0;
    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual void SetTitle(const std::string& title) = 0;
    virtual void MakeCurrent() = 0;
    /// The SDL_Window, for code that needs SDL itself (audio, input).
    virtual void* SdlWindow() const = 0;
};

Result<std::unique_ptr<Window>> CreateAppWindow(const WindowDesc& desc);
/// Looks up an OpenGL function of the current context; the loader gfx is started with.
void* GlProcAddress(const char* name);

// Folders.
std::filesystem::path ExecutableDir();
/// "user" next to the executable when that folder exists (portable), else the OS app-data
/// folder under "EmuDEN". Created when missing.
std::filesystem::path DataDir();
bool IsPortable();
/// The read-only files shipped with the app (fonts, strings).
std::filesystem::path AssetsDir();
/// The user's Downloads folder; empty when the device has none the app can list.
std::filesystem::path DownloadsDir();

/// Opens an http or https address in the user's browser (the Windows shell, xdg-open, an
/// Android intent). Only for something the user just pressed: the app opens no page by itself.
Status OpenUrl(const std::string& url);

double NowSeconds();
void SleepMs(int ms);
std::string GetEnv(const char* name);

// HTTP.
struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    /// When set the body is streamed to this file (written as "<file>.part", renamed when
    /// complete) and HttpResponse::body stays empty.
    std::filesystem::path to_file;
    /// Bodies kept in memory are cut off here.
    std::uint64_t max_body = 64u << 20;
    int timeout_seconds = 30;
    /// Called while the body arrives (total is 0 when unknown); returning false cancels.
    std::function<bool(std::uint64_t received, std::uint64_t total)> progress;
};

struct HttpResponse {
    int status = 0; // 0 when the request never got an answer
    std::string body;
    std::string error;
    std::uint64_t received = 0;
    bool ok() const {
        return status >= 200 && status < 300 && error.empty();
    }
};

/// A blocking GET that follows redirects. Run it from a job.
HttpResponse HttpGet(const HttpRequest& request);

// Child processes.
struct ProcessDesc {
    std::filesystem::path executable;
    std::vector<std::string> args;
    /// Added to (or replacing in) the app's own environment.
    std::vector<std::pair<std::string, std::string>> env;
};

class Process {
public:
    virtual ~Process() = default;
    /// The exit code once the process has ended.
    virtual std::optional<int> Poll() = 0;
    virtual int Wait() = 0;
    virtual void Kill() = 0;
};

Result<std::unique_ptr<Process>> StartProcess(const ProcessDesc& desc);

/// Secrets (tokens). This stage keeps them in memory only; the shells replace it with DPAPI,
/// Secret Service and Keystore.
class Secrets {
public:
    virtual ~Secrets() = default;
    virtual std::optional<std::string> Get(const std::string& key) = 0;
    virtual Status Set(const std::string& key, const std::string& value) = 0;
    virtual void Remove(const std::string& key) = 0;
};
Secrets& GetSecrets();

/// A line-based request/reply server on 127.0.0.1 (the automation endpoint). A client
/// connects, sends one line and gets one line back.
class LineServer {
public:
    struct Request {
        std::uint64_t id = 0;
        std::string line;
    };
    virtual ~LineServer() = default;
    /// The next waiting request, if any. Never blocks.
    virtual std::optional<Request> Next() = 0;
    virtual void Reply(std::uint64_t id, const std::string& line) = 0;
};
Result<std::unique_ptr<LineServer>> StartLineServer(int port);

} // namespace den::pal
