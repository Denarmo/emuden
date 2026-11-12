// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// HTTP for Linux through libcurl. Without libcurl (and on Android until the shell provides
// platform HTTP) requests fail with a clear error.

#include <fstream>
#include <system_error>

#include "den/platform/pal.h"

#ifdef DEN_HAVE_CURL
#include <curl/curl.h>
#endif

namespace den::pal {

#ifdef DEN_HAVE_CURL
namespace {

struct Transfer {
    const HttpRequest* request;
    HttpResponse* response;
    std::ofstream file;
    bool to_file = false;
};

size_t OnData(char* data, size_t size, size_t count, void* user) {
    auto& t = *static_cast<Transfer*>(user);
    const size_t bytes = size * count;
    t.response->received += bytes;
    if (t.to_file) {
        t.file.write(data, static_cast<std::streamsize>(bytes));
        return t.file ? bytes : 0;
    }
    if (t.response->body.size() + bytes > t.request->max_body) {
        t.response->error = "the answer is larger than allowed";
        return 0;
    }
    t.response->body.append(data, bytes);
    return bytes;
}

int OnProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto& t = *static_cast<Transfer*>(user);
    if (t.request->progress &&
        !t.request->progress(static_cast<std::uint64_t>(now), static_cast<std::uint64_t>(total))) {
        t.response->error = "cancelled";
        return 1;
    }
    return 0;
}

} // namespace

HttpResponse HttpGet(const HttpRequest& request) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl could not start";
        return response;
    }
    Transfer transfer{&request, &response, {}, !request.to_file.empty()};
    std::filesystem::path part = request.to_file;
    part += ".part";
    if (transfer.to_file) {
        transfer.file.open(part, std::ios::binary | std::ios::trunc);
    }
    curl_slist* headers = nullptr;
    for (const auto& [name, value] : request.headers) {
        headers = curl_slist_append(headers, (name + ": " + value).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "EmuDEN");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(request.timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.status = static_cast<int>(status);
    if (code != CURLE_OK && response.error.empty()) {
        response.error = curl_easy_strerror(code);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (transfer.to_file) {
        transfer.file.close();
        std::error_code ec;
        if (response.ok()) {
            std::filesystem::rename(part, request.to_file, ec);
        } else {
            std::filesystem::remove(part, ec);
        }
    }
    return response;
}
#else
HttpResponse HttpGet(const HttpRequest&) {
    HttpResponse response;
    response.error = "this build has no HTTP client";
    return response;
}
#endif

} // namespace den::pal
