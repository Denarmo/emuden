// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// First-run setup in four steps: welcome, game folders (add them or take them over from the
// emulator already installed on this device), consoles (install what plays the games found)
// and done. Route "setup"; shown first until "setup.done" is set, and again from Settings.

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <format>

#include "../consoles/console_state.h"
#include "../home/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/system/device.h"
#include "den/system/first_run.h"

namespace den::app {

namespace {

namespace fs = std::filesystem;

// Where the emulator that plays the engine consoles keeps its profile: a portable "user"
// folder next to its program, or its folder in the OS app data.
std::vector<fs::path> EarlierProfiles() {
    std::vector<fs::path> found;
    const auto add = [&](const fs::path& dir) {
        if (!dir.empty() && IsRegularFile(dir / "config" / "qt-config.ini") &&
            std::find(found.begin(), found.end(), dir) == found.end()) {
            found.push_back(dir);
        }
    };
    std::vector<fs::path> roots;
    for (const char* name : {"APPDATA", "XDG_DATA_HOME"}) {
        if (const char* value = std::getenv(name); value && *value) {
            roots.push_back(PathFromUtf8(value));
        }
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        roots.push_back(PathFromUtf8(home) / ".local" / "share");
    }
    for (const plugins::Console& console : Svc().plugins->Consoles()) {
        if (console.runner != plugins::Runner::Engine) {
            continue;
        }
        if (const auto install = Svc().engine->Find(console.id)) {
            add(install->executable.parent_path() / "user");
            for (const fs::path& root : roots) {
                add(root / install->executable.stem());
            }
        }
    }
    return found;
}

class SetupScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        signature_ = Signature();
        static const char* const TITLES[] = {"setup.welcome", "setup.games", "setup.consoles", "setup.done"};
        // The first step says what was set up for this device; nothing is asked.
        ui::Node* content =
            BuildPlainFrame(*this, step_ == 0 ? "Set up for " + system::Device().name : Tr(TITLES[step_]));
        content->pad_right = svc.theme->margin;
        auto* progress = content->Emplace<ui::Label>(TrF("setup.step", step_ + 1, 4));
        progress->tone = ui::Tone::Dim;
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "setup.list";
        list->pad_bottom = 24;
        const auto text = [&](const std::string& line) { list->Emplace<ui::Label>(line)->tone = ui::Tone::Dim; };
        std::vector<ui::RowSpec> rows;
        switch (step_) {
        case 0:
            text("EmuDEN chose these settings for this device. Settings > Device changes them.");
            for (const std::string& line : system::DescribeDefaults(system::Device())) {
                list->Emplace<ui::Label>(line);
            }
            break;
        case 1:
            text(Tr("setup.games_help"));
            GameRows(rows);
            break;
        case 2:
            text(Tr("setup.consoles_help"));
            ConsoleRows(rows);
            break;
        default:
            text(TrF("setup.done_games", svc.library->Entries().size(), svc.library->Systems().size()));
            text(Tr("setup.done_help"));
            break;
        }
        rows.push_back(ActionRow("setup.next", Tr(step_ == 3 ? "setup.finish" : step_ == 0 ? "setup.begin" : "setup.next"),
                                 [this] { Next(); }));
        if (step_ > 0) {
            rows.push_back(ActionRow("setup.previous", Tr("setup.previous"), [this] { Go(step_ - 1); }));
        }
        if (step_ < 3) {
            rows.push_back(ActionRow("setup.skip", Tr("setup.skip"), [this] { Finish(); }, Tr("setup.skip_help")));
        }
        AddRows(list, std::move(rows));
        focus_next_ = true;
    }

    void Update(ui::Context&) override {
        // Each step opens with the focus on its main button.
        if (focus_next_ && moved_) {
            if (ui::Node* next = Root().FindById("setup.next")) {
                SetFocus(next);
            }
            moved_ = false;
        }
        focus_next_ = false;
        // Installs run one after the other.
        if (!queue_.empty() && !ConsoleJob(current_)) {
            current_ = queue_.front();
            queue_.pop_front();
            if (const auto console = Svc().plugins->FindConsole(current_); console && !console->installed) {
                TrackConsoleJob(console->id, console->name, "installed", Svc().plugins->Install(console->id));
            }
        }
        if (Signature() != signature_) {
            Rebuild();
        }
    }

    bool OnNav(input::Nav nav) override {
        if (nav != input::Nav::Back) {
            return false;
        }
        if (step_ > 0) {
            Go(step_ - 1);
        }
        return true;
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, Tr("hint.select")}, {input::Nav::Back, Tr("setup.previous")}};
    }

