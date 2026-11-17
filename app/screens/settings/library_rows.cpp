// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Library: the game folders (add through the in-app browser, remove), the rescan and the tidy-up.

#include <format>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/library/tidy.h"

namespace den::app {

namespace {

JobPtr g_scan;

void Rescan() {
    if (g_scan && !g_scan->Finished()) {
        Svc().stage->Toast("Already scanning");
        return;
    }
    g_scan = Svc().library->Scan();
}

void AddFolder() {
    PickOptions options;
    options.title = "Add a game folder";
    const std::vector<library::Folder> folders = Svc().library->Folders();
    if (!folders.empty()) {
        options.start = PathToUtf8(PathFromUtf8(folders.back().path).parent_path());
    }
    Pick(std::move(options), [](const std::string& path) {
        const Status added = Svc().library->AddFolder(path);
        if (!added) {
            Svc().stage->Toast(added.error.empty() ? "That folder could not be added" : added.error, 5.0f);
            return;
        }
        Svc().stage->Toast(TrF("Added {}", path));
        Rescan();
        RebuildTop();
    });
}

std::vector<ui::RowSpec> LibraryRows() {
    std::vector<ui::RowSpec> rows;
    int index = 0;
    for (const library::Folder& folder : Svc().library->Folders()) {
        ui::RowSpec row = ValueRow(
            std::format("library.folder.{}", index++), folder.path, [] { return std::string("Remove"); },
            [path = folder.path] {
                Svc().stage->Confirm("Remove this folder?",
                                     path + " leaves the library. Its files stay where they are.",
                                     "Remove", [path] {
                                         if (const Status removed = Svc().library->RemoveFolder(path); !removed) {
                                             Svc().stage->Toast(removed.error);
                                         }
                                         Rescan();
                                         RebuildTop();
                                     });
            });
        if (!folder.system.empty()) {
            if (const auto console = Svc().plugins->FindConsole(folder.system)) {
                row.description = console->name;
            }
        }
        rows.push_back(std::move(row));
    }
    rows.push_back(ActionRow("library.add", "Add a game folder", AddFolder,
                             "Folders are searched with their subfolders"));
    rows.push_back(ValueRow(
        "library.rescan", "Search for games again",
        [] {
            return JobText(g_scan, std::format("{} games", Svc().library->Entries().size()));
        },
        Rescan));
    rows.push_back(ActionRow("library.tidy", "Tidy up the library",
                             [] { Svc().stage->Push("library.tidy"); },
                             "Clean game names, files and leftovers; shows the changes first"));
    if (library::tidy::CanUndo(Svc().data_dir)) {
        rows.push_back(ActionRow("library.tidy_undo", "Undo the last tidy-up",
                                 [] { Svc().stage->Push("library.tidy", {{"undo", "1"}}); }));
    }
    return rows;
}

} // namespace

DEN_REGISTER_SETTINGS(30, "Library", LibraryRows);

} // namespace den::app
