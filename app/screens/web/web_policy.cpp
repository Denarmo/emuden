// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the browser page saves and for whom: the openers, the extensions they take and the
// ROM images nobody gets.

#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <sstream>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/patching/patch.h"
#include "den/platform/webview.h"
#include "web_browser.h"

namespace den::app::web {

namespace {

std::map<std::string, Opener>& Openers() {
    static std::map<std::string, Opener> openers;
    return openers;
}

// A download nobody collected (its opener was gone, or the app was closed first) does not
// stay for ever: what is older than a day goes when the app starts.
void CleanInbox() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(24);
    std::vector<fs::path> old;
    for (fs::directory_iterator it(InboxDir(), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code failed;
        const auto written = fs::last_write_time(it->path(), failed);
        if (!failed && written < limit) {
            old.push_back(it->path());
        }
    }
    for (const fs::path& path : old) {
        fs::remove_all(path, ec);
    }
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
    return patching::IsRomImageExtension(extension);
}

Verdict JudgeDownload(const std::string& file_name, const std::vector<std::string>& wanted) {
    const std::string extension = ExtensionOf(file_name);
    if (IsRomImage(extension) && std::find(wanted.begin(), wanted.end(), ANY) == wanted.end()) {
        return Verdict::RomImage;
    }
    const std::vector<std::string>& list = wanted.empty() ? DefaultExtensions() : wanted;
    if (std::find(list.begin(), list.end(), ANY) != list.end()) {
        return Verdict::Take;
    }
    return std::find(list.begin(), list.end(), extension) != list.end() ? Verdict::Take : Verdict::NotWanted;
}

std::string RefusalText(const std::string& file_name, Verdict verdict) {
    if (verdict == Verdict::RomImage) {
        return std::format("EmuDEN only installs patch files; this is a full game file ({})", file_name);
    }
    if (ExtensionOf(file_name) == "rar") {
        return std::format("{} is a rar archive, which EmuDEN does not open: download it in your browser, unpack it "
                           "and choose the patch file",
                           file_name);
    }
    return std::format("{} is not a file this page takes", file_name);
}

bool CanLeaveTo(const std::string& url) {
    const std::string host = pal::WebUrlHost(url);
    return !host.empty() && pal::WebUrlAllowed(url, {host});
}

std::vector<std::string> SplitList(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string item; std::getline(in, item, ',');) {
        if (item = ToLower(Trim(item)); !item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

std::filesystem::path InboxDir() {
    return Svc().data_dir / "inbox" / "web";
}

std::filesystem::path FreeFile(const std::filesystem::path& dir, const std::string& name) {
    const std::filesystem::path wanted = PathFromUtf8(name);
    std::filesystem::path file = dir / wanted;
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(file, ec) && n < 1000; ++n) {
        file = dir / PathFromUtf8(std::format("{} ({}){}", PathToUtf8(wanted.stem()), n, PathToUtf8(wanted.extension())));
    }
    return file;
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

DEN_REGISTER_STARTUP(CleanInbox);

void Open(OpenRequest request) {
    // One visit at a time: the name is reused, so openers do not pile up.
    RegisterOpener("request", std::move(request.opener));
    Svc().stage->Push("web", {{"url", request.url},
                              {"title", request.title},
                              {"hosts", request.browser ? std::string(ANY) : Join(request.allowed_hosts)},
                              {"opener", "request"},
                              {"mode", request.browser ? "browser" : ""}});
}

} // namespace den::app::web
