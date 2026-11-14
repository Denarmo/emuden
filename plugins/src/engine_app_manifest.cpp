// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An engine-app component: an emulator app that speaks the Emulator Control Protocol,
// offered as one of a console's cores (docs/engines.md).

#include <algorithm>

#include "den/base/files.h"
#include "json_util.h"
#include "manifest_parts.h"

namespace den::plugins {

namespace {

// An Android package name: dot-separated Java identifiers, at least two of them.
bool IsPackageName(std::string_view name) {
    if (name.empty() || name.size() > 255 || name.find('.') == std::string_view::npos) {
        return false;
    }
    bool start = true;
    for (const char c : name) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (c == '.') {
            if (start) {
                return false;
            }
            start = true;
        } else if (start ? !letter : !(letter || digit)) {
            return false;
        } else {
            start = false;
        }
    }
    return !start;
}

} // namespace

std::string NormalCertDigest(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c != ':' && c != ' ') {
            out += static_cast<char>(c >= 'A' && c <= 'F' ? c - 'A' + 'a' : c);
        }
    }
    return out;
}

Result<EngineAppSpec> ParseEngineApp(const JsonValue& component) {
    EngineAppSpec spec;
    const JsonValue* ecp = component.Find("ecp");
    if (ecp == nullptr || ecp->kind != JsonValue::Kind::Object) {
        return Status::Fail("an engine app has no \"ecp\" descriptor");
    }
    spec.ecp_id = ecp->String("id");
    spec.level = static_cast<int>(json::Number(*ecp, "level", 1));
    if (spec.level < 1 || spec.level > 2) {
        return Status::Fail("an engine app has an unknown protocol level");
    }
    spec.capabilities = json::Strings(*ecp, "capabilities", false);
    spec.needs = json::Strings(*ecp, "needs", false);
    if (const JsonValue* executables = component.Find("executable")) {
        for (const auto& [platform, path] : executables->members) {
            if (path.kind != JsonValue::Kind::String || !IsSafeRelativePath(path.string) || path.string.ends_with("/")) {
                return Status::Fail("an engine app's executable is not a plain relative path");
            }
            spec.executables.emplace(platform, path.string);
        }
    }
    if (const JsonValue* android = component.Find("android")) {
        spec.package = android->String("package");
        spec.cert_sha256 = NormalCertDigest(android->String("cert_sha256"));
        spec.min_version_code = static_cast<std::int64_t>(json::Number(*android, "min_version_code", 0));
        spec.store_url = android->String("store");
        if (!IsPackageName(spec.package)) {
            return Status::Fail("an engine app has a bad package name");
        }
        // Without the pinned certificate any app could take the package name.
        if (!IsSha256Hex(spec.cert_sha256)) {
            return Status::Fail("an engine app does not pin its signing certificate");
        }
        if (const JsonValue* apk = android->Find("apk")) {
            Result<Download> download = ParseDownload(*apk);
            if (!download) {
                return download.status();
            }
            if (download->archive != "none") {
                return Status::Fail("an engine app's APK is not a plain file");
            }
            spec.apk = download.take();
        }
        if (!spec.store_url.empty() && !spec.store_url.starts_with("https://") &&
            !spec.store_url.starts_with("market://")) {
            return Status::Fail("an engine app's store page is not an https or market address");
        }
    }
    return spec;
}

bool IsAndroidKey(std::string_view platform_key) {
    return platform_key.starts_with("android-");
}

std::string Component::ExecutableFor(std::string_view platform_key) const {
    const auto it = engine_app.executables.find(platform_key);
    return it == engine_app.executables.end() ? std::string() : it->second;
}

bool Component::OffersPlatform(std::string_view platform_key) const {
    if (type != TYPE_ENGINE_APP) {
        return DownloadFor(platform_key) != nullptr;
    }
    if (IsAndroidKey(platform_key)) {
        return !engine_app.package.empty() && (engine_app.apk || !engine_app.store_url.empty());
    }
    return DownloadFor(platform_key) != nullptr && !ExecutableFor(platform_key).empty();
}

} // namespace den::plugins
