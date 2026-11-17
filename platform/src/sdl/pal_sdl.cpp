// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <jni.h>
#endif

#include "den/base/files.h"
#include "den/base/log.h"
#include "../frame_native.h"
#include "../lifecycle_native.h"
#include "../own_env.h"
#include "den/platform/pal.h"

namespace den::pal {

namespace {

Caps g_caps;

Caps DetectCaps() {
    Caps caps;
    const std::string platform = SDL_GetPlatform();
    if (platform == "Windows") {
        caps.os = "windows";
    } else if (platform == "Android") {
        caps.os = "android";
    } else if (platform == "macOS") {
        caps.os = "macos";
    } else {
        caps.os = "linux";
    }
    caps.arch = sizeof(void*) == 8 ? (SDL_HasNEON() && !SDL_HasSSE2() ? "arm64" : "x86_64") : "x86";
    caps.platform_key = caps.os + "-" + caps.arch;
    const bool android = caps.os == "android";
    caps.gles = android;
    caps.touch = android;
    // Gamescope (SteamOS Game Mode) shows one window over the whole screen: there is no window
    // to resize or leave full screen.
    const bool gamescope = !GetEnv("GAMESCOPE_WAYLAND_DISPLAY").empty();
    caps.windowed = !android && !gamescope;
    caps.child_processes = !android;
    caps.handheld = android || gamescope || !GetEnv("SteamDeck").empty();
    // SDL keeps a borderless Windows window's shadow, snapping and animations.
    caps.custom_frame = caps.os == "windows" && GetEnv("EMUDEN_SYSTEM_FRAME").empty();
    return caps;
}

class SdlWindowImpl final : public Window {
public:
    SdlWindowImpl(SDL_Window* window, SDL_GLContext context) : window_(window), context_(context) {}
    ~SdlWindowImpl() override {
        SDL_GL_DestroyContext(context_);
        SDL_DestroyWindow(window_);
    }
    void PixelSize(int& width, int& height) const override {
        SDL_GetWindowSizeInPixels(window_, &width, &height);
    }
    float DisplayScale() const override {
        const float scale = SDL_GetWindowDisplayScale(window_);
        return scale > 0.0f ? scale : 1.0f;
    }
    void Swap() override {
        SDL_GL_SwapWindow(window_);
    }
    void SetVsync(bool on) override {
        SDL_GL_SetSwapInterval(on ? 1 : 0);
    }
    void SetFullscreen(bool on) override {
        SDL_SetWindowFullscreen(window_, on);
    }
    bool Fullscreen() const override {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    }
    void Show() override {
        SDL_ShowWindow(window_);
    }
    void Hide() override {
        SDL_HideWindow(window_);
    }
    void Raise() override {
        // gamescope ignores raising and shows the game's most recently mapped window, so
        // mapping it again is what brings it over an engine's.
        if (!GetEnv("GAMESCOPE_WAYLAND_DISPLAY").empty() && (SDL_GetWindowFlags(window_) & SDL_WINDOW_HIDDEN) == 0) {
            SDL_HideWindow(window_);
        }
        SDL_ShowWindow(window_);
        SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
        SDL_RaiseWindow(window_);
        SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "0");
    }
    void SetTitle(const std::string& title) override {
        SDL_SetWindowTitle(window_, title.c_str());
    }
    void MakeCurrent() override {
        SDL_GL_MakeCurrent(window_, context_);
    }
    void* SdlWindow() const override {
        return window_;
    }
    void SetTitleBar(int height, std::vector<std::array<int, 4>> buttons) override {
        const bool was = bar_height_ > 0;
        bar_height_ = height;
        bar_buttons_ = std::move(buttons);
        if (was != (height > 0)) {
            SDL_SetWindowHitTest(window_, height > 0 ? &SdlWindowImpl::HitTest : nullptr, this);
        }
    }
    void SetFrameColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        StyleCustomFrame(window_, r, g, b);
    }
    void Minimize() override {
        SDL_MinimizeWindow(window_);
    }
    void ToggleMaximize() override {
        if (Maximized()) {
            SDL_RestoreWindow(window_);
        } else {
            SDL_MaximizeWindow(window_);
        }
    }
    bool Maximized() const override {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
    }

private:
    // SDL asks in window points; the bar is kept in pixels.
    static SDL_HitTestResult SDLCALL HitTest(SDL_Window* window, const SDL_Point* point, void* data) {
        const auto* self = static_cast<const SdlWindowImpl*>(data);
        const float density = SDL_GetWindowPixelDensity(window);
        const int x = static_cast<int>(point->x * density), y = static_cast<int>(point->y * density);
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        if (!self->Maximized() && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
            const int edge = static_cast<int>(6 * density);
            const bool left = x < edge, right = x >= w - edge, top = y < edge, bottom = y >= h - edge;
            if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
            if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
            if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
            if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
            if (left) return SDL_HITTEST_RESIZE_LEFT;
            if (right) return SDL_HITTEST_RESIZE_RIGHT;
            if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
            if (top && !self->InButton(x, y)) return SDL_HITTEST_RESIZE_TOP;
        }
        if (y < self->bar_height_ && !self->InButton(x, y)) {
            return SDL_HITTEST_DRAGGABLE;
        }
        return SDL_HITTEST_NORMAL;
    }
    bool InButton(int x, int y) const {
        for (const auto& b : bar_buttons_) {
            if (x >= b[0] && x < b[0] + b[2] && y >= b[1] && y < b[1] + b[3]) {
                return true;
            }
        }
        return false;
    }

