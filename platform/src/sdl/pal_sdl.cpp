// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <map>
#include <mutex>

#include <SDL3/SDL.h>

#include "den/base/files.h"
#include "den/base/log.h"
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
    caps.windowed = !android;
    caps.child_processes = !android;
    caps.handheld = android || !GetEnv("SteamDeck").empty() || !GetEnv("GAMESCOPE_WAYLAND_DISPLAY").empty();
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
    void SetTitle(const std::string& title) override {
        SDL_SetWindowTitle(window_, title.c_str());
    }
    void MakeCurrent() override {
        SDL_GL_MakeCurrent(window_, context_);
    }
    void* SdlWindow() const override {
        return window_;
    }

private:
    SDL_Window* window_;
    SDL_GLContext context_;
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

class MemorySecrets final : public Secrets {
public:
    std::optional<std::string> Get(const std::string& key) override {
        std::lock_guard lock(mutex_);
        const auto it = values_.find(key);
        return it == values_.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    Status Set(const std::string& key, const std::string& value) override {
        std::lock_guard lock(mutex_);
        values_[key] = value;
        return {};
    }
    void Remove(const std::string& key) override {
        std::lock_guard lock(mutex_);
        values_.erase(key);
    }

private:
    std::mutex mutex_;
    std::map<std::string, std::string> values_;
};

} // namespace

Status Init() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    SDL_SetAppMetadata("EmuDEN", "0.1.0", "io.github.denarmo.emuden");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        return Status::Fail(std::string("SDL_Init: ") + SDL_GetError());
    }
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
    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (!window) {
        return Status::Fail(std::string("SDL_CreateWindow: ") + SDL_GetError());
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

std::filesystem::path ExecutableDir() {
    const char* base = SDL_GetBasePath();
    return base ? PathFromUtf8(base) : std::filesystem::current_path();
}

bool IsPortable() {
    return IsDirectory(ExecutableDir() / "user");
}

std::filesystem::path DataDir() {
    static const std::filesystem::path dir = [] {
        std::filesystem::path path;
        if (IsPortable()) {
            path = ExecutableDir() / "user";
        } else if (char* pref = SDL_GetPrefPath(nullptr, "EmuDEN")) {
            path = PathFromUtf8(pref);
            SDL_free(pref);
        } else {
            path = ExecutableDir() / "user";
        }
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return path;
    }();
    return dir;
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

Secrets& GetSecrets() {
    static MemorySecrets secrets;
    return secrets;
}

} // namespace den::pal
