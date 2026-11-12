// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"
#include "den/gfx/device.h"

namespace den::gfx {

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

/// PNG, JPEG, BMP, GIF (first frame), TGA. Safe on any thread.
Result<ImageData> DecodeImage(const void* data, std::size_t size);
Result<ImageData> LoadImageFile(const std::filesystem::path& file);
/// Halves the image until neither side is larger than `max_side`.
void ShrinkImage(ImageData& image, int max_side);

/// Textures by file path, decoded on the job system and uploaded a few per frame. Main thread.
class TextureCache {
public:
    enum class State { Unknown, Loading, Ready, Failed };

    TextureCache(Device& device, Jobs& jobs);
    ~TextureCache();

    /// The texture when it is ready; otherwise an empty one, and the decode is started.
    /// `path` is UTF-8. Images are shrunk to `max_side`.
    Texture Get(const std::string& path, int max_side = 512);
    State StateOf(const std::string& path) const;
    /// Puts a decoded image under a key of the caller's choosing (generated covers).
    void Put(const std::string& key, const ImageData& image);
    void Forget(const std::string& path);
    /// Uploads finished decodes and drops textures unused for a while. Once per frame.
    void Pump();
    void SetCapacity(std::size_t textures);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace den::gfx