private:
    std::uint64_t Signature() const {
        Services& svc = Svc();
        return svc.library->Revision() * 31 + svc.plugins->Revision() * 7 + svc.library->Folders().size() +
               (ConsoleJob(current_) ? 1000003u : 0u) + queue_.size() * 131;
    }

    void Go(int step) {
        step_ = std::clamp(step, 0, 3);
        moved_ = true;
        Rebuild();
    }

    void Next() {
        if (step_ == 3) {
            Finish();
        } else {
            Go(step_ + 1);
        }
    }

    void Finish() {
        system::MarkSetupDone(*Svc().settings);
        if (GetStage().Depth() > 1) {
            GetStage().Pop();
        } else {
            GetStage().Replace("home");
        }
    }

    void GameRows(std::vector<ui::RowSpec>& rows) {
        int index = 0;
        for (const library::Folder& folder : Svc().library->Folders()) {
            rows.push_back(ValueRow(std::format("setup.folder.{}", index++), folder.path,
                                    [] { return Tr("setup.remove"); }, [path = folder.path] {
                                        Svc().library->RemoveFolder(path);
                                        Svc().library->Scan();
                                    }));
        }
        rows.push_back(ActionRow("setup.add_folder", Tr("setup.add_folder"), [] {
            PickOptions options;
            options.title = Tr("setup.add_folder");
            Pick(std::move(options), [](const std::string& path) {
                const Status added = Svc().library->AddFolder(path);
                if (!added) {
                    Svc().stage->Toast(added.error.empty() ? Tr("setup.add_failed") : added.error, 5.0f);
                    return;
                }
                Svc().library->Scan();
            });
        }, Tr("setup.add_folder_help")));
        int number = 0;
        for (const fs::path& profile : EarlierProfiles()) {
            rows.push_back(ActionRow(std::format("setup.import.{}", number++), Tr("setup.import"), [profile] {
                const std::size_t before = Svc().library->Folders().size();
                const Status imported = Svc().library->ImportFolders(profile);
                const std::size_t added = Svc().library->Folders().size() - before;
                if (!imported) {
                    Svc().stage->Toast(imported.error, 5.0f);
                } else {
                    Svc().stage->Toast(added > 0 ? TrF("setup.imported", added) : Tr("setup.imported_none"), 4.0f);
                    if (added > 0) {
                        Svc().library->Scan();
                    }
                }
            }, TrF("setup.import_help", PathToUtf8(profile))));
        }
        rows.push_back(ValueRow("setup.found", Tr("setup.found"), [] {
            return GameCount(Svc().library->Entries().size());
        }));
    }

    void ConsoleRows(std::vector<ui::RowSpec>& rows) {
        Services& svc = Svc();
        std::vector<std::string> missing;
        for (const library::SystemInfo& info : svc.library->Systems()) {
            const auto console = svc.plugins->FindConsole(info.id);
            if (!console) {
                continue;
            }
            ui::RowSpec row = ValueRow("setup.console." + info.id, console->name,
                                       [id = info.id] {
                                           const auto found = Svc().plugins->FindConsole(id);
                                           return found ? StateText(*found) : std::string();
                                       },
                                       [id = info.id] { Svc().stage->Push("console", {{"system", id}}); });
            row.description = GameCount(info.game_count);
            rows.push_back(std::move(row));
            if (console->runner == plugins::Runner::Core && !console->installed && console->available) {
                missing.push_back(info.id);
            }
        }
        if (rows.empty()) {
            rows.push_back(ValueRow("setup.no_consoles", Tr("setup.no_games"), [] { return std::string(); }));
        }
        if (!missing.empty() && queue_.empty() && !ConsoleJob(current_)) {
            rows.push_back(ActionRow("setup.install_all", TrF("setup.install_all", missing.size()), [this, missing] {
                queue_.assign(missing.begin(), missing.end());
            }, Tr("setup.install_all_help")));
        }
    }

    int step_ = 0;
    bool moved_ = true, focus_next_ = false;
    std::uint64_t signature_ = 0;
    std::deque<std::string> queue_;
    std::string current_;
};

} // namespace

DEN_REGISTER_SCREEN("setup", SetupScreen);
DEN_REGISTER_SETTINGS(95, "Setup", [] {
    return std::vector<ui::RowSpec>{ActionRow(
        "setup.again", Tr("setup.again"), [] { Svc().stage->Push("setup"); }, Tr("setup.again_help"))};
});

} // namespace den::app
