// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Box covers from the LaunchBox Games Database, through its split copy at
// github.com/Denarmo/launchbox-covers: a small manifest, then per platform one file per first
// letter of a title's key. The images themselves come from LaunchBox's image server.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/library/library.h"
#include "den/library/systems.h"

namespace den {
struct JsonValue;
}

namespace den::library {

/// The data's address; the manifest is `manifest.json` under it.
inline constexpr std::string_view LAUNCHBOX_DATA = "https://raw.githubusercontent.com/Denarmo/launchbox-covers/main/data/";

/// A title's key in that data: lower case, accents folded, "&" as "and", a leading "the" or a
/// ", the" after the main title dropped, then letters and digits only (README, "Keys").
std::string LaunchBoxKey(std::string_view title);

struct LaunchBoxCover {
    std::string url;    // the image on LaunchBox's server
    std::string title;  // the game's name in the database
    std::string region; // LaunchBox's region name of the cover, "" for none
};

class LaunchBoxCovers {
public:
    /// `cache_dir` holds the manifest and the files read so far, per data version.
    LaunchBoxCovers(std::filesystem::path cache_dir, HttpFetcher fetcher);
    ~LaunchBoxCovers();

    /// The cover of the first title that names a game of the system's platform, in the region
    /// that fits `region_tags` (RegionTags) best. `answered` turns false when the data could
    /// not be reached, so a miss is not to be remembered.
    std::optional<LaunchBoxCover> Find(const SystemDef& system, const std::vector<std::string>& titles,
                                       const std::vector<std::string>& region_tags, bool& answered);

private:
    struct Platform {
        std::string dir;
        std::vector<std::string> buckets;
    };
    struct Manifest {
        std::string version;
        std::string images;
        std::map<std::string, std::string> libretro;
        std::map<std::string, Platform> platforms;
    };
    const Manifest* LoadManifest(bool& answered);
    const JsonValue* LoadBucket(const Manifest& manifest, const Platform& platform, const std::string& bucket,
                                bool& answered);

    std::filesystem::path cache_dir_;
    HttpFetcher fetcher_;
    std::mutex mutex_;
    std::optional<Manifest> manifest_;
    bool manifest_tried_ = false;
    std::map<std::string, std::unique_ptr<JsonValue>> buckets_;
};

} // namespace den::library
