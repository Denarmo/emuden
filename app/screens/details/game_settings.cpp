// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Settings for one game: every row starts at "Console setting" and overrides it for this
// game only. Route "game_settings", param id. Also the page that puts a game into the
// user's collections: route "collect", param id.

#include <algorithm>

#include "den/app/collections.h"
#include "den/app/games.h"
#include "den/app/strings.h"
#include "den/app/black_frames.h"
#include "den/app/player_view.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/app/game_prefs.h"

namespace den::app {

namespace {

// A choice whose first entry clears the game's own value. `stored` are the values kept in
// the settings, `labels` what the user reads for them.
ui::RowSpec InheritRow(const std::string& id, const std::string& label, const std::string& key,
                       const system::Scope& scope, std::vector<JsonValue> stored,
                       std::vector<std::string> labels) {
    const auto same = [](const JsonValue& a, const JsonValue& b) {
        return a.kind == b.kind && a.number == b.number && a.string == b.string && a.boolean == b.boolean;
    };
    const auto index_of = [stored, same](const JsonValue& value) {
        for (std::size_t i = 0; i < stored.size(); ++i) {
            if (same(stored[i], value)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const int inherited =
        index_of(system::Store().Resolve(key, system::Scope::Console(scope.system)));
    std::vector<std::string> options;
    options.push_back(inherited >= 0 ? TrF("game_settings.console_value", labels[static_cast<std::size_t>(inherited)])
                                     : Tr("game_settings.console"));
    options.insert(options.end(), labels.begin(), labels.end());
    return ChoiceRow(
        id, label, std::move(options),
        [key, scope, index_of] {
            return system::Store().IsSet(key, scope) ? index_of(system::Store().Resolve(key, scope)) + 1 : 0;
        },
        [key, scope, stored](int index) {
            if (index <= 0) {
                system::Store().Clear(key, scope);
            } else {
                system::Store().SetJson(key, stored[static_cast<std::size_t>(index - 1)], scope);
            }
        });
}

std::vector<JsonValue> Strings(const std::vector<std::string>& values) {
    std::vector<JsonValue> out;
    for (const std::string& value : values) {
        out.push_back(JsonString(value));
    }
    return out;
}

class GameSettingsScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        const auto found = svc.library->Find(Param("id"));
        ui::Node* content =
            BuildPlainFrame(*this, found ? TrF("game_settings.title", found->name) : Tr("details.settings"));
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "game_settings.list";
        list->pad_bottom = 24;
        const auto console = found ? svc.plugins->FindConsole(found->system) : std::nullopt;
        if (!found || !console || !system::HasStore()) {
            list->Emplace<ui::Label>(Tr("launch.gone"))->tone = ui::Tone::Dim;
            return;
        }
        const library::Entry game = *found;
        const system::Scope scope = ScopeOf(game);
        std::vector<ui::RowSpec> rows;
        if (console->runner == plugins::Runner::Engine) {
            rows.push_back(ValueRow("game_settings.bfi", BFI_LABEL, [] { return std::string("Not with an engine"); }));
            rows.push_back(InheritRow("game_settings.engine_screen", Tr("engine.screen"), ENGINE_SCREEN_KEY.name(),
                                      scope, Strings({"app", "fullscreen", "window"}),
                                      {Tr("engine.screen_app"), Tr("engine.screen_full"), Tr("engine.screen_window")}));
        } else {
            CoreRows(game, scope, rows);
        }
        AddRows(list, std::move(rows));

        if (console->runner == plugins::Runner::Core) {
            OptionRows(list, game, scope);
        } else {
            list->Emplace<ui::Label>(Tr("engine.game_note"))->tone = ui::Tone::Dim;
        }
        std::vector<ui::RowSpec> last;
        last.push_back(ActionRow(
            "game_settings.reset", Tr("game_settings.reset"),
            [scope] {
                system::Store().ClearScope(scope);
                Svc().stage->Toast(Tr("game_settings.reset_done"));
                RebuildTop();
            },
            Tr("game_settings.reset_help")));
        AddRows(list, std::move(last));
    }

private:
    static void CoreRows(const library::Entry& game, const system::Scope& scope, std::vector<ui::RowSpec>& rows) {
        const std::vector<plugins::CoreInfo> cores = Svc().plugins->CoresFor(game.system);
        const auto name = [](const plugins::CoreInfo& core) { return core.name.empty() ? core.id : core.name; };
        const auto current = Svc().plugins->CoreFor(game.system);
        if (cores.size() > 1) {
            std::vector<std::string> ids, labels;
            for (const plugins::CoreInfo& core : cores) {
                ids.push_back(core.id);
                labels.push_back(core.installed ? name(core) : TrF("details.core_missing", name(core)));
            }
            // The console's core is not a stored value, so it is named in the first entry here.
            ui::RowSpec row = InheritRow("game_settings.core", Tr("game_settings.core"), GAME_CORE_KEY.name(),
                                         scope, Strings(ids), labels);
            if (current) {
                row.options.front() = TrF("game_settings.console_value", name(*current));
            }
            row.set_index = [inner = row.set_index](int index) {
                inner(index);
                RebuildTop(); // the core options below belong to the chosen core
            };
            rows.push_back(std::move(row));
        } else if (current) {
            rows.push_back(ValueRow("game_settings.core", Tr("game_settings.core"),
                                    [text = name(*current)] { return text; }));
        }
        std::vector<JsonValue> sizes;
        for (std::size_t i = 0; i < ScreenSizeNames().size(); ++i) {
            sizes.push_back(JsonNumber(static_cast<double>(i)));
        }
        rows.push_back(InheritRow("game_settings.screen_size", Tr("game_settings.screen_size"),
                                  SCREEN_SIZE_KEY.name(), scope, std::move(sizes), ScreenSizeNames()));
        ui::RowSpec bfi = InheritRow("game_settings.bfi", BFI_LABEL, BFI_KEY.name(), scope,
                                     {JsonBool(false), JsonBool(true)}, {"Off", "On"});
        bfi.description = "On this device only";
        rows.push_back(std::move(bfi));
    }

    static void OptionRows(ui::Node* list, const library::Entry& game, const system::Scope& scope) {
        auto heading = ui::MakeHeading(Tr("game_settings.options"));
        heading->pad_top = 14;
        heading->pad_bottom = 4;
        list->Add(std::move(heading));
        const auto core = CoreForGame(game);
        const std::vector<KnownOption> options = core ? KnownOptions(core->id) : std::vector<KnownOption>{};
        if (options.empty()) {
            list->Emplace<ui::Label>(Tr("game_settings.options_unknown"))->tone = ui::Tone::Dim;
            return;
        }
        std::vector<ui::RowSpec> rows;
        for (const KnownOption& option : options) {
            rows.push_back(InheritRow("game_settings.option." + option.key,
                                      option.label.empty() ? option.key : option.label,
                                      OptionKey(core->id, option.key), scope, Strings(option.values), option.values));
        }
        AddRows(list, std::move(rows));
    }
};

class CollectScreen final : public ui::Page {
public:
    void Build() override {
        revision_ = GamesRevision();
        const std::string id = Param("id");
        const auto game = Svc().library->Find(id);
        ui::Node* content = BuildPlainFrame(*this, game ? TrF("collections.for", game->name) : Tr("collections.title"));
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "collect.list";
        list->pad_bottom = 24;
        std::vector<ui::RowSpec> rows;
        int index = 0;
        for (const std::string& name : Collections()) {
            rows.push_back(ToggleRow(
                "collect." + std::to_string(index++), name, [name, id] { return InCollection(name, id); },
                [name, id](bool in) { SetInCollection(name, id, in); }));
        }
        rows.push_back(ActionRow(
            "collect.new", Tr("collections.new"),
            [id] {
                const std::string name = CreateCollection();
                SetInCollection(name, id, true);
                Svc().stage->Toast(TrF("collections.created", name));
            },
            Tr("collections.new_help")));
        AddRows(list, std::move(rows));
    }
    void Update(ui::Context&) override {
        if (GamesRevision() != revision_) {
            Rebuild();
        }
    }

private:
    std::uint64_t revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("game_settings", GameSettingsScreen);
DEN_REGISTER_SCREEN("collect", CollectScreen);

} // namespace den::app
