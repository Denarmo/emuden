// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Console plugins installed from a file the user picked: one signed with EmuDEN's key is one
// of its own, any other is a third party's, held to the rules of an added source without its
// namespace. And the console plugins an update no longer brings, carried over.

#include <set>

#include "den/plugins/feature_manifest.h"
#include "real_host.h"

namespace den::plugins {

namespace {

constexpr const char* SIGNATURE_FILE = "plugin.json.sig";
constexpr const char* CARRY_OVER_FILE = "carry-over.json";
constexpr std::uint64_t MAX_SIGNATURE_SIZE = 4096;
constexpr std::size_t MAX_PACKAGE_SIZE = 4u << 20;

struct PluginFiles {
    std::string manifest;
    std::string signature; // empty: none
};

std::optional<std::string> ReadSmall(const fs::path& path, std::uint64_t limit) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec || size > limit) {
        return std::nullopt;
    }
    return ReadTextFile(path);
}

// plugin.json and its signature from a package, from a folder, or plugin.json itself.
Result<PluginFiles> ReadPluginFiles(const fs::path& path, const fs::path& temp) {
    std::error_code ec;
    PluginFiles files;
    if (fs::is_directory(path, ec) || path.filename() == "plugin.json") {
        const fs::path dir = fs::is_directory(path, ec) ? path : path.parent_path();
        const auto manifest = ReadSmall(dir / "plugin.json", MAX_MANIFEST_SIZE);
        if (!manifest) {
            return Status::Fail("There is no plugin.json in this folder.");
        }
        files.manifest = *manifest;
        files.signature = ReadSmall(dir / SIGNATURE_FILE, MAX_SIGNATURE_SIZE).value_or(std::string());
        return files;
    }
    if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) > MAX_PACKAGE_SIZE) {
        return Status::Fail("This is not a plugin package.");
    }
    fs::create_directories(temp, ec);
    const fs::path manifest = temp / "picked-plugin.json";
    const fs::path signature = temp / "picked-plugin.json.sig";
    const Status unpacked = UnpackEntry(path, "plugin.json", manifest, MAX_MANIFEST_SIZE);
    const auto text = unpacked.ok ? ReadTextFile(manifest) : std::nullopt;
    if (text && UnpackEntry(path, SIGNATURE_FILE, signature, MAX_SIGNATURE_SIZE).ok) {
        files.signature = ReadTextFile(signature).value_or(std::string());
    }
    fs::remove(manifest, ec);
    fs::remove(signature, ec);
    if (!text) {
        return Status::Fail("This is not a plugin package: it has no readable plugin.json.");
    }
    files.manifest = *text;
    return files;
}

bool SignedBy(const std::string& manifest, const std::string& signature, const std::vector<std::string>& keys) {
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size());
    return !signature.empty() && std::any_of(keys.begin(), keys.end(), [&](const std::string& key) {
               return VerifyEd25519(bytes, signature, key);
           });
}

// The rules for an unsigned plugin of a file.
Status CheckUnsigned(const Manifest& manifest) {
    if (manifest.id.starts_with("emuden.")) {
        return Status::Fail(manifest.id + " is one of EmuDEN's own plugins, but this file is not signed by EmuDEN.");
    }
    return CheckDeveloperManifest(manifest);
}

} // namespace

std::string ManualPackagePath(const std::string& plugin_id) {
    return "manual/" + plugin_id + ".emuplugin";
}

