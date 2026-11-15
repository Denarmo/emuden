// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The tuning calls of feature plugins over the app: the device profile, a game's own core
// options in the layered settings, and Steam (library::steam). Plugins call from helper
// threads; what touches the app's state runs on the main thread, handed over every frame.

#include <algorithm>
#include <chrono>
#include <deque>
#include <future>
#include <mutex>

#include "../details/game_prefs.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/library/steam.h"
#include "den/plugins/feature_tune.h"
#include "den/system/device.h"
#include "feature_api.h"

namespace den::app {

namespace {

namespace steam = library::steam;

std::mutex& QueueMutex() {
    static std::mutex mutex;
    return mutex;
}

std::deque<std::function<void()>>& Queue() {
    static std::deque<std::function<void()>> queue;
    return queue;
}

// Runs `fn` on the main thread and waits for it; nullopt when the app did not get to it.
template <typename T>
std::optional<T> OnMain(std::function<T()> fn) {
    auto task = std::make_shared<std::packaged_task<T()>>(std::move(fn));
    std::future<T> result = task->get_future();
    {
        std::lock_guard lock(QueueMutex());
        Queue().push_back([task] { (*task)(); });
    }
    if (result.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        return std::nullopt;
    }
    return result.get();
}

void RunQueue() {
    std::deque<std::function<void()>> tasks;
    {
        std::lock_guard lock(QueueMutex());
        tasks.swap(Queue());
    }
    for (auto& task : tasks) {
        task();
    }
}

const char* FormId(system::FormFactor form) {
    switch (form) {
    case system::FormFactor::HandheldSmall:
        return "handheld";
    case system::FormFactor::HandheldDual:
        return "handheld_dual";
    case system::FormFactor::Tablet:
        return "tablet";
    case system::FormFactor::Tv:
        return "tv";
    default:
        return "desktop";
    }
}

// PRETTY_NAME and whether ID is steamos, from /etc/os-release where there is one.
std::pair<std::string, bool> OsRelease() {
    const auto text = ReadTextFile("/etc/os-release");
    std::string name;
    bool steamos = false;
    std::size_t start = 0;
    while (text && start < text->size()) {
        std::size_t end = text->find('\n', start);
        end = end == std::string::npos ? text->size() : end;
        std::string line = text->substr(start, end - start);
        start = end + 1;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string value = line.substr(equals + 1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (line.starts_with("PRETTY_NAME=")) {
            name = value;
        } else if (line.starts_with("ID=")) {
            steamos = ToLower(value) == "steamos";
        }
    }
    return {name, steamos};
}

JsonValue Facts() {
    const system::DeviceProfile& device = system::Device();
    const system::DeviceInfo& info = device.info;
    const auto [os_name, steamos] = info.os == "linux" ? OsRelease() : std::pair<std::string, bool>{};
    JsonValue screen = JsonObject();
    if (!info.screens.empty()) {
        screen = JsonObject({{"width", JsonNumber(info.screens.front().width)},
                             {"height", JsonNumber(info.screens.front().height)},
                             {"refresh_hz", JsonNumber(info.screens.front().refresh_hz)}});
    }
    return JsonObject({{"name", JsonString(device.name)},
                       {"known", JsonBool(device.known)},
                       {"manufacturer", JsonString(info.manufacturer)},
                       {"model", JsonString(info.model)},
                       {"soc", JsonString(info.soc)},
                       {"os", JsonString(info.os)},
                       {"os_version", JsonString(info.os_version)},
                       {"os_name", JsonString(os_name)},
                       {"steamos", JsonBool(steamos)},
                       {"cpu_cores", JsonNumber(info.cpu_cores)},
                       {"ram_mb", JsonNumber(info.ram_mb)},
                       {"gpu", JsonString(info.gpu)},
                       {"handheld", JsonBool(info.handheld || device.form == system::FormFactor::HandheldSmall ||
                                             device.form == system::FormFactor::HandheldDual)},
                       {"form", JsonString(FormId(device.form))},
                       {"tier", JsonString(system::Id(device.tier))},
                       {"screen", std::move(screen)}});
}

std::string Text(const JsonValue& value) {
    return value.kind == JsonValue::Kind::String ? value.string : std::string();
}

std::optional<plugins::TuneGame> GameNow(const std::string& id) {
    const auto entry = Svc().library->Find(id);
    if (!entry || !system::HasStore()) {
        return std::nullopt;
    }
    plugins::TuneGame game{entry->id, entry->name, entry->system, {}, {}, false, {}};
    const auto core = CoreForGame(*entry);
    if (!core) {
        return game;
    }
    game.core = core->id;
    game.core_name = core->name.empty() ? core->id : core->name;
    const system::Scope scope = ScopeOf(*entry);
    for (const KnownOption& option : KnownOptions(core->id)) {
        const std::string key = OptionKey(core->id, option.key);
        const bool layered = system::Store().Source(key, scope) >= system::Layer::Console;
        game.options.push_back({option.key, option.label,
                                layered ? Text(system::Store().Resolve(key, scope)) : option.fallback,
                                option.values, system::Store().IsSet(key, scope)});
    }
    game.known = !game.options.empty();
    return game;
}

Result<JsonValue> SetNow(const std::string& id, const JsonValue& values) {
    const auto game = GameNow(id);
    const auto entry = Svc().library->Find(id);
    if (!game || !entry || game->core.empty()) {
        return Status::Fail("the game has no core to set options of");
    }
    // Every value is checked before any is written.
    for (const auto& [key, value] : values.members) {
        const auto option = std::find_if(game->options.begin(), game->options.end(),
                                         [&](const plugins::TuneOption& o) { return o.key == key; });
        if (game->known && option == game->options.end()) {
            return Status::Fail(game->core_name + " has no option " + key);
        }
        if (value.kind == JsonValue::Kind::String && option != game->options.end() &&
            std::find(option->values.begin(), option->values.end(), value.string) == option->values.end()) {
            return Status::Fail(value.string + " is not a value of " + key);
        }
    }
    const system::Scope scope = ScopeOf(*entry);
    JsonValue before = JsonObject();
    for (const auto& [key, value] : values.members) {
        const std::string name = OptionKey(game->core, key);
        before.members.emplace_back(key, system::Store().IsSet(name, scope) ? system::Store().Resolve(name, scope)
                                                                            : JsonValue());
        if (value.kind == JsonValue::Kind::Null) {
            system::Store().Clear(name, scope);
        } else {
            system::Store().SetJson(name, value, scope);
        }
    }
    RebuildTop();
    return before;
}

class AppTune final : public plugins::TuneServices {
public:
    JsonValue DeviceFacts() override {
        return OnMain<JsonValue>(Facts).value_or(JsonObject());
    }

    std::optional<plugins::TuneGame> Game(const std::string& id) override {
        return OnMain<std::optional<plugins::TuneGame>>([id] { return GameNow(id); }).value_or(std::nullopt);
    }

    Result<JsonValue> SetGameOptions(const std::string& id, const JsonValue& values) override {
        auto done = OnMain<Result<JsonValue>>([id, values] { return SetNow(id, values); });
        return done ? std::move(*done) : Result<JsonValue>(Status::Fail("the app is busy; try again"));
    }

    plugins::SteamState Steam() override {
        plugins::SteamState state;
        const auto root = steam::FindRoot();
        state.installed = root.has_value();
        if (!root) {
            state.note = "Steam was not found on this device.";
        } else if (!steam::Client::Enabled(*root)) {
            state.note = "EmuDEN changes Steam games through Steam's local debugging port, which is off. "
                         "Decky Loader turns it on; until then, enter the settings in the game's Properties.";
        } else {
            state.can_change = true;
        }
        return state;
    }

    std::vector<plugins::SteamApp> SteamApps() override {
        std::vector<plugins::SteamApp> out;
        if (const auto root = steam::FindRoot()) {
            for (const steam::App& app : steam::Install(*root).Apps()) {
                if (!app.tool) {
                    out.push_back({app.appid, app.name, app.shortcut});
                }
            }
        }
        return out;
    }

    std::vector<plugins::SteamTool> SteamTools() override {
        std::vector<plugins::SteamTool> out;
        if (const auto root = steam::FindRoot()) {
            for (const steam::Tool& tool : steam::Install(*root).Tools()) {
                out.push_back({tool.name, tool.display});
            }
        }
        return out;
    }

    Result<plugins::SteamConfig> SteamAppConfig(const std::string& appid) override {
        const auto root = steam::FindRoot();
        if (!root) {
            return Status::Fail("Steam was not found on this device");
        }
        steam::AppConfig config = steam::Install(*root).ConfigFromFiles(appid);
        if (steam::Client::Enabled(*root)) {
            if (const Result<steam::AppConfig> live = steam::Client::Local().Config(appid)) {
                config = live.value();
            }
        }
        return plugins::SteamConfig{config.launch_options, config.compat_tool, config.compat_tool_display,
                                    config.compat_tool_chosen};
    }

    Status SetSteamApp(const std::string& appid, const std::optional<std::string>& launch_options,
                       const std::optional<std::string>& compat_tool) override {
        const auto root = steam::FindRoot();
        if (!root || !steam::Client::Enabled(*root)) {
            return Status::Fail("Steam's local debugging port is off; enter the settings in the game's Properties");
        }
        steam::Client client = steam::Client::Local();
        if (const Status set = client.Set(appid, launch_options, compat_tool); !set.ok) {
            return set;
        }
        const Result<steam::AppConfig> after = client.Config(appid);
        if (after && launch_options && after.value().launch_options != *launch_options) {
            return Status::Fail("Steam did not keep the launch options");
        }
        return {};
    }
};

} // namespace

std::shared_ptr<plugins::TuneServices> CreateTuneServices() {
    return std::make_shared<AppTune>();
}

DEN_REGISTER_FRAME(RunQueue);

} // namespace den::app
