// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the browser page saves and for whom: the openers, the extensions they take and the
// ROM images nobody gets.

#include <algorithm>
#include <map>

#include "den/app/app.h"
#include "den/base/files.h"
#include "web_browser.h"

namespace den::app::web {

namespace {

std::map<std::string, Opener>& Openers() {
    static std::map<std::string, Opener> openers;
    return openers;
}

std::string Join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ",") + item;
    }
    return out;
}

} // namespace

const std::vector<std::string>& DefaultExtensions() {
    static const std::vector<std::string> extensions = {"bps", "ips", "ups",  "xdelta", "vcdiff",
                                                        "aps", "ppf", "zip", "7z"};
    return extensions;
}

std::string ExtensionOf(const std::string& file_name) {
    const auto dot = file_name.rfind('.');
    return dot == std::string::npos ? std::string() : ToLower(file_name.substr(dot + 1));
}

bool IsRomImage(const std::string& extension) {
    static const char* const IMAGES[] = {
        "gb",  "gbc", "sgb", "gba", "nds", "dsi", "3ds", "cci", "cia", "3dsx", "nes", "fds", "unf", "unif",
        "sfc", "smc", "n64", "z64", "v64", "ndd", "gcm", "gcz", "rvz", "wbfs", "wad", "iso", "bin", "cue",
        "chd", "img", "ccd", "mdf", "mds", "nrg", "ecm", "cso", "pbp", "xci",  "nsp", "nsz", "xcz", "md",
        "gen", "smd", "32x", "sms", "gg",  "sg",  "pce", "ngp", "ngc", "ws",   "wsc", "vb",  "lnx", "a26",
        "a78", "j64", "jag", "col", "min", "gdi", "cdi", "rom", "vpk", "pkg",
    };
    return std::any_of(std::begin(IMAGES), std::end(IMAGES), [&](const char* image) { return extension == image; });
}

Verdict JudgeDownload(const std::string& file_name, const std::vector<std::string>& wanted) {
    const std::string extension = ExtensionOf(file_name);
    if (IsRomImage(extension)) {
        return Verdict::RomImage;
    }
    const std::vector<std::string>& list = wanted.empty() ? DefaultExtensions() : wanted;
    return std::find(list.begin(), list.end(), extension) != list.end() ? Verdict::Take : Verdict::NotWanted;
}

std::filesystem::path InboxDir() {
    return Svc().data_dir / "inbox" / "web";
}

void RegisterOpener(const std::string& name, Opener opener) {
    for (std::string& extension : opener.extensions) {
        extension = ToLower(Trim(extension));
    }
    std::erase_if(opener.extensions, [](const std::string& e) { return e.empty() || IsRomImage(e); });
    Openers()[name] = std::move(opener);
}

const Opener* FindOpener(const std::string& name) {
    const auto it = Openers().find(name);
    return it == Openers().end() ? nullptr : &it->second;
}

void Open(OpenRequest request) {
    // One visit at a time: the name is reused, so openers do not pile up.
    RegisterOpener("request", std::move(request.opener));
    Svc().stage->Push("web", {{"url", request.url},
                              {"title", request.title},
                              {"hosts", Join(request.allowed_hosts)},
                              {"opener", "request"}});
}

} // namespace den::app::web
