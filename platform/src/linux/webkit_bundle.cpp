// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// WebKitGTK looks for its helper programs (WebKitWebProcess, WebKitNetworkProcess) in a
// folder written into the library when it was built, `/usr/lib/<triplet>/webkit2gtk-4.1`.
// The bundle's copy sits elsewhere, so right after loading, that one string is overwritten
// in this process's memory with the bundle's folder, padded with slashes to the same length
// (`a//b` is `a/b`). Nothing on disk changes, and every instance points at its own files.
// When the bundle's folder is too long, a link named after the process in the user's
// runtime folder stands in for it. The helpers find the bundle's libraries by their RPATH.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../own_env.h"
#include "den/base/log.h"
#include "webkit_bundle.h"

namespace den::pal::gtk {

namespace {

constexpr const char* LIBRARY = "libwebkit2gtk-4.1.so.0";

std::string Dir(const std::string& file) {
    const auto slash = file.rfind('/');
    return slash == std::string::npos ? std::string(".") : file.substr(0, slash);
}

bool IsFile(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool IsDir(const std::string& path) {
    struct stat info {};
    return !path.empty() && stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

/// `path` grown to `length` with slashes before its last part; empty when it is longer.
std::string Padded(const std::string& path, std::size_t length) {
    const auto slash = path.rfind('/');
    if (path.size() > length || slash == std::string::npos || slash == 0) {
        return {};
    }
    std::string padded = path;
    padded.insert(slash, length - path.size(), '/');
    return padded;
}

std::string g_link; // the stand-in link of this process, removed at exit

void RemoveLink() {
    if (!g_link.empty()) {
        unlink(g_link.c_str());
    }
}

/// A link to `target` named after this process, `length` bytes once padded; empty when
/// none can be made.
std::string StandInLink(const std::string& target, std::size_t length) {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    for (const std::string& dir : {std::string(runtime ? runtime : ""), std::string("/tmp")}) {
        if (!IsDir(dir)) {
            continue;
        }
        const std::string link = dir + "/emuden-webkit-" + std::to_string(getpid());
        const std::string padded = Padded(link, length);
        if (padded.empty()) {
            continue;
        }
        // A leftover of an earlier process with this number is ours to replace; someone
        // else's is not followed.
        struct stat info {};
        if (lstat(link.c_str(), &info) == 0) {
            if (!S_ISLNK(info.st_mode) || info.st_uid != getuid() || unlink(link.c_str()) != 0) {
                continue;
            }
        }
        if (symlink(target.c_str(), link.c_str()) == 0) {
            g_link = link;
            std::atexit(RemoveLink);
            return padded;
        }
    }
    return {};
}

struct Patch {
    const char* library;
    std::string from; // with its terminating zero
    std::string to;   // as long as `from`
    int replaced = 0;
};

/// Overwrites `from` with `to` in the read-only data of the loaded `library`.
int ReplaceInLibrary(dl_phdr_info* info, size_t, void* data) {
    auto& patch = *static_cast<Patch*>(data);
    if (!info->dlpi_name || std::strcmp(info->dlpi_name, patch.library) != 0) {
        return 0;
    }
    const auto page = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_LOAD || (segment.p_flags & (PF_W | PF_X)) != 0) {
            continue;
        }
        auto* begin = reinterpret_cast<char*>(info->dlpi_addr + segment.p_vaddr);
        char* const end = begin + segment.p_memsz;
        for (char* at = begin; at + patch.from.size() <= end;) {
            at = static_cast<char*>(memmem(at, static_cast<size_t>(end - at), patch.from.data(), patch.from.size()));
            if (!at) {
                break;
            }
            const auto first = reinterpret_cast<std::uintptr_t>(at) & ~(page - 1);
            const auto last = reinterpret_cast<std::uintptr_t>(at) + patch.from.size();
            void* pages = reinterpret_cast<void*>(first);
            if (mprotect(pages, last - first, PROT_READ | PROT_WRITE) != 0) {
                return 1;
            }
            std::memcpy(at, patch.to.data(), patch.to.size());
            mprotect(pages, last - first, PROT_READ);
            ++patch.replaced;
            at += patch.from.size();
        }
    }
    return 1;
}

} // namespace

std::string BundledWebKit() {
    char exe[4096];
    const ssize_t size = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (size <= 0) {
        return {};
    }
    const std::string library = Dir(Dir(std::string(exe, static_cast<std::size_t>(size)))) + "/lib/webkit/" + LIBRARY;
    return IsFile(library) ? library : std::string();
}

bool PrepareBundledWebKit(const std::string& library, std::string& reason) {
    const std::string dir = Dir(library);
    // The folder the library was built to look in, written down by the bundling script.
    std::string built;
    std::getline(std::ifstream(dir + "/libexec-dir"), built);
    if (built.empty() || built[0] != '/' || !IsFile(dir + "/WebKitWebProcess")) {
        reason = "The bundled web engine is incomplete (" + dir + ").";
        return false;
    }
    Patch patch{library.c_str(), built + '\0', {}};
    std::string to = Padded(dir, built.size());
    if (to.empty()) {
        to = StandInLink(dir, built.size());
    }
    if (to.empty()) {
        reason = "The bundled web engine cannot reach its helper programs (no short folder for them).";
        return false;
    }
    patch.to = to + '\0';
    dl_iterate_phdr(ReplaceInLibrary, &patch);
    if (patch.replaced == 0) {
        reason = "The bundled web engine does not name its helper folder as expected.";
        return false;
    }

    // Its injected bundle, the TLS module of GLib (without it no https), and GStreamer kept
    // to the bundle and away from the system's plugins and registry.
    SetOwnEnv("WEBKIT_INJECTED_BUNDLE_PATH", dir + "/injected-bundle");
    SetOwnEnv("GIO_MODULE_DIR", dir + "/gio");
    SetOwnEnv("GST_PLUGIN_SYSTEM_PATH_1_0", dir + "/gstreamer");
    const char* cache = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    if ((cache && *cache) || home) {
        SetOwnEnv("GST_REGISTRY_1_0", (cache && *cache ? std::string(cache) : std::string(home) + "/.cache") +
                                          "/emuden-webkit-gstreamer.bin");
    }
    log::Info("web", "bundled web engine: {} (helpers at {}, {} place{})", dir, to, patch.replaced,
              patch.replaced == 1 ? "" : "s");
    return true;
}

} // namespace den::pal::gtk
