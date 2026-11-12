// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
#include <winhttp.h>

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
    Handle call;
    call.h = WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!call.h) {
        return Fail("opening the request");
    }
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(call.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(call.h, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));

    std::wstring headers;
    for (const auto& [name, value] : request.headers) {
        headers += Widen(name) + L": " + Widen(value) + L"\r\n";
    }
    if (!WinHttpSendRequest(call.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(call.h, nullptr)) {
        return Fail("the request");
    }

    HttpResponse response;
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(call.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    response.status = static_cast<int>(status);
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
