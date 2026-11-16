// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web_rules.h"

#include <algorithm>
#include <format>
#include <sstream>
#include <system_error>

#include "den/base/files.h"
#include "den/patching/patch.h"
#include "den/platform/webview.h"

namespace den::app::web {

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

std::filesystem::path FreeFile(const std::filesystem::path& dir, const std::string& name) {
    const std::filesystem::path wanted = PathFromUtf8(name);
    std::filesystem::path file = dir / wanted;
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(file, ec) && n < 1000; ++n) {
        file = dir / PathFromUtf8(std::format("{} ({}){}", PathToUtf8(wanted.stem()), n, PathToUtf8(wanted.extension())));
    }
    return file;
}

void CleanExtensions(std::vector<std::string>& extensions) {
    for (std::string& extension : extensions) {
        extension = ToLower(Trim(extension));
    }
    std::erase_if(extensions, [](const std::string& e) { return e.empty() || IsRomImage(e); });
}

} // namespace den::app::web
