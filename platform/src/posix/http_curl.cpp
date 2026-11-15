// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// HTTP for Linux through libcurl. The library is loaded at run time, so one binary starts on
// any distribution whichever TLS flavour of libcurl it ships; without it requests fail with a
// clear error.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <system_error>

#include "den/platform/pal.h"

#ifdef DEN_HAVE_CURL
#include <curl/curl.h>
#include <dlfcn.h>
#endif

namespace den::pal {

#ifdef DEN_HAVE_CURL
namespace {

struct Curl {
    decltype(&curl_easy_init) init = nullptr;
    decltype(&curl_easy_setopt) setopt = nullptr;
    decltype(&curl_easy_perform) perform = nullptr;
    decltype(&curl_easy_getinfo) getinfo = nullptr;
    decltype(&curl_easy_cleanup) cleanup = nullptr;
    decltype(&curl_easy_strerror) strerror = nullptr;
    decltype(&curl_slist_append) slist_append = nullptr;
    decltype(&curl_slist_free_all) slist_free_all = nullptr;
    bool ok = false;
};

template <typename Fn>
bool Bind(void* lib, const char* name, Fn& fn) {
    fn = reinterpret_cast<Fn>(dlsym(lib, name));
    return fn != nullptr;
}

const Curl& GetCurl() {
    static const Curl curl = [] {
        Curl c;
        void* lib = nullptr;
        for (const char* name : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"}) {
            if ((lib = dlopen(name, RTLD_NOW | RTLD_GLOBAL)) != nullptr) {
                break;
            }
        }
        if (!lib) {
            return c;
        }
        auto global_init = reinterpret_cast<decltype(&curl_global_init)>(dlsym(lib, "curl_global_init"));
        c.ok = global_init && Bind(lib, "curl_easy_init", c.init) &&
               Bind(lib, "curl_easy_setopt", c.setopt) && Bind(lib, "curl_easy_perform", c.perform) &&
               Bind(lib, "curl_easy_getinfo", c.getinfo) && Bind(lib, "curl_easy_cleanup", c.cleanup) &&
               Bind(lib, "curl_easy_strerror", c.strerror) &&
               Bind(lib, "curl_slist_append", c.slist_append) &&
               Bind(lib, "curl_slist_free_all", c.slist_free_all) &&
               global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
        return c;
    }();
    return curl;
}

struct Transfer {
    const HttpRequest* request;
    HttpResponse* response;
    std::ofstream file;
    bool to_file = false;
    std::ifstream upload;   // body_file
    std::uint64_t left = 0; // of its slice
    std::size_t sent = 0;   // of an in-memory body
};

size_t OnRead(char* data, size_t size, size_t count, void* user) {
    auto& t = *static_cast<Transfer*>(user);
    const size_t room = size * count;
    if (!t.request->body_file.empty()) {
        t.upload.read(data, static_cast<std::streamsize>(std::min<std::uint64_t>(room, t.left)));
        const auto got = static_cast<size_t>(t.upload.gcount());
        t.left -= got;
        return got;
    }
    const size_t n = std::min(room, t.request->body.size() - t.sent);
    std::memcpy(data, t.request->body.data() + t.sent, n);
    t.sent += n;
    return n;
}

// One header line; a status line starts the headers of the next answer (redirects).
size_t OnHeader(char* data, size_t size, size_t count, void* user) {
    auto& t = *static_cast<Transfer*>(user);
    const size_t bytes = size * count;
    std::string line(data, bytes);
    while (!line.empty() && (line.back() == '
' || line.back() == '
')) {
        line.pop_back();
    }
    if (line.rfind("HTTP/", 0) == 0) {
        t.response->headers.clear();
        return bytes;
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) {
        return bytes;
    }
    std::string name = line.substr(0, colon);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string value = line.substr(colon + 1);
    const std::size_t first = value.find_first_not_of(' ');
    value = first == std::string::npos ? std::string() : value.substr(first);
    t.response->headers.emplace_back(std::move(name), std::move(value));
    return bytes;
}

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
    const Curl& api = GetCurl();
    if (!api.ok) {
        response.error = "libcurl is not installed";
        return response;
    }
    CURL* curl = api.init();
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
        headers = api.slist_append(headers, (name + ": " + value).c_str());
    }
    const std::string method = request.method.empty() ? "GET" : request.method;
    const bool has_body = !request.body.empty() || !request.body_file.empty() ||
                          (method != "GET" && method != "HEAD" && method != "DELETE");
    if (has_body) {
        const std::uint64_t length = HttpBodySize(request);
        transfer.left = length;
        if (!request.body_file.empty()) {
            transfer.upload.open(request.body_file, std::ios::binary);
            transfer.upload.seekg(static_cast<std::streamoff>(request.body_offset));
            if (!transfer.upload) {
                response.error = "could not read " + request.body_file.string();
                api.slist_free_all(headers);
                api.cleanup(curl);
                return response;
            }
        }
        headers = api.slist_append(headers, "Expect:");
        api.setopt(curl, CURLOPT_POST, 1L);
        api.setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(length));
        api.setopt(curl, CURLOPT_READFUNCTION, OnRead);
        api.setopt(curl, CURLOPT_READDATA, &transfer);
    }
    if (method == "HEAD") {
        api.setopt(curl, CURLOPT_NOBODY, 1L);
    } else if (method != "GET" && method != "POST") {
        api.setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    }
    api.setopt(curl, CURLOPT_HEADERFUNCTION, OnHeader);
    api.setopt(curl, CURLOPT_HEADERDATA, &transfer);
    api.setopt(curl, CURLOPT_URL, request.url.c_str());
    api.setopt(curl, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
    if (request.loopback_any_certificate && IsLoopbackUrl(request.url)) {
        api.setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        api.setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    api.setopt(curl, CURLOPT_MAXREDIRS, 10L);
    api.setopt(curl, CURLOPT_USERAGENT, "EmuDEN");
    api.setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    api.setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(request.timeout_seconds));
    api.setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    api.setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    api.setopt(curl, CURLOPT_NOSIGNAL, 1L);
    api.setopt(curl, CURLOPT_HTTPHEADER, headers);
    api.setopt(curl, CURLOPT_WRITEFUNCTION, OnData);
    api.setopt(curl, CURLOPT_WRITEDATA, &transfer);
    api.setopt(curl, CURLOPT_NOPROGRESS, 0L);
    api.setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
    api.setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    const CURLcode code = api.perform(curl);
    long status = 0;
    api.getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.status = static_cast<int>(status);
    if (code != CURLE_OK && response.error.empty()) {
        response.error = api.strerror(code);
    }
    api.slist_free_all(headers);
    api.cleanup(curl);
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
