// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The only way a plugin reaches the network: https to the hosts its manifest declares, two
// requests in flight at most, a pause between requests to one host, a stop after 429 and a
// cache of answers in the plugin's folder.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "den/base/result.h"
#include "den/plugins/installer.h"

namespace den::plugins::feature {

inline constexpr std::uint64_t MAX_HTTP_BODY = 8ull << 20;
inline constexpr std::uint64_t MAX_DOWNLOAD = 256ull << 20;
/// For a plugin with game.files (den.mods): the releases it sets up are larger.
inline constexpr std::uint64_t MAX_MOD_DOWNLOAD = 512ull << 20;
inline constexpr int BLOCKED_AFTER_429_SECONDS = 15 * 60;

struct UrlParts {
    bool valid = false;
    std::string scheme; // lower case
    std::string host;   // lower case
    int port = 0;       // 0 = the scheme's own
    std::string path;   // with the query, "/" when the URL has none
};

UrlParts ParseUrl(std::string_view url);

/// Fine when `url` is https on its default port to one of `hosts`, without a user name.
Status CheckUrl(std::string_view url, const std::vector<std::string>& hosts);

/// Who an address of den.http.request belongs to.
enum class Route { Declared, UserServer, Local };

struct HttpSettings {
    HttpGetter http;
    std::vector<std::string> hosts;
    bool local = false; // http.local
    /// http.user_host: the server the user confirmed, "scheme://host[:port]" (empty: none).
    std::function<std::string()> user_server;
    int min_interval_ms = 500;
    std::filesystem::path cache_dir;
    std::string user_agent;
    std::function<std::int64_t()> now; // Unix seconds
    const std::atomic<bool>* cancel = nullptr;
};

struct HttpAnswer {
    int status = 0;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers; // names in lower case
    std::string error; // set when there is no usable answer
    bool cached = false;
};

class HttpPolicy {
public:
    explicit HttpPolicy(HttpSettings settings);

    Status Allowed(std::string_view url) const {
        return CheckUrl(url, settings_.hosts);
    }

    /// Blocking. `cache_seconds` 0 asks the server every time.
    HttpAnswer Get(const std::string& url,
                   const std::vector<std::pair<std::string, std::string>>& headers,
                   int cache_seconds);

    /// Where den.http.request may send `url`: a declared https host, the user's server or an
    /// app on this device.
    Result<Route> AllowedRequest(std::string_view url) const;

    /// Blocking. The request as the script made it (url, method, headers, body); the policy
    /// adds the user agent and limits. `max_file` bounds `to_file`.
    HttpAnswer Request(pal::HttpRequest request, std::uint64_t max_file,
                       const std::function<void(float)>& progress);

    /// Blocking. Streams to `destination`; `progress` gets 0..1 (negative: size unknown).
    Status Download(const std::string& url, const std::filesystem::path& destination,
                    std::uint64_t max_size, const std::function<void(float)>& progress);

private:
    // Waits for a free slot and the host's pause. Fails when cancelled or the host is blocked.
    Status Enter(const std::string& host, int interval_ms);
    void Leave(const std::string& host, int status, int interval_ms);
    std::vector<std::pair<std::string, std::string>> Headers(
        const std::vector<std::pair<std::string, std::string>>& extra,
        bool authorization = false) const;
    bool Cancelled() const {
        return settings_.cancel != nullptr && settings_.cancel->load();
    }

    HttpSettings settings_;
    std::mutex mutex_;
    std::condition_variable changed_;
    int in_flight_ = 0;
    std::map<std::string, std::chrono::steady_clock::time_point> next_;
    std::map<std::string, std::int64_t> blocked_until_;
};

} // namespace den::plugins::feature
