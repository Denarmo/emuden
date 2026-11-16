// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Custom shaders: RetroArch shader presets (.slangp, .glslp) imported from a GitHub repository
// or a folder on the device, each into a folder of its own under ImportedShaderDir().

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "den/base/jobs.h"
#include "den/base/result.h"

namespace den::player {

struct ShaderSource {
    std::string url;  // of the zip
    std::string name; // the folder it goes into
};

/// "https://github.com/owner/repo", "github.com/owner/repo/tree/branch" or "owner/repo".
std::optional<ShaderSource> GithubShaderSource(std::string link);

/// The folder for a repository or folder of this name: the characters GitHub allows in names
/// (letters, digits, '-', '_', '.'), not starting with '.', at most 60. Empty when none is left.
std::string ShaderFolderName(std::string_view text);

/// Unpacks a GitHub zip ("<repo>-<branch>/..." inside) into `out` without its top folder,
/// leaving out links and paths that could leave `out`.
Status UnpackShaderZip(const std::filesystem::path& archive, const std::filesystem::path& out,
                       const std::function<bool()>& cancelled);

/// Moves `from` to `dir`/`name`, over an earlier import of that name; fails when it has no preset.
Status InstallShaderFolder(const std::filesystem::path& from, const std::filesystem::path& dir,
                           const std::string& name);

/// Downloads and installs the repository into `dir` (ImportedShaderDir() in the app).
Status DownloadShaderPack(const ShaderSource& source, const std::filesystem::path& dir, Job& job);
/// Copies a folder of shaders on the device into `dir`.
Status CopyShaderFolder(const std::filesystem::path& from, const std::string& name,
                        const std::filesystem::path& dir, Job& job);

} // namespace den::player