    SDL_Window* window_;
    SDL_GLContext context_;
    int bar_height_ = 0;
    std::vector<std::array<int, 4>> bar_buttons_;
};

class SdlProcess final : public Process {
public:
    explicit SdlProcess(SDL_Process* process) : process_(process) {}
    ~SdlProcess() override {
        SDL_DestroyProcess(process_);
    }
    std::optional<int> Poll() override {
        if (!exit_) {
            int code = 0;
            if (SDL_WaitProcess(process_, false, &code)) {
                exit_ = code;
            }
        }
        return exit_;
    }
    int Wait() override {
        if (!exit_) {
            int code = -1;
            SDL_WaitProcess(process_, true, &code);
            exit_ = code;
        }
        return *exit_;
    }
    void Kill() override {
        if (!Poll()) {
            SDL_KillProcess(process_, true);
        }
    }

private:
    SDL_Process* process_;
    std::optional<int> exit_;
};

} // namespace

Status Init() {
    // A pad must not drive the app from behind another window. Android has no such case
    // (pad events only ever reach the app's own windows), and there the game must keep its
    // pad while the system keyboard on the second display has the focus.
    const bool android = std::string(SDL_GetPlatform()) == "Android";
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, android ? "1" : "0");
    // Android: Back is a key for the app (it goes back a page), never the end of the activity.
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetAppMetadata("EmuDEN", "0.1.0", "io.github.denarmo.emuden");
    LifecycleBeforeInit();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        return Status::Fail(std::string("SDL_Init: ") + SDL_GetError());
    }
    LifecycleAfterInit();
    g_caps = DetectCaps();
    return {};
}

void Shutdown() {
    SDL_Quit();
}

const Caps& GetCaps() {
    return g_caps;
}

