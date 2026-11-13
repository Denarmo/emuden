// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dlfcn.h>

#include "gtk_api.h"

namespace den::pal::gtk {

namespace {

// WebKitGTK for GTK 3: the libsoup 3 build, then the older libsoup 2 one. Their interfaces
// are the same for what is used here. (WebKitGTK 6.0 is GTK 4, which cannot be placed inside
// another program's window, so it is not tried.)
constexpr const char* LIBRARIES[] = {"libwebkit2gtk-4.1.so.0", "libwebkit2gtk-4.0.so.37"};

struct Loaded {
    Api api;
    bool tried = false, ok = false;
    std::string reason;
};

void LoadInto(Loaded& loaded) {
    void* handle = nullptr;
    for (const char* name : LIBRARIES) {
        // Local: the engine's symbols stay out of the app's own lookups.
        handle = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (handle) {
            loaded.api.library = name;
            break;
        }
    }
    if (!handle) {
        loaded.reason = "This system has no web engine EmuDEN can use (WebKitGTK 4.1 is not installed).";
        return;
    }
    // The handle's own dependencies (GTK, GDK, GLib, cairo, libsoup, Xlib) are searched too.
#define DEN_GTK_RESOLVE(ret, name, args)                                                            \
    loaded.api.name = reinterpret_cast<ret (*) args>(dlsym(handle, #name));                         \
    if (!loaded.api.name && loaded.reason.empty()) {                                                \
        loaded.reason = std::string("The WebKitGTK on this system cannot be used (no ") + #name + ")."; \
    }
    DEN_GTK_FUNCTIONS(DEN_GTK_RESOLVE)
#undef DEN_GTK_RESOLVE
    if (!loaded.reason.empty()) {
        dlclose(handle);
        return;
    }
    loaded.ok = true; // the library stays loaded for the life of the process
}

} // namespace

const Api* Load(std::string& reason) {
    static Loaded loaded;
    if (!loaded.tried) {
        loaded.tried = true;
        LoadInto(loaded);
    }
    reason = loaded.reason;
    return loaded.ok ? &loaded.api : nullptr;
}

} // namespace den::pal::gtk
