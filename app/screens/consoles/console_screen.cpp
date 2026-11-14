// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One console: install, repair or remove what plays it, choose its core, and the settings
// its plugin manifest declares.

#include <algorithm>
#include <cstdlib>
#include <format>

#include "../details/game_prefs.h"
#include "bios_import.h"
#include "console_state.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"

namespace den::app {

namespace {

std::string SettingKey(const std::string& system, const std::string& key) {
    return "console." + system + "." + key;
}

std::string SettingValue(const std::string& system, const plugins::SettingSpec& spec) {
    return Svc().settings->GetString(SettingKey(system, spec.key), spec.default_value);
}

// A row for one setting of the manifest's schema. The value is kept as text.
ui::RowSpec SettingRow(const std::string& system, const plugins::SettingSpec& spec) {
    const std::string key = SettingKey(system, spec.key);
    const auto get = [system, spec] { return SettingValue(system, spec); };
    const auto set = [key](const std::string& value) { Svc().settings->SetString(key, value); };
    ui::RowSpec row;
    if (spec.type == "toggle") {
        row = ToggleRow(key, spec.label, [get] { return get() == "true"; },
                        [set](bool on) { set(on ? "true" : "false"); });
    } else if (spec.type == "choice" && !spec.choices.empty()) {
        std::vector<std::string> labels;
        for (const auto& [value, label] : spec.choices) {
            labels.push_back(label.empty() ? value : label);
        }
        row = ChoiceRow(
            key, spec.label, std::move(labels),
            [get, spec] {
                const std::string value = get();
                for (std::size_t i = 0; i < spec.choices.size(); ++i) {
                    if (spec.choices[i].first == value) {
                        return static_cast<int>(i);
                    }
                }
                return 0;
            },
            [set, spec](int index) { set(spec.choices[static_cast<std::size_t>(index)].first); });
    } else if (spec.type == "range" && spec.max > spec.min) {
        const bool whole = spec.step == static_cast<double>(static_cast<long long>(spec.step));
        const auto text = [whole, suffix = spec.suffix](float value) {
            return (whole ? std::format("{}", static_cast<long long>(value + (value < 0 ? -0.5f : 0.5f)))
                          : std::format("{:.2f}", value)) +
                   suffix;
        };
        row = SliderRow(
            key, spec.label, static_cast<float>(spec.min), static_cast<float>(spec.max),
            static_cast<float>(spec.step > 0 ? spec.step : 1.0),
            [get] { return static_cast<float>(std::strtod(get().c_str(), nullptr)); },
            [set, whole](float value) {
                set(whole ? std::format("{}", static_cast<long long>(value + (value < 0 ? -0.5f : 0.5f)))
                          : std::format("{}", value));
            },
            text);
    } else if (spec.type == "path") {
        row = ValueRow(key, spec.label, [get] { return get().empty() ? std::string("Not set") : get(); },
                       [get, set, label = spec.label] {
                           PickOptions options;
                           options.title = label;
                           options.start = get();
                           Pick(std::move(options), [set](const std::string& path) {
                               set(path);
                               RebuildTop();
                           });
                       });
    } else {
        row = ValueRow(key, spec.label, get);
    }
    row.description = spec.help;
    return row;
}

class ConsoleScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        revision_ = svc.plugins->Revision();
        bios_imports_ = BiosImports();
        const std::string system = Param("system");
        const auto console = svc.plugins->FindConsole(system);
        ui::Node* content = BuildPlainFrame(*this, console ? console->name : "Console");
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "console.list";
        list->pad_bottom = 24;
        if (!console) {
            list->Emplace<ui::Label>("This console is not known any more.");
            return;
        }
        std::vector<ui::RowSpec> rows;
        rows.push_back(ValueRow("console.state", "State", [system] {
            const auto found = Svc().plugins->FindConsole(system);
            return found ? StateText(*found) : std::string();
        }));
        if (console->runner == plugins::Runner::Engine) {
            EngineRows(*console, rows);
        } else {
            CoreRows(*console, rows);
        }
        AddRows(list, std::move(rows));

        const std::vector<plugins::SettingSpec> settings = svc.plugins->SettingsFor(system);
        if (!settings.empty()) {
            auto heading = ui::MakeHeading("Settings");
            heading->pad_top = 14;
            heading->pad_bottom = 4;
            list->Add(std::move(heading));
            std::vector<ui::RowSpec> setting_rows;
            for (const plugins::SettingSpec& spec : settings) {
                setting_rows.push_back(SettingRow(system, spec));
            }
            AddRows(list, std::move(setting_rows));
        }
    }

    void Update(ui::Context&) override {
        if (Svc().plugins->Revision() != revision_ || BiosImports() != bios_imports_) {
            Rebuild();
        }
    }

private:
    static void Start(const plugins::Console& console, const char* done) {
        if (ConsoleJob(console.id)) {
            return;
        }
        TrackConsoleJob(console.id, console.name, done, Svc().plugins->Install(console.id));
    }