Result<PackageProbe> RealHost::ProbePackage(const fs::path& path) const {
    Result<PluginFiles> files = ReadPluginFiles(path, plugins_dir_ / "tmp");
    if (!files) {
        return files.status();
    }
    if (IsFeatureManifest(files->manifest)) {
        return Status::Fail("Only console plugins are installed from a file.");
    }
    Result<Manifest> manifest = ParseManifest(files->manifest);
    if (!manifest) {
        return Status::Fail("EmuDEN cannot read this plugin: " + manifest.error());
    }
    PackageProbe probe;
    probe.path = path;
    probe.id = manifest->id;
    probe.name = manifest->name;
    probe.version = manifest->version.Text();
    probe.publisher = manifest->publisher;
    probe.summary = manifest->summary;
    probe.manifest = files->manifest;
    probe.signature = files->signature;
    probe.official = SignedBy(files->manifest, files->signature, deps_.keys);
    for (const SystemDef& system : manifest->systems) {
        probe.systems.push_back(system.console.id);
        probe.consoles.push_back(system.console.name);
        probe.engine = probe.engine || system.console.runner == Runner::Engine;
    }
    if (!probe.official) {
        if (const Status status = CheckUnsigned(*manifest); !status.ok) {
            return status;
        }
    }
    std::lock_guard lock(mutex_);
    if (const auto reason = RevokedFor(views_, 0, manifest->id, manifest->version)) {
        return Status::Fail(manifest->name + " was withdrawn: " + *reason);
    }
    const auto it = std::find_if(manifests_.begin(), manifests_.end(),
                                 [&](const Loaded& l) { return l.manifest.id == manifest->id; });
    if (it != manifests_.end()) {
        if (it->developer) {
            return Status::Fail(manifest->name + " is in the developer folder; that copy is the one used.");
        }
        if (probe.official == it->local || OriginOf(*it) > 0) {
            return Status::Fail(manifest->name + " is installed already from " +
                                (OriginOf(*it) > 0 ? views_[OriginOf(*it)].label : std::string("EmuDEN")) +
                                "; this file is not signed like it.");
        }
        if (probe.official && it->manifest.version > manifest->version) {
            return Status::Fail("A newer " + manifest->name + " (" + it->manifest.version.Text() +
                                ") is installed already.");
        }
        probe.replaces = it->manifest.version.Text();
    }
    return probe;
}

Status RealHost::InstallPackage(const PackageProbe& probe) {
    // Checked again: the file may have been changed since it was probed.
    Result<Manifest> manifest = ParseManifest(probe.manifest);
    const bool official = SignedBy(probe.manifest, probe.signature, deps_.keys);
    if (!manifest || manifest->id != probe.id) {
        return Status::Fail("EmuDEN cannot read this plugin.");
    }
    if (!official) {
        if (const Status status = CheckUnsigned(*manifest); !status.ok) {
            return status;
        }
    }
    const fs::path dir = (official ? plugins_dir_ : local_dir_) / PathFromUtf8(probe.id);
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove(dir / ORIGIN_FILE, ec); // EmuDEN's own, as from the official list
    if (!WriteFileAtomic(dir / "plugin.json", probe.manifest)) {
        return Status::Fail("The plugin could not be saved.");
    }
    std::lock_guard lock(mutex_);
    if (!official) {
        std::erase_if(manifests_, [&](const Loaded& l) { return l.local && l.manifest.id == probe.id; });
    }
    log::Info("plugins", "{} {} installed from a file ({})", probe.id, probe.version,
              official ? "signed by EmuDEN" : "unsigned");
    AddManifest(manifest.take(), official, views_[0].address, false, !official);
    std::erase_if(carry_overs_, [&](const CarryOver& c) { return c.plugin_id == probe.id; });
    SaveCarryOvers();
    checked_.clear();
    ++revision_;
    return {};
}

void RealHost::LoadLocalManifests() {
    for (const fs::path& dir : ListDirectory(local_dir_)) {
        const auto text = ReadTextFile(dir / "plugin.json");
        if (!text) {
            continue;
        }
        Result<Manifest> manifest = ParseManifest(*text);
        Status status = manifest ? CheckUnsigned(*manifest) : manifest.status();
        if (status.ok && manifest->id != PathToUtf8(dir.filename())) {
            status = Status::Fail("the folder is not named after the plugin");
        }
        if (!status.ok) {
            log::Warn("plugins", "installed from a file, {}: {}", PathToUtf8(dir.filename()), status.error);
            continue;
        }
        AddManifest(manifest.take(), false, views_[0].address, false, true);
    }
}

