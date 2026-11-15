// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Settings, Sync: the active provider, the categories of this device, Sync now, the last
// sync, conflicts, games to download, "Upload my games", the folders.

#include <algorithm>
#include <format>

#include "../plugins/feature_api.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/sync/names.h"
#include "sync_glue.h"

namespace den::app {

namespace {

const char* CategoryLabel(const std::string& category) {
    if (category == sync::SAVES) {
        return "Saves";
    }
    if (category == sync::STATES) {
        return "Save states";
    }
    if (category == sync::CONFIG) {
        return "Settings";
    }
    if (category == sync::GAMES) {
        return "Games";
    }
    return "BIOS files";
}

const char* CategoryHelp(const std::string& category) {
    if (category == sync::STATES) {
        return "Save states and their pictures, the resume state included";
    }
    if (category == sync::CONFIG) {
        return "Settings and per-game settings; display, folders and controllers stay on this device";
    }
    if (category == sync::GAMES) {
        return "Download games from the other devices when you ask for them";
    }
    if (category == sync::BIOS) {
        return "Only what you put in EmuDEN's BIOS folder";
    }
    return "Battery saves";
}

std::string StatusText() {
    const sync::SyncState state = SyncSvc()->State();
    if (state.running) {
        return state.total > 0 ? std::format("Syncing {} of {}", std::min(state.done + 1, state.total), state.total)
                               : std::string("Syncing");
    }
    return SyncTimeText(state.last_sync);
}

std::string StatusHelp() {
    const sync::SyncState state = SyncSvc()->State();
    if (!state.errors.empty()) {
        return state.errors.front();
    }
    if (state.running && !state.current.empty()) {
        return state.current;
    }
    if (state.waiting > 0) {
        return std::format("{} files wait until the game ends", state.waiting);
    }
    return std::format("{} up, {} down last time", state.uploaded, state.downloaded);
}

void ChooseFolder(const std::string& title, const std::filesystem::path& start,
                  std::function<void(const std::filesystem::path&)> done) {
    PickOptions options;
    options.title = title;
    options.start = PathToUtf8(start);
    Pick(std::move(options), [done = std::move(done)](const std::string& path) { done(PathFromUtf8(path)); });
}

std::vector<ui::RowSpec> SyncRows() {
    std::vector<ui::RowSpec> rows;
    sync::Engine* engine = SyncSvc();
    if (engine == nullptr) {
        return rows;
    }
    const std::vector<sync::ProviderInfo> providers = engine->Providers();
    if (providers.empty()) {
        rows.push_back(ActionRow("sync.none", "Install a sync plugin", [] { Svc().stage->Push("plugins"); },
                                 "Google Drive, RomM or Syncthing, under Plugins"));
        return rows;
    }
    std::vector<std::string> names{"Off"};
    for (const sync::ProviderInfo& provider : providers) {
        names.push_back(provider.name);
    }
    rows.push_back(ChoiceRow(
        "sync.active", "Sync with", names,
        [providers] {
            const std::string active = SyncSvc()->Active();
            for (std::size_t i = 0; i < providers.size(); ++i) {
                if (providers[i].id == active) {
                    return static_cast<int>(i + 1);
                }
            }
            return 0;
        },
        [providers](int index) {
            SyncSvc()->SetActive(index <= 0 ? std::string() : providers[static_cast<std::size_t>(index - 1)].id);
            RebuildTop();
        }));
    const std::string active = engine->Active();
    const auto info = std::find_if(providers.begin(), providers.end(),
                                   [&](const sync::ProviderInfo& p) { return p.id == active; });
    if (info == providers.end()) {
        return rows;
    }
    rows.push_back(ActionRow(
        "sync.setup", "Set up " + info->name, [id = info->id] { Svc().stage->Push("plugin.view", {{"id", id}}); },
        "Sign in, choose the server or the folder"));
    ui::RowSpec status = ValueRow("sync.now", "Sync now", StatusText, [] { SyncSvc()->SyncNow(); });
    status.description = StatusHelp();
    rows.push_back(std::move(status));
    rows.push_back(ValueRow(
        "sync.conflicts", "Conflicts",
        [] {
            const std::size_t count = SyncSvc()->Conflicts().size();
            return count == 0 ? std::string("None") : std::to_string(count);
        },
        [] { Svc().stage->Push("sync.conflicts"); }));
    const std::vector<std::string> on = engine->Categories(active);
    for (const std::string& category : info->categories) {
        ui::RowSpec row = ToggleRow(
            "sync.category." + category, CategoryLabel(category),
            [active, category] {
                const std::vector<std::string> now = SyncSvc()->Categories(active);
                return std::find(now.begin(), now.end(), category) != now.end();
            },
            [active, category](bool value) {
                SyncSvc()->SetCategory(active, category, value);
                RebuildTop();
            });
        row.description = CategoryHelp(category);
        rows.push_back(std::move(row));
    }
    if (std::find(on.begin(), on.end(), sync::GAMES) != on.end()) {
        rows.push_back(ActionRow("sync.games", "Games", [] { Svc().stage->Push("sync.games"); },
                                 "Download a game from your other devices"));
        ui::RowSpec upload = ToggleRow(
            "sync.upload_games", "Upload my games", [] { return SyncSvc()->UploadGames(); },
            [](bool value) { SyncSvc()->SetUploadGames(value); });
        upload.description = "The games of this device go up, so the others can download them";
        rows.push_back(std::move(upload));
        rows.push_back(ValueRow(
            "sync.games_folder", "Downloaded games go to", [] { return PathToUtf8(SyncSvc()->GamesFolder()); },
            [] {
                ChooseFolder("Folder for downloaded games", SyncSvc()->GamesFolder(),
                             [](const std::filesystem::path& folder) {
                                 SyncSvc()->SetGamesFolder(folder);
                                 RebuildTop();
                             });
            }));
    }
    if (const std::filesystem::path folder = engine->Folder(active); !folder.empty()) {
        rows.push_back(ValueRow(
            "sync.folder", "Sync folder", [active] { return PathToUtf8(SyncSvc()->Folder(active)); },
            [active, folder] {
                ChooseFolder("The folder to sync", folder, [active](const std::filesystem::path& chosen) {
                    if (const Status used = SyncSvc()->UseFolder(active, chosen); !used.ok) {
                        Svc().stage->Toast(used.error, 5.0f);
                    }
                    RebuildTop();
                });
            }));
    }
    ui::RowSpec timer = ToggleRow(
        "sync.timer", "Every 15 minutes", [] { return SyncSvc()->IntervalMinutes() > 0; },
        [](bool value) { SyncSvc()->SetIntervalMinutes(value ? 15 : 0); });
    timer.description = "Also when the app starts and when a game ends";
    rows.push_back(std::move(timer));
    return rows;
}

} // namespace

DEN_REGISTER_SETTINGS(35, "Sync", SyncRows);

} // namespace den::app
