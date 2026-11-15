// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

#include "den/platform/pal.h"

namespace den::pal {

namespace {

std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr,
                        nullptr);
    return out;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) {
            WinHttpCloseHandle(h);
        }
    }
};

HttpResponse Fail(const char* what) {
    HttpResponse response;
    response.error = std::string(what) + " failed (" + std::to_string(GetLastError()) + ")";
    return response;
}

// The raw header block after the status line: "Name: value" lines.
void ReadHeaders(HINTERNET call, HttpResponse& response) {
    DWORD size = 0;
    WinHttpQueryHeaders(call, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (size == 0) {
        return;
    }
    std::wstring raw(size / sizeof(wchar_t) + 1, L'\0');
    if (!WinHttpQueryHeaders(call, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(),
                             &size, WINHTTP_NO_HEADER_INDEX)) {
        return;
    }
    raw.resize(size / sizeof(wchar_t));
    const std::string text = Narrow(raw);
    std::size_t at = text.find("\r\n"); // skips the status line
    while (at != std::string::npos && at + 2 < text.size()) {
        const std::size_t start = at + 2;
        at = text.find("\r\n", start);
        const std::string line = text.substr(start, at == std::string::npos ? std::string::npos : at - start);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) {
            continue;
        }
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string value = line.substr(colon + 1);
        const std::size_t first = value.find_first_not_of(' ');
        value = first == std::string::npos ? std::string() : value.substr(first);
        response.headers.emplace_back(std::move(name), std::move(value));
    }
}

// Sends the request with its body (from memory or streamed from a file).
bool Send(HINTERNET call, const HttpRequest& request, std::wstring headers, HttpResponse& response) {
    std::ifstream file;
    const std::uint64_t length = HttpBodySize(request);
    if (!request.body_file.empty()) {
        file.open(request.body_file, std::ios::binary);
        file.seekg(static_cast<std::streamoff>(request.body_offset));
        if (!file) {
            response.error = "could not read " + request.body_file.string();
            return false;
        }
    }
    DWORD total = static_cast<DWORD>(length);
    if (length > 0xFFFFFFFFull) {
        headers += L"Content-Length: " + std::to_wstring(length) + L"\r\n";
        total = WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH;
    }
    const bool streamed = !request.body_file.empty();
    LPVOID optional = !streamed && !request.body.empty() ? const_cast<char*>(request.body.data())
                                                           : WINHTTP_NO_REQUEST_DATA;
    const DWORD optional_size = !streamed ? static_cast<DWORD>(request.body.size()) : 0;
    if (!WinHttpSendRequest(call, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            static_cast<DWORD>(-1), optional, optional_size, total, 0)) {
        response = Fail("the request");
        return false;
    }
    if (streamed) {
        std::string buffer(1u << 16, '\0');
        std::uint64_t left = length;
        while (file && left > 0) {
            file.read(buffer.data(), static_cast<std::streamsize>(std::min<std::uint64_t>(buffer.size(), left)));
            const auto got = static_cast<DWORD>(file.gcount());
            if (got == 0) {
                break;
            }
            left -= got;
            DWORD written = 0;
            if (got > 0 && !WinHttpWriteData(call, buffer.data(), got, &written)) {
                response = Fail("sending the file");
                return false;
            }
        }
    }
    if (!WinHttpReceiveResponse(call, nullptr)) {
        response = Fail("the request");
        return false;
    }
    return true;
}

} // namespace

HttpResponse HttpGet(const HttpRequest& request) {
    const std::wstring url = Widen(request.url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256];
    wchar_t path[4096];
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        return Fail("parsing the address");
    }
    const bool https = parts.nScheme == INTERNET_SCHEME_HTTPS;

    Handle session;
    session.h = WinHttpOpen(L"EmuDEN", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) {
        return Fail("WinHttpOpen");
    }
    const int ms = request.timeout_seconds * 1000;
    WinHttpSetTimeouts(session.h, ms, ms, ms, ms);
    Handle connect;
    connect.h = WinHttpConnect(session.h, host, parts.nPort, 0);
    if (!connect.h) {
        return Fail("connecting");
    }
    const std::wstring method = Widen(request.method.empty() ? "GET" : request.method);
    Handle call;
    call.h = WinHttpOpenRequest(connect.h, method.c_str(), path, nullptr, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!call.h) {
        return Fail("opening the request");
    }
    DWORD redirect = request.follow_redirects ? WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS
                                              : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(call.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    if (https && request.loopback_any_certificate && IsLoopbackUrl(request.url)) {
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(call.h, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    }
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(call.h, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));

    std::wstring headers;
    for (const auto& [name, value] : request.headers) {
        headers += Widen(name) + L": " + Widen(value) + L"\r\n";
    }
    HttpResponse response;
    if (!Send(call.h, request, std::move(headers), response)) {
        return response;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(call.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    response.status = static_cast<int>(status);
    ReadHeaders(call.h, response);
    std::uint64_t total = 0;
    wchar_t length[32];
    size = sizeof(length);
    if (WinHttpQueryHeaders(call.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            length, &size, WINHTTP_NO_HEADER_INDEX)) {
        total = _wcstoui64(length, nullptr, 10);
    }

    const bool to_file = !request.to_file.empty() && response.status >= 200 && response.status < 300;
    std::filesystem::path part = request.to_file;
    part += ".part";
    std::ofstream file;
    if (to_file) {
        file.open(part, std::ios::binary | std::ios::trunc);
        if (!file) {
            response.error = "could not write " + part.string();
            return response;
        }
    }
    std::string buffer(1u << 16, '\0');
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(call.h, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            response.error = "reading the answer failed (" + std::to_string(GetLastError()) + ")";
            break;
        }
        if (read == 0) {
            break;
        }
        response.received += read;
        if (to_file) {
            file.write(buffer.data(), read);
            if (!file) {
                response.error = "could not write " + part.string();
                break;
            }
        } else if (response.body.size() + read > request.max_body) {
            response.error = "the answer is larger than allowed";
            break;
        } else {
            response.body.append(buffer.data(), read);
        }
        if (request.progress && !request.progress(response.received, total)) {
            response.error = "cancelled";
            break;
        }
    }
    if (to_file) {
        file.close();
        std::error_code ec;
        if (response.error.empty()) {
            std::filesystem::rename(part, request.to_file, ec);
            if (ec) {
                response.error = "could not rename " + part.string();
            }
        }
        if (!response.error.empty()) {
            std::filesystem::remove(part, ec);
        }
    }
    return response;
}

} // namespace den::pal