Result<std::unique_ptr<Window>> CreateAppWindow(const WindowDesc& desc) {
    const bool gles = g_caps.gles;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        gles ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gles ? 0 : 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (g_caps.windowed) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.fullscreen || !g_caps.windowed) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (desc.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (desc.custom_frame && g_caps.custom_frame && g_caps.windowed) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (!window) {
        return Status::Fail(std::string("SDL_CreateWindow: ") + SDL_GetError());
    }
    if (desc.icon_size > 0 &&
        desc.icon_rgba.size() == static_cast<std::size_t>(desc.icon_size) * desc.icon_size * 4) {
        SDL_Surface* icon = SDL_CreateSurfaceFrom(desc.icon_size, desc.icon_size, SDL_PIXELFORMAT_RGBA32,
                                                  const_cast<std::uint8_t*>(desc.icon_rgba.data()),
                                                  desc.icon_size * 4);
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        const std::string error = SDL_GetError();
        SDL_DestroyWindow(window);
        return Status::Fail("SDL_GL_CreateContext: " + error);
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    return std::unique_ptr<Window>(new SdlWindowImpl(window, context));
}

void* GlProcAddress(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

void* JavaVm() {
#ifdef __ANDROID__
    static JavaVM* vm = [] {
        JavaVM* found = nullptr;
        if (auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv())) {
            env->GetJavaVM(&found);
        }
        return found;
    }();
    return vm;
#else
    return nullptr;
#endif
}

std::filesystem::path ExecutableDir() {
#ifdef __ANDROID__
    // An APK has no folder of its own: the app's private files folder stands in for it (the
    // activity unpacks the assets there).
    if (const char* files = SDL_GetAndroidInternalStoragePath()) {
        return PathFromUtf8(files);
    }
#endif
    const char* base = SDL_GetBasePath();
    return base ? PathFromUtf8(base) : std::filesystem::current_path();
}

// Where a portable "user" folder sits: beside the executable, or beside the AppImage file when
// the app runs from one (its own folder is a read-only mount then).
std::filesystem::path PortableBase() {
    if (const std::string image = GetEnv("APPIMAGE"); !image.empty()) {
        return PathFromUtf8(image).parent_path();
    }
    return ExecutableDir();
}

bool IsPortable() {
    return IsDirectory(PortableBase() / "user");
}

std::filesystem::path DataDir() {
    static const std::filesystem::path dir = [] {
        std::filesystem::path path;
        if (IsPortable()) {
            path = PortableBase() / "user";
        } else if (char* pref = SDL_GetPrefPath(nullptr, "EmuDEN")) {
            path = PathFromUtf8(pref);
            SDL_free(pref);
        } else {
            path = PortableBase() / "user";
        }
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return path;
    }();
    return dir;
}

std::filesystem::path DownloadsDir() {
    const char* folder = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
    if (folder == nullptr) {
        return {};
    }
    // SDL ends the folder with a separator: without the cut, parent_path() is the folder itself.
    std::filesystem::path path = PathFromUtf8(folder);
    if (!path.has_filename() && path.has_parent_path() && path != path.root_path()) {
        path = path.parent_path();
    }
    return path;
}

std::filesystem::path AssetsDir() {
    const std::filesystem::path beside = ExecutableDir() / "assets";
    if (IsDirectory(beside)) {
        return beside;
    }
    // Development builds run from build/<preset>/bin inside the source tree.
    std::filesystem::path up = ExecutableDir();
    for (int i = 0; i < 4 && up.has_parent_path(); ++i) {
        up = up.parent_path();
        if (IsDirectory(up / "assets" / "fonts")) {
            return up / "assets";
        }
    }
    return beside;
}

double NowSeconds() {
    return static_cast<double>(SDL_GetTicksNS()) / 1e9;
}

void SleepMs(int ms) {
    SDL_Delay(static_cast<Uint32>(ms));
}

std::string GetEnv(const char* name) {
    const char* value = SDL_getenv(name);
    return value ? std::string(value) : std::string();
}

std::vector<std::string> PreferredLanguages() {
    std::vector<std::string> out;
    if (const std::string forced = GetEnv("EMUDEN_LOCALE"); !forced.empty()) {
        std::size_t start = 0;
        while (start <= forced.size()) {
            const std::size_t end = std::min(forced.find(',', start), forced.size());
            if (end > start) {
                out.push_back(forced.substr(start, end - start));
            }
            start = end + 1;
        }
        return out;
    }
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    for (int i = 0; locales != nullptr && i < count; ++i) {
        if (locales[i] != nullptr && locales[i]->language != nullptr) {
            std::string code = locales[i]->language;
            if (locales[i]->country != nullptr && locales[i]->country[0] != '\0') {
                code += std::string("-") + locales[i]->country;
            }
            out.push_back(std::move(code));
        }
    }
    SDL_free(locales);
    return out;
}

Result<std::unique_ptr<Process>> StartProcess(const ProcessDesc& desc) {
    if (!g_caps.child_processes) {
        return Status::Fail("this device cannot start child processes");
    }
    const std::string exe = PathToUtf8(desc.executable);
    std::vector<const char*> argv;
    argv.push_back(exe.c_str());
    for (const std::string& arg : desc.args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    SDL_Environment* env = SDL_CreateEnvironment(true);
    for (const OwnEnvChange& change : OwnEnvChanges()) {
        if (change.had) {
            SDL_SetEnvironmentVariable(env, change.name.c_str(), change.before.c_str(), true);
        } else {
            SDL_UnsetEnvironmentVariable(env, change.name.c_str());
        }
    }
    for (const auto& [name, value] : desc.env) {
        SDL_SetEnvironmentVariable(env, name.c_str(), value.c_str(), true);
    }
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    const std::string error = process ? "" : SDL_GetError();
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (!process) {
        return Status::Fail("could not start " + exe + ": " + error);
    }
    return std::unique_ptr<Process>(new SdlProcess(process));
}

std::uint64_t HttpBodySize(const HttpRequest& request) {
    if (request.body_file.empty()) {
        return request.body.size();
    }
    std::error_code ec;
    const std::uint64_t size = std::filesystem::file_size(request.body_file, ec);
    if (ec || request.body_offset >= size) {
        return 0;
    }
    const std::uint64_t left = size - request.body_offset;
    return request.body_length == 0 ? left : std::min(left, request.body_length);
}

bool IsLoopbackUrl(const std::string& url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return false;
    }
    std::string host = url.substr(scheme + 3);
    host = host.substr(0, host.find_first_of("/?#"));
    if (host.find('@') != std::string::npos) {
        return false;
    }
    host = host.substr(0, host.rfind(':'));
    for (char& c : host) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return host == "127.0.0.1" || host == "localhost";
}

} // namespace den::pal
