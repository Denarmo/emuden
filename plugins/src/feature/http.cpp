// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/json.h"
#include "feature/http.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr int MAX_IN_FLIGHT = 2;

// Headers a script may not set: the host writes them, or never sends them.
bool Reserved(std::string_view name) {
    for (const char* reserved : {"user-agent", "cookie", "referer", "host", "authorization",
                                 "proxy-authorization", "content-length"}) {
        if (EqualsNoCase(name, reserved)) {
            return true;
        }
    }
    return false;
}

std::string CacheKey(const std::string& url) {
    return Sha256Hex(url.data(), url.size());
}

} // namespace

UrlParts ParseUrl(std::string_view url) {
    UrlParts parts;
    for (const char c : url) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f || c == '\\') {
            return parts;
        }
    }
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos || scheme_end == 0) {
        return parts;
    }
    parts.scheme = ToLower(url.substr(0, scheme_end));
    std::string_view rest = url.substr(scheme_end + 3);
    const std::size_t path_at = rest.find_first_of("/?#");
    std::string_view authority = rest.substr(0, path_at);
    parts.path = path_at == std::string_view::npos ? "/" : std::string(rest.substr(path_at));
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return parts; // "https://allowed.example@evil.example/" names another host
    }
    if (const std::size_t colon = authority.rfind(':'); colon != std::string_view::npos) {
        const std::string_view port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 ||
            !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return parts;
        }
        parts.port = std::stoi(std::string(port));
        authority = authority.substr(0, colon);
    }
    parts.host = ToLower(authority);
    parts.valid = !parts.host.empty();
    return parts;
}

Status CheckUrl(std::string_view url, const std::vector<std::string>& hosts) {
    const UrlParts parts = ParseUrl(url);
    if (!parts.valid) {
        return Status::Fail("that is not a web address");
    }
    if (parts.scheme != "https" || (parts.port != 0 && parts.port != 443)) {
        return Status::Fail("only https addresses are allowed");
    }
    if (std::find(hosts.begin(), hosts.end(), parts.host) == hosts.end()) {
        return Status::Fail("the plugin did not declare the host " + parts.host);
    }
    return {};
}

HttpPolicy::HttpPolicy(HttpSettings settings) : settings_(std::move(settings)) {}

Status HttpPolicy::Enter(const std::string& host) {
    std::unique_lock lock(mutex_);
    for (;;) {
        if (Cancelled()) {
            return Status::Fail("cancelled");
        }
        if (const auto blocked = blocked_until_.find(host);
            blocked != blocked_until_.end() && settings_.now() < blocked->second) {
            return Status::Fail("the server asked for a pause; try again later");
        }
        const auto now = std::chrono::steady_clock::now();
        const auto next = next_.find(host);
        const bool paused = next != next_.end() && now < next->second;
        if (in_flight_ < MAX_IN_FLIGHT && !paused) {
            break;
        }
        changed_.wait_for(lock, std::chrono::milliseconds(25));
    }
    ++in_flight_;
    // The pause runs from the start of a request, and again from its end (Leave).
    next_[host] = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(settings_.min_interval_ms);
    return {};
}

void HttpPolicy::Leave(const std::string& host, int status) {
    std::lock_guard lock(mutex_);
    --in_flight_;
    next_[host] = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(settings_.min_interval_ms);
    if (status == 429) {
        blocked_until_[host] = settings_.now() + BLOCKED_AFTER_429_SECONDS;
    }
    changed_.notify_all();
}

std::vector<std::pair<std::string, std::string>> HttpPolicy::Headers(
    const std::vector<std::pair<std::string, std::string>>& extra) const {
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("User-Agent", settings_.user_agent);
    for (const auto& [name, value] : extra) {
        const bool plain = !name.empty() &&
                           name.find_first_of("\r\n: ") == std::string::npos &&
                           value.find_first_of("\r\n") == std::string::npos;
        if (plain && !Reserved(name)) {
            headers.emplace_back(name, value);
        }
    }
    return headers;
}