// Before this file existed every console plugin of the official list was on the device: built
// in, or brought by the list. One the app does not bring now came from an earlier version.
void RealHost::FindCarryOvers() {
    const fs::path file = plugins_dir_ / CARRY_OVER_FILE;
    if (const auto text = ReadTextFile(file)) {
        if (const auto root = ParseJson(*text); root && root->Find("plugins") != nullptr) {
            for (const JsonValue& item : root->Find("plugins")->items) {
                carry_overs_.push_back({item.String("id"), item.String("name"), json::Strings(item, "systems", false),
                                        json::Bool(item, "installed", false)});
            }
        }
        return;
    }
    std::set<std::string> bundled;
    if (deps_.bundled) {
        std::size_t count = 0;
        const BundledManifest* manifests = BundledManifests(count);
        for (std::size_t i = 0; i < count; ++i) {
            bundled.insert(manifests[i].id);
        }
    }
    if (views_[0].index) {
        for (const IndexPlugin& plugin : views_[0].index->plugins) {
            if (plugin.kind != "console" || bundled.contains(plugin.id)) {
                continue;
            }
            const auto it = std::find_if(manifests_.begin(), manifests_.end(),
                                         [&](const Loaded& l) { return l.manifest.id == plugin.id; });
            if (it == manifests_.end() || (it->from_index && !it->local && !it->developer && OriginOf(*it) == 0)) {
                carry_overs_.push_back({plugin.id, plugin.name, plugin.systems, it != manifests_.end()});
            }
        }
    }
    SaveCarryOvers();
}

void RealHost::SaveCarryOvers() const {
    std::vector<JsonValue> items;
    for (const CarryOver& carry : carry_overs_) {
        items.push_back(JsonObject({{"id", JsonString(carry.plugin_id)},
                                    {"name", JsonString(carry.name)},
                                    {"systems", json::StringArray(carry.systems)},
                                    {"installed", JsonBool(carry.installed)}}));
    }
    WriteFileAtomic(plugins_dir_ / CARRY_OVER_FILE, WriteJson(JsonObject({{"plugins", JsonArray(std::move(items))}})));
}

std::vector<CarryOver> RealHost::CarryOvers() const {
    std::lock_guard lock(mutex_);
    return carry_overs_;
}

JobPtr RealHost::SettleCarryOver(const std::string& plugin_id, bool keep) {
    return config_.jobs->Run("Keep " + plugin_id, [this, plugin_id, keep](Job& job) {
        Status status = keep ? KeepCarryOver(job, plugin_id) : Status{};
        if (status.ok) {
            std::lock_guard lock(mutex_);
            std::erase_if(carry_overs_, [&](const CarryOver& c) { return c.plugin_id == plugin_id; });
            SaveCarryOvers();
        }
        return status;
    });
}

// Kept as the signed package beside the official list has it (no list pins it, its signature
// does), unless the list offers it still: then it is updated from there as before. A copy the
// device has stays when there is no such package.
Status RealHost::KeepCarryOver(Job& job, const std::string& plugin_id) {
    std::string url, address;
    bool installed = false;
    {
        std::lock_guard lock(mutex_);
        url = ResolveSourceUrl(views_[0].index_url, ManualPackagePath(plugin_id));
        address = views_[0].address;
        installed = std::any_of(manifests_.begin(), manifests_.end(),
                                [&](const Loaded& l) { return l.manifest.id == plugin_id; });
    }
    job.SetMessage("Keeping " + plugin_id);
    if (const Status fetched = FetchSource(job, address, true); !fetched.ok) {
        return fetched;
    }
    {
        std::lock_guard lock(mutex_);
        if (views_[0].Offers() && views_[0].index->Find(plugin_id) != nullptr) {
            return {};
        }
    }
    pal::HttpRequest request;
    request.url = url;
    request.max_body = MAX_PACKAGE_SIZE;
    request.progress = [&job](std::uint64_t, std::uint64_t) { return !job.CancelRequested(); };
    const pal::HttpResponse response = deps_.http(request);
    if (response.status == 404 && installed) {
        return {};
    }
    if (!response.ok()) {
        return Status::Fail("The plugin could not be fetched" +
                            (response.error.empty() ? std::string(".") : ": " + response.error));
    }
    const fs::path temp = plugins_dir_ / "tmp" / PathFromUtf8(plugin_id + ".emuplugin");
    std::error_code ec;
    fs::create_directories(temp.parent_path(), ec);
    if (!WriteFileAtomic(temp, response.body)) {
        return Status::Fail("The plugin could not be saved.");
    }
    Result<PackageProbe> probe = ProbePackage(temp);
    fs::remove(temp, ec);
    if (!probe) {
        // The copy on the device is as new as the package, or newer.
        return installed && probe.error().starts_with("A newer ") ? Status{} : probe.status();
    }
    if (!probe->official || probe->id != plugin_id) {
        return Status::Fail("The plugin found is not the one EmuDEN signed.");
    }
    return InstallPackage(*probe);
}

} // namespace den::plugins
