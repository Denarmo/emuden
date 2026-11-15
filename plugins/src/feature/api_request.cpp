// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.http.request: any method, a body from memory, from a file (or a slice of one) or built
// as multipart, the answer in memory or in a file, its headers.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
#include <system_error>

#include "den/base/files.h"
#include "feature/runner.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t MAX_LENT_FILE = 64ull << 30; // files the sync engine lends: games too
constexpr std::size_t MAX_PARTS = 16;

std::atomic<int> g_next_body{1};

std::string Upper(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

bool Network(const FeatureAccess& access) {
    return access.Has(PERMISSION_HTTP) || access.Has(PERMISSION_HTTP_USER_HOST) ||
           access.Has(PERMISSION_HTTP_LOCAL);
}

std::uint64_t FieldNumber(lua_State* L, int table, const char* key) {
    lua_getfield(L, table, key);
    const lua_Integer value = luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    return value < 0 ? 0 : static_cast<std::uint64_t>(value);
}

std::vector<std::pair<std::string, std::string>> FieldHeaders(lua_State* L, int table) {
    std::vector<std::pair<std::string, std::string>> headers;
    lua_getfield(L, table, "headers");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING && headers.size() < 32) {
                headers.emplace_back(lua_tostring(L, -2), lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return headers;
}

bool PlainHeaderText(const std::string& text) {
    return text.find_first_of("\r\n\"") == std::string::npos;
}

struct Part {
    std::string name, value, filename, content_type;
    std::vector<std::pair<std::string, std::string>> headers;
    fs::path file;
};

// The multipart body as a file (it may hold whole files). Returns its content type.
Result<std::string> WriteMultipart(const std::vector<Part>& parts, const std::string& kind,
                                   const fs::path& out) {
    std::random_device random;
    char boundary[33];
    for (int i = 0; i < 32; ++i) {
        boundary[i] = "0123456789abcdef"[random() % 16];
    }
    boundary[32] = 0;
    const std::string mark = std::string("emuden-") + boundary;
    std::ofstream body(out, std::ios::binary | std::ios::trunc);
    for (const Part& part : parts) {
        body << "--" << mark << "\r\n";
        if (!part.name.empty()) {
            body << "Content-Disposition: form-data; name=\"" << part.name << "\"";
            if (!part.filename.empty()) {
                body << "; filename=\"" << part.filename << "\"";
            }
            body << "\r\n";
        }
        if (!part.content_type.empty()) {
            body << "Content-Type: " << part.content_type << "\r\n";
        }
        for (const auto& [name, value] : part.headers) {
            body << name << ": " << value << "\r\n";
        }
        body << "\r\n";
        if (!part.file.empty()) {
            std::ifstream in(part.file, std::ios::binary);
            if (!in) {
                return Status::Fail("a part's file could not be read");
            }
            body << in.rdbuf();
        } else {
            body << part.value;
        }
        body << "\r\n";
    }
    body << "--" << mark << "--\r\n";
    if (!body) {
        return Status::Fail("the request could not be written");
    }
    return "multipart/" + kind + "; boundary=" + mark;
}

// Reads the multipart list at the top of the stack.
Status ReadParts(lua_State* L, Runner& runner, std::vector<Part>& parts) {
    const int list = lua_gettop(L);
    for (lua_Integer i = 1;; ++i) {
        lua_rawgeti(L, list, i);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        if (!lua_istable(L, -1) || parts.size() >= MAX_PARTS) {
            lua_pop(L, 1);
            return Status::Fail("multipart is a list of at most 16 parts");
        }
        const int at = lua_gettop(L);
        Part part;
        part.name = FieldText(L, at, "name");
        part.value = FieldText(L, at, "value");
        if (part.value.empty()) {
            part.value = FieldText(L, at, "body");
        }
        part.filename = FieldText(L, at, "filename");
        part.content_type = FieldText(L, at, "content_type");
        part.headers = FieldHeaders(L, at);
        std::string file = FieldText(L, at, "file");
        if (file.empty()) {
            file = FieldText(L, at, "body_file");
        }
        lua_pop(L, 1);
        if (!file.empty()) {
            bool lent = false;
            if (const Status found = runner.ResolveFile(file, part.file, lent); !found.ok) {
                return found;
            }
        }
        bool plain = PlainHeaderText(part.name) && PlainHeaderText(part.filename) &&
                     PlainHeaderText(part.content_type);
        for (const auto& [name, value] : part.headers) {
            plain = plain && PlainHeaderText(name) && name.find(':') == std::string::npos &&
                    value.find_first_of("\r\n") == std::string::npos;
        }
        if (!plain) {
            return Status::Fail("a part has a name or header that cannot be sent");
        }
        parts.push_back(std::move(part));
    }
    return {};
}

int Request(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    if (!Network(runner.Config().manifest.access)) {
        return Refuse(L, "the plugin has no permission to connect");
    }
    pal::HttpRequest request;
    request.url = FieldText(L, 1, "url");
    if (const auto route = runner.Http().AllowedRequest(request.url); !route) {
        return Refuse(L, route.error());
    }
    request.method = Upper(FieldText(L, 1, "method"));
    if (request.method.empty()) {
        request.method = "GET";
    }
    const char* const methods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"};
    if (std::none_of(std::begin(methods), std::end(methods),
                     [&](const char* m) { return request.method == m; })) {
        return Refuse(L, "the method must be GET, POST, PUT, PATCH, DELETE or HEAD");
    }
    request.headers = FieldHeaders(L, 1);
    request.body = FieldText(L, 1, "body");
    if (request.body.size() > MAX_HTTP_BODY) {
        return Refuse(L, "the body is larger than 8 MiB");
    }
    const std::string body_file = FieldText(L, 1, "body_file");
    if (!body_file.empty()) {
        bool lent = false;
        if (const Status found = runner.ResolveFile(body_file, request.body_file, lent); !found.ok) {
            return Refuse(L, found.error);
        }
        std::error_code ec;
        if (!fs::is_regular_file(request.body_file, ec)) {
            return Refuse(L, "body_file is not a file");
        }
        request.body_offset = FieldNumber(L, 1, "body_offset");
        request.body_length = FieldNumber(L, 1, "body_length");
    }
    std::vector<Part> parts;
    lua_getfield(L, 1, "multipart");
    if (lua_istable(L, -1)) {
        if (const Status read = ReadParts(L, runner, parts); !read.ok) {
            return Refuse(L, read.error);
        }
    }
    lua_pop(L, 1);
    std::string kind = FieldText(L, 1, "multipart_type");
    kind = kind.empty() ? "form-data" : kind;
    if (kind != "form-data" && kind != "related" && kind != "mixed") {
        return Refuse(L, "multipart_type is form-data, related or mixed");
    }
    if (static_cast<int>(!request.body.empty()) + static_cast<int>(!body_file.empty()) +
            static_cast<int>(!parts.empty()) > 1) {
        return Refuse(L, "give one of body, body_file and multipart");
    }
    std::uint64_t max_file = MAX_LENT_FILE;
    const std::string to_file = FieldText(L, 1, "to_file");
    if (!to_file.empty()) {
        bool lent = false;
        if (const Status found = runner.ResolveFile(to_file, request.to_file, lent); !found.ok) {
            return Refuse(L, found.error);
        }
        if (!lent) {
            max_file = std::min<std::uint64_t>(MAX_DOWNLOAD, runner.StorageLeft());
            if (max_file == 0) {
                return Refuse(L, "the plugin's folder is full");
            }
        }
    }
    request.timeout_seconds = to_file.empty() && body_file.empty() && parts.empty() ? 60 : 300;
    int progress_ref = LUA_NOREF;
    lua_getfield(L, 1, "on_progress");
    if (lua_isfunction(L, -1)) {
        progress_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }
    const fs::path temp_dir = runner.Config().data_dir / "cache" / "requests";
    return runner.Suspend(L, [&runner, request = std::move(request), parts = std::move(parts), kind,
                              max_file, progress_ref, temp_dir]() mutable -> Outcome {
        fs::path built;
        std::error_code ec;
        if (!parts.empty()) {
            fs::create_directories(temp_dir, ec);
            built = temp_dir / ("multipart-" + std::to_string(g_next_body.fetch_add(1)) + ".tmp");
            const Result<std::string> type = WriteMultipart(parts, kind, built);
            if (!type) {
                fs::remove(built, ec);
                return [error = type.error()](lua_State* T) { return Refuse(T, error); };
            }
            std::erase_if(request.headers, [](const auto& h) { return EqualsNoCase(h.first, "content-type"); });
            request.headers.emplace_back("Content-Type", type.value());
            request.body_file = built;
        }
        if (!request.to_file.empty()) {
            fs::create_directories(request.to_file.parent_path(), ec);
        }
        auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        HttpAnswer answer = runner.Http().Request(std::move(request), max_file, [&](float fraction) {
            const auto now = std::chrono::steady_clock::now();
            if (progress_ref != LUA_NOREF && now - last >= std::chrono::milliseconds(100)) {
                last = now;
                runner.CallProgress(progress_ref, fraction);
            }
        });
        if (!built.empty()) {
            fs::remove(built, ec);
        }
        if (progress_ref != LUA_NOREF) {
            runner.ReleaseRef(progress_ref);
        }
        return [answer = std::move(answer)](lua_State* T) {
            if (!answer.error.empty()) {
                return Refuse(T, answer.error);
            }
            lua_createtable(T, 0, 3);
            lua_pushinteger(T, answer.status);
            lua_setfield(T, -2, "status");
            lua_pushlstring(T, answer.body.data(), answer.body.size());
            lua_setfield(T, -2, "body");
            lua_newtable(T);
            for (const auto& [name, value] : answer.headers) {
                lua_getfield(T, -1, name.c_str());
                if (lua_type(T, -1) == LUA_TSTRING) {
                    const std::string joined = std::string(lua_tostring(T, -1)) + ", " + value;
                    lua_pop(T, 1);
                    lua_pushlstring(T, joined.data(), joined.size());
                } else {
                    lua_pop(T, 1);
                    lua_pushlstring(T, value.data(), value.size());
                }
                lua_setfield(T, -2, name.c_str());
            }
            lua_setfield(T, -2, "headers");
            return 1;
        };
    });
}

} // namespace

void RegisterRequest(lua_State* L, Runner& runner) {
    lua_getfield(L, -1, "http");
    SetFunction(L, runner, "request", &Request);
    lua_pop(L, 1);
}

} // namespace den::plugins::feature