HttpAnswer HttpPolicy::Get(const std::string& url,
                           const std::vector<std::pair<std::string, std::string>>& headers,
                           int cache_seconds) {
    HttpAnswer answer;
    if (const Status allowed = Allowed(url); !allowed.ok) {
        answer.error = allowed.error;
        return answer;
    }
    const fs::path body_file = settings_.cache_dir / (CacheKey(url) + ".body");
    const fs::path meta_file = settings_.cache_dir / (CacheKey(url) + ".json");
    if (cache_seconds > 0 && !settings_.cache_dir.empty()) {
        const auto meta_text = ReadTextFile(meta_file);
        const auto meta = meta_text ? ParseJson(*meta_text) : std::nullopt;
        const JsonValue* stored = meta ? meta->Find("stored") : nullptr;
        if (stored != nullptr && meta->String("url") == url &&
            settings_.now() - static_cast<std::int64_t>(stored->number) < cache_seconds) {
            if (auto body = ReadTextFile(body_file)) {
                answer.status = 200;
                answer.body = std::move(*body);
                answer.cached = true;
                return answer;
            }
        }
    }
    const std::string host = ParseUrl(url).host;
    if (const Status entered = Enter(host); !entered.ok) {
        answer.error = entered.error;
        return answer;
    }
    pal::HttpRequest request;
    request.url = url;
    request.headers = Headers(headers);
    request.max_body = MAX_HTTP_BODY;
    request.progress = [this](std::uint64_t received, std::uint64_t) {
        return !Cancelled() && received <= MAX_HTTP_BODY;
    };
    const pal::HttpResponse response = settings_.http(request);
    Leave(host, response.status);
    if (response.status == 0 || (!response.error.empty() && response.body.empty())) {
        answer.error = response.error.empty() ? "no answer from " + host : response.error;
        return answer;
    }
    if (response.body.size() > MAX_HTTP_BODY || response.received > MAX_HTTP_BODY) {
        answer.error = "the answer is larger than 8 MiB";
        return answer;
    }
    answer.status = response.status;
    answer.body = response.body;
    if (response.status == 200 && cache_seconds > 0 && !settings_.cache_dir.empty()) {
        std::error_code ec;
        fs::create_directories(settings_.cache_dir, ec);
        if (WriteFileAtomic(body_file, answer.body)) {
            WriteFileAtomic(
                meta_file,
                WriteJson(JsonObject({
                    {"url", JsonString(url)},
                    {"stored", JsonNumber(static_cast<double>(settings_.now()))},
                })));
        }
    }
    return answer;
}

Status HttpPolicy::Download(const std::string& url, const fs::path& destination,
                            std::uint64_t max_size, const std::function<void(float)>& progress) {
    if (const Status allowed = Allowed(url); !allowed.ok) {
        return allowed;
    }
    const std::string host = ParseUrl(url).host;
    if (const Status entered = Enter(host); !entered.ok) {
        return entered;
    }
    bool too_large = false;
    pal::HttpRequest request;
    request.url = url;
    request.headers = Headers({});
    request.to_file = destination;
    request.timeout_seconds = 120;
    request.progress = [&](std::uint64_t received, std::uint64_t total) {
        if (received > max_size || total > max_size) {
            too_large = true;
            return false;
        }
        if (progress) {
            progress(total > 0 ? static_cast<float>(received) / static_cast<float>(total)
                               : -1.0f);
        }
        return !Cancelled();
    };
    const pal::HttpResponse response = settings_.http(request);
    Leave(host, response.status);
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(destination, ec);
    if (too_large || (!ec && size > max_size)) {
        fs::remove(destination, ec);
        return Status::Fail("the file is larger than the plugin may download");
    }
    if (!response.ok()) {
        fs::remove(destination, ec);
        if (Cancelled()) {
            return Status::Fail("cancelled");
        }
        return Status::Fail(response.status != 0
                                ? "the server answered " + std::to_string(response.status)
                                : (response.error.empty() ? "no answer from " + host
                                                          : response.error));
    }
    return {};
}

} // namespace den::plugins::feature
