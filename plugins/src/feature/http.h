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

struct HttpSettings {
    HttpGetter http;
    std::vector<std::string> hosts;
    int min_interval_ms = 500;
    std::filesystem::path cache_dir;
    std::string user_agent;
    std::function<std::int64_t()> now; // Unix seconds
    const std::atomic<bool>* cancel = nullptr;
};

struct HttpAnswer {
    int status = 0;
    std::string body;
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

    /// Blocking. Streams to `destination`; `progress` gets 0..1 (negative: size unknown).
    Status Download(const std::string& url, const std::filesystem::path& destination,
                    std::uint64_t max_size, const std::function<void(float)>& progress);

private:
    // Waits for a free slot and the host's pause. Fails when cancelled or the host is blocked.
    Status Enter(const std::string& host);
    void Leave(const std::string& host, int status);
    std::vector<std::pair<std::string, std::string>> Headers(
        const std::vector<std::pair<std::string, std::string>>& extra) const;
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
