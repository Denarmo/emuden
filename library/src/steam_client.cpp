// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The running Steam client over its local DevTools port: a minimal WebSocket client on a
// loopback stream that evaluates one expression in SharedJSContext, where SteamClient lives.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/library/steam.h"
#include "den/platform/pal.h"
#include "den/platform/stream.h"

namespace den::library::steam {

namespace {

constexpr int TIMEOUT_SECONDS = 8;

// Closes the stream when the call takes too long, so a blocked Read returns.
class Watchdog {
public:
    explicit Watchdog(pal::Stream& stream)
        : thread_([this, &stream] {
              std::unique_lock lock(mutex_);
              if (!wake_.wait_for(lock, std::chrono::seconds(TIMEOUT_SECONDS), [this] { return done_; })) {
                  stream.Close();
              }
          }) {}
    ~Watchdog() {
        {
            std::lock_guard lock(mutex_);
            done_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }

private:
    std::mutex mutex_;
    std::condition_variable wake_;
    bool done_ = false;
    std::thread thread_;
};

bool SendText(pal::Stream& stream, const std::string& text) {
    std::string frame;
    frame.push_back(static_cast<char>(0x81));
    const std::uint64_t size = text.size();
    if (size < 126) {
        frame.push_back(static_cast<char>(0x80 | size));
    } else if (size < 65536) {
        frame.push_back(static_cast<char>(0x80 | 126));
        frame.push_back(static_cast<char>(size >> 8));
        frame.push_back(static_cast<char>(size & 0xff));
    } else {
        frame.push_back(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<char>((size >> shift) & 0xff));
        }
    }
    std::random_device random;
    char mask[4];
    for (char& m : mask) {
        m = static_cast<char>(random() & 0xff);
    }
    frame.append(mask, 4);
    for (std::size_t i = 0; i < text.size(); ++i) {
        frame.push_back(static_cast<char>(text[i] ^ mask[i % 4]));
    }
    return stream.Write(frame.data(), frame.size());
}

// One message (text frames, continuations joined); control frames are skipped.
std::optional<std::string> ReadMessage(pal::Stream& stream) {
    std::string message;
    while (true) {
        unsigned char head[2];
        if (!stream.Read(head, 2)) {
            return std::nullopt;
        }
        std::uint64_t size = head[1] & 0x7f;
        if (size >= 126) {
            unsigned char extended[8];
            const int count = size == 126 ? 2 : 8;
            if (!stream.Read(extended, static_cast<std::size_t>(count))) {
                return std::nullopt;
            }
            size = 0;
            for (int i = 0; i < count; ++i) {
                size = (size << 8) | extended[i];
            }
        }
        if (size > (16u << 20)) {
            return std::nullopt;
        }
        std::string payload(size, '\0');
        if (size > 0 && !stream.Read(payload.data(), size)) {
            return std::nullopt;
        }
        const int opcode = head[0] & 0x0f;
        if (opcode == 0x8) {
            return std::nullopt;
        }
        if (opcode == 0x1 || opcode == 0x0) {
            message += payload;
            if ((head[0] & 0x80) != 0) {
                return message;
            }
        }
    }
}

Result<std::string> EvaluateOnPort(int port, const std::string& script) {
    pal::HttpRequest list;
    list.url = "http://127.0.0.1:" + std::to_string(port) + "/json";
    list.timeout_seconds = 5;
    const pal::HttpResponse targets = pal::HttpGet(list);
    if (!targets.ok()) {
        return Status::Fail("Steam is not running, or its local debugging port is off");
    }
    const auto parsed = ParseJson(targets.body);
    std::string path;
    for (const JsonValue& target : parsed ? parsed->items : std::vector<JsonValue>{}) {
        if (target.String("title") == "SharedJSContext") {
            const std::string url = target.String("webSocketDebuggerUrl");
            const std::size_t slash = url.find("/devtools/");
            path = slash == std::string::npos ? std::string() : url.substr(slash);
        }
    }
    if (path.empty()) {
        return Status::Fail("Steam's client page was not found");
    }
    auto connected = pal::ConnectLoopback(port);
    if (!connected) {
        return connected.status();
    }
    pal::Stream& stream = *connected.value();
    Watchdog watchdog(stream);
    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                                "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                "Sec-WebSocket-Key: ZW11ZGVuLXN0ZWFtLWtleQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if (!stream.Write(request.data(), request.size())) {
        return Status::Fail("Steam closed the connection");
    }
    std::string answer;
    while (answer.find("\r\n\r\n") == std::string::npos) {
        char c = 0;
        if (answer.size() > 8192 || !stream.Read(&c, 1)) {
            return Status::Fail("Steam did not answer");
        }
        answer.push_back(c);
    }
    if (answer.find(" 101 ") == std::string::npos) {
        return Status::Fail("Steam refused the connection");
    }
    const JsonValue call = JsonObject(
        {{"id", JsonNumber(1)},
         {"method", JsonString("Runtime.evaluate")},
         {"params", JsonObject({{"expression", JsonString(script)},
                                {"awaitPromise", JsonBool(true)},
                                {"returnByValue", JsonBool(true)}})}});
    if (!SendText(stream, WriteJson(call))) {
        return Status::Fail("Steam closed the connection");
    }
    while (true) {
        const auto message = ReadMessage(stream);
        if (!message) {
            return Status::Fail("Steam did not answer in time");
        }
        const auto reply = ParseJson(*message);
        const JsonValue* id = reply ? reply->Find("id") : nullptr;
        if (id == nullptr || id->number != 1) {
            continue;
        }
        const JsonValue* result = reply->Find("result");
        if (result != nullptr && result->Find("exceptionDetails") != nullptr) {
            const JsonValue* inner = result->Find("result");
            return Status::Fail("Steam: " + (inner ? inner->String("description") : std::string("error")));
        }
        const JsonValue* value = result ? result->Find("result") : nullptr;
        value = value ? value->Find("value") : nullptr;
        return value != nullptr && value->kind == JsonValue::Kind::String ? value->string : std::string();
    }
}

bool IsAppId(const std::string& appid) {
    return !appid.empty() && appid.size() <= 10 &&
           appid.find_first_not_of("0123456789") == std::string::npos;
}

bool IsShortcutId(const std::string& appid) {
    return IsAppId(appid) && std::stoull(appid) >= 0x80000000ull;
}

} // namespace

std::string JsString(std::string_view text) {
    // A JSON string is a JavaScript string literal, once U+2028 and U+2029 are escaped.
    std::string out = WriteJson(JsonString(std::string(text)));
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    for (const char* separator : {"\xE2\x80\xA8", "\xE2\x80\xA9"}) {
        for (std::size_t at = out.find(separator); at != std::string::npos; at = out.find(separator)) {
            out.replace(at, 3, separator[2] == '\xA8' ? "\\u2028" : "\\u2029");
        }
    }
    return out;
}

Client::Client(Evaluate evaluate) : evaluate_(std::move(evaluate)) {}

Client Client::Local(int port) {
    return Client([port](const std::string& script) { return EvaluateOnPort(port, script); });
}

bool Client::Enabled(const std::filesystem::path& root) {
    return IsRegularFile(root / ".cef-enable-remote-debugging");
}

Result<AppConfig> Client::Config(const std::string& appid) {
    if (!IsAppId(appid)) {
        return Status::Fail("not a Steam app id");
    }
    const std::string script =
        "new Promise((ok, no) => { const t = setTimeout(() => no('no answer'), 4000);"
        " const h = SteamClient.Apps.RegisterForAppDetails(" + appid + ", d => { clearTimeout(t); h.unregister();"
        " ok(JSON.stringify({launch: d.strLaunchOptions || '', tool: d.strCompatToolName || '',"
        " display: d.strCompatToolDisplayName || '', priority: d.nCompatToolPriority || 0})); }); })";
    const Result<std::string> text = evaluate_(script);
    if (!text) {
        return text.status();
    }
    const auto parsed = ParseJson(text.value());
    if (!parsed) {
        return Status::Fail("Steam gave no details for the app");
    }
    // Steam ranks a tool the user picked at 250; lower ones are its own choice for the game.
    const JsonValue* priority = parsed->Find("priority");
    return AppConfig{parsed->String("launch"), parsed->String("tool"), parsed->String("display"),
                     priority != nullptr && priority->number >= 250};
}

Status Client::Set(const std::string& appid, const std::optional<std::string>& launch_options,
                   const std::optional<std::string>& compat_tool) {
    if (!IsAppId(appid)) {
        return Status::Fail("not a Steam app id");
    }
    std::string script = "(() => {";
    if (launch_options) {
        if (const Status safe = CheckLaunchOptions(*launch_options); !safe.ok) {
            return safe;
        }
        script += std::string(IsShortcutId(appid) ? "SteamClient.Apps.SetShortcutLaunchOptions("
                                                  : "SteamClient.Apps.SetAppLaunchOptions(") +
                  appid + ", " + JsString(*launch_options) + ");";
    }
    if (compat_tool) {
        if (compat_tool->size() > 64 || compat_tool->find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos) {
            return Status::Fail("not a compatibility tool name");
        }
        script += "SteamClient.Apps.SpecifyCompatTool(" + appid + ", " + JsString(*compat_tool) + ");";
    }
    script += "return 'ok'; })()";
    const Result<std::string> done = evaluate_(script);
    if (!done) {
        return done.status();
    }
    return done.value() == "ok" ? Status{} : Status::Fail("Steam did not take the change");
}

} // namespace den::library::steam
