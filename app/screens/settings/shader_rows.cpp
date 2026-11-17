// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Custom shaders: the rows that import RetroArch presets (player/shader_import.h) and remove them.

#include <algorithm>
#include <format>
#include <system_error>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/player/session.h"
#include "den/player/shader_import.h"

namespace den::app {

namespace {

namespace fs = std::filesystem;

JobPtr g_import;

void StartImport(std::string name, std::function<Status(Job&)> work) {
    if (g_import && !g_import->Finished()) {
        Svc().stage->Toast("Already importing");
        return;
    }
    g_import = Svc().jobs->Run("Importing shaders", std::move(work), [name](Job& job) {
        if (job.State() == JobState::Done) {
            player::RefreshImportedPresets();
            Svc().stage->Toast(TrF("Imported {}. Its presets are under Imported in the picture list.", name), 5.0f);
        } else if (job.State() == JobState::Failed) {
            Svc().stage->Toast(job.Error(), 6.0f);
        }
        RebuildTop();
    });
    RebuildTop();
}

void ImportFromGithub() {
    ui::TextRequest request;
    request.title = "GitHub repository";
    request.placeholder = "github.com/owner/repository";
    request.max_length = 200;
    Svc().stage->AskText(std::move(request), [](const std::string& text) {
        const auto source = player::GithubShaderSource(text);
        if (!source) {
            Svc().stage->Toast("That is not a GitHub repository link", 4.0f);
            return;
        }
        StartImport(source->name, [source = *source](Job& job) {
            return player::DownloadShaderPack(source, player::ImportedShaderDir(), job);
        });
    });
}

void ImportFromFolder() {
    PickOptions options;
    options.title = "Choose a shader folder";
    Pick(std::move(options), [](const std::string& path) {
        const fs::path from = PathFromUtf8(path);
        std::string name = player::ShaderFolderName(PathToUtf8(from.filename()));
        if (name.empty()) {
            name = "shaders";
        }
        StartImport(name, [from, name](Job& job) {
            return player::CopyShaderFolder(from, name, player::ImportedShaderDir(), job);
        });
    });
}

std::vector<ui::RowSpec> ShaderRows() {
    std::vector<ui::RowSpec> rows;
    ui::RowSpec github = ValueRow(
        "shaders.github", "Import from GitHub", [] { return JobText(g_import, ""); }, ImportFromGithub);
    github.description = "A repository of RetroArch shaders (.slangp or .glslp), by its link";
    rows.push_back(std::move(github));
    rows.push_back(ActionRow("shaders.folder", "Import a folder", ImportFromFolder,
                             "RetroArch shaders already on this device"));

    std::vector<fs::path> folders;
    std::error_code error;
    for (fs::directory_iterator it(player::ImportedShaderDir(), error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_directory(error) && PathToUtf8(it->path().filename()) != ".import") {
            folders.push_back(it->path());
        }
    }
    std::sort(folders.begin(), folders.end());
    int index = 0;
    for (const fs::path& folder : folders) {
        const std::string name = PathToUtf8(folder.filename());
        rows.push_back(ValueRow(
            std::format("shaders.imported.{}", index++), name, [] { return std::string("Remove"); },
            [folder, name] {
                Svc().stage->Confirm(TrF("Remove {}?", name), "Its presets leave the picture list.", "Remove",
                                     [folder] {
                                         std::error_code ignored;
                                         fs::remove_all(folder, ignored);
                                         player::RefreshImportedPresets();
                                         RebuildTop();
                                     });
            }));
    }
    return rows;
}

} // namespace

DEN_REGISTER_SETTINGS(26, "Custom shaders", ShaderRows);

} // namespace den::app