    static void CoreRows(const plugins::Console& console, std::vector<ui::RowSpec>& rows) {
        Services& svc = Svc();
        const std::string system = console.id;
        if (const JobPtr job = ConsoleJob(system)) {
            rows.push_back(ActionRow("console.cancel", "Cancel", [job] { job->Cancel(); }));
        } else if (!console.installed && !console.available) {
            rows.push_back(ValueRow("console.unavailable", "Core", [] {
                return std::string("No download for this device");
            }));
        } else if (!console.installed) {
            rows.push_back(ActionRow("console.install", "Install", [console] { Start(console, "installed"); },
                                     "Downloads the core that plays these games"));
        } else {
            if (console.update_available) {
                rows.push_back(ActionRow("console.update", "Update", [console] { Start(console, "updated"); }));
            }
            rows.push_back(ActionRow("console.repair", "Repair", [console] { Start(console, "repaired"); },
                                     "Downloads and checks the core again"));
            rows.push_back(ActionRow("console.remove", "Remove", [console] {
                Svc().stage->Confirm("Remove " + console.name + "?",
                                     "Its core is deleted. Games, saves and states stay.", "Remove", [console] {
                                         TrackConsoleJob(console.id, console.name, "removed",
                                                         Svc().plugins->Remove(console.id));
                                     });
            }));
        }

        const std::vector<plugins::CoreInfo> cores = svc.plugins->CoresFor(system);
        const auto chosen = svc.plugins->CoreFor(system);
        const auto name = [](const plugins::CoreInfo& core) { return core.name.empty() ? core.id : core.name; };
        if (cores.size() > 1) {
            std::vector<std::string> names;
            for (const plugins::CoreInfo& core : cores) {
                names.push_back(name(core));
            }
            ui::RowSpec row = ChoiceRow(
                "console.core", "Core", std::move(names),
                [system, cores] {
                    const auto current = Svc().plugins->CoreFor(system);
                    for (std::size_t i = 0; current && i < cores.size(); ++i) {
                        if (cores[i].id == current->id) {
                            return static_cast<int>(i);
                        }
                    }
                    return 0;
                },
                [system, cores](int index) {
                    Svc().plugins->SelectCore(system, cores[static_cast<std::size_t>(index)].id);
                    RebuildTop();
                });
            row.description = "Which core plays this console's games";
            rows.push_back(std::move(row));
        } else if (chosen) {
            rows.push_back(ValueRow("console.core", "Core", [text = name(*chosen)] { return text; }));
        }
        if (chosen) {
            for (const std::string& file : chosen->missing_bios) {
                ui::RowSpec bios = ValueRow("console.bios." + file, file, [] { return std::string("Missing"); });
                bios.description = "Put this BIOS file into the BIOS folder";
                rows.push_back(std::move(bios));
            }
            if (!chosen->system_dir.empty()) {
                rows.push_back(ValueRow("console.bios_dir", "BIOS folder",
                                        [dir = PathToUtf8(chosen->system_dir)] { return dir; }));
                if (!console.bios.empty()) {
                    rows.push_back(ActionRow(
                        "console.bios_import", "Add BIOS files",
                        [console, dir = chosen->system_dir] { ImportBios(console, dir); },
                        "Copies this console's BIOS files from a folder you choose"));
                }
            }
        }
        std::string extensions;
        for (const std::string& extension : console.extensions) {
            extensions += (extensions.empty() ? "" : ", ") + extension;
        }
        if (!extensions.empty()) {
            rows.push_back(ValueRow("console.files", "Game files", [extensions] { return extensions; }));
        }
    }

    static void EngineRows(const plugins::Console& console, std::vector<ui::RowSpec>& rows) {
        const std::string system = console.id;
        ui::RowSpec engine = ValueRow(
            "console.engine", "Engine program",
            [system] {
                const auto install = Svc().engine->Find(system);
                return install ? PathToUtf8(install->executable) : std::string("Not found");
            },
            [system] { ChooseEnginePath(system); });
        const std::string reason = Svc().engine->UnavailableReason(system);
        engine.description = Svc().engine->Available(system) || reason.empty()
                                 ? "These games run in their own engine, outside EmuDEN"
                                 : reason;
        rows.push_back(std::move(engine));
        if (!Svc().settings->GetString(engine::PathKey(system)).empty()) {
            rows.push_back(ActionRow(
                "console.engine_auto", "Find the engine automatically",
                [system] {
                    Svc().settings->Remove(engine::PathKey(system));
                    RebuildTop();
                },
                "Forgets the chosen program and looks in the usual places again"));
        }
        ui::RowSpec screen = ChoiceRow(
            "console.engine_screen", "Screen", {"Same as EmuDEN", "Full screen", "Window"},
            [system] {
                const std::string value = system::Store().Get(ENGINE_SCREEN_KEY, system::Scope::Console(system));
                return value == "fullscreen" ? 1 : value == "window" ? 2 : 0;
            },
            [system](int index) {
                system::Store().Set(ENGINE_SCREEN_KEY, std::string(index == 1 ? "fullscreen" : index == 2 ? "window" : "app"),
                                    system::Scope::Console(system));
            });
        screen.description = "How these games open; a game can have its own choice on its settings page";
        rows.push_back(std::move(screen));
        rows.push_back(ValueRow("console.engine_args", "Launch options", [system] {
            const std::vector<std::string> args = Svc().settings->GetList(engine::ArgsKey(system));
            std::string text;
            for (const std::string& arg : args) {
                text += (text.empty() ? "" : " ") + arg;
            }
            return text.empty() ? std::string("Default") : text;
        }));
        rows.back().description = "The list \"" + engine::ArgsKey(system) + "\" in settings.json replaces them";
        ui::RowSpec note = ValueRow("console.engine_note", "About the engine",
                                    [] { return std::string("Installed emulator"); });
        note.description = "Until the engine plugin exists, EmuDEN starts the emulator installed on this device, "
                           "and saves and settings stay in that emulator";
        rows.push_back(std::move(note));
    }

    std::uint64_t revision_ = 0;
    int bios_imports_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("console", ConsoleScreen);

} // namespace den::app
