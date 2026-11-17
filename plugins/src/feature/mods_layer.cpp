// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.mods.layer: Vulkan implicit layers. The libraries are kept in <root>/layers/<name>/,
// the manifests are written to the user's implicit layer folder with library_path pointing
// at the copies, and <root>/layers/<name>/record.json says what was written.

#include <algorithm>
#include <cctype>
#include <system_error>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/json.h"
#include "den/base/sha256.h"
#include "feature/mods.h"
#include "feature/ui_model.h"

namespace den::plugins::feature::mods {

namespace {

struct Written {
    std::string file, sha256;
};

struct LayerRecord {
    std::string name;
    std::int64_t time = 0;
    fs::path manifest_dir;
    std::vector<Written> manifests, libraries;
};

JsonValue ToJson(const std::vector<Written>& files) {
    JsonValue out = JsonArray();
    for (const Written& file : files) {
        out.items.push_back(JsonObject({{"file", JsonString(file.file)}, {"sha256", JsonString(file.sha256)}}));
    }
    return out;
}

std::vector<Written> FromJson(const JsonValue* list) {
    std::vector<Written> out;
    for (const JsonValue& item : list != nullptr ? list->items : std::vector<JsonValue>{}) {
        if (IsPlainName(item.String("file"))) {
            out.push_back({item.String("file"), item.String("sha256")});
        }
    }
    return out;
}

bool Write(const fs::path& home, const LayerRecord& record) {
    return WriteFileAtomic(home / "record.json",
                           WriteJson(JsonObject({{"version", JsonNumber(1)},
                                                 {"name", JsonString(record.name)},
                                                 {"time", JsonNumber(static_cast<double>(record.time))},
                                                 {"manifest_dir", JsonString(PathToUtf8(record.manifest_dir))},
                                                 {"manifests", ToJson(record.manifests)},
                                                 {"libraries", ToJson(record.libraries)}})));
}

std::optional<LayerRecord> Read(const fs::path& home) {
    const auto text = ReadTextFile(home / "record.json");
    const auto root = text ? ParseJson(*text) : std::nullopt;
    if (!root || root->Find("manifests") == nullptr) {
        return std::nullopt;
    }
    LayerRecord record;
    record.name = root->String("name");
    if (const JsonValue* time = root->Find("time"); time != nullptr && time->kind == JsonValue::Kind::Number) {
        record.time = static_cast<std::int64_t>(time->number);
    }
    record.manifest_dir = PathFromUtf8(root->String("manifest_dir"));
    record.manifests = FromJson(root->Find("manifests"));
    record.libraries = FromJson(root->Find("libraries"));
    return record;
}

// A file name made of letters, digits, ".", "-" and "_".
bool IsSimpleName(std::string_view name, std::size_t max) {
    return !name.empty() && name.size() <= max && name.front() != '.' && name.back() != '.' &&
           std::all_of(name.begin(), name.end(), [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-' || c == '_';
           });
}

bool IsFile(const fs::path& path, std::uint64_t max) {
    std::error_code ec;
    return fs::is_regular_file(fs::symlink_status(path, ec)) && fs::file_size(path, ec) <= max && !ec;
}

// One layer object of a manifest: what the loader needs, and a way to switch it off.
Status CheckLayer(const JsonValue& layer) {
    if (layer.kind != JsonValue::Kind::Object || layer.String("name").empty() ||
        layer.String("library_path").empty()) {
        return Status::Fail("the file is not a Vulkan layer manifest");
    }
    const std::string type = layer.String("type");
    if (type != "GLOBAL" && type != "INSTANCE") {
        return Status::Fail("the file is not a Vulkan layer manifest");
    }
    const JsonValue* off = layer.Find("disable_environment");
    if (off == nullptr || off->kind != JsonValue::Kind::Object || off->members.empty() ||
        off->members.front().first.empty()) {
        return Status::Fail("the layer " + layer.String("name") +
                            " has no disable_environment: it could not be switched off per game");
    }
    return {};
}

// The end of the JSON string that starts at text[at] (a quote): one past its closing quote.
std::size_t StringEnd(std::string_view text, std::size_t at) {
    for (std::size_t i = at + 1; i < text.size(); ++i) {
        if (text[i] == '\\') {
            ++i;
        } else if (text[i] == '"') {
            return i + 1;
        }
    }
    return std::string_view::npos;
}

std::size_t SkipSpace(std::string_view text, std::size_t at) {
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) != 0) {
        ++at;
    }
    return at;
}

struct LayerFiles {
    std::string manifest; // the name it is written under
    std::string library;  // its file name in the layer's folder
    std::string text;     // the manifest as it is written
};

} // namespace

bool IsLayerName(std::string_view name) {
    return IsSimpleName(name, 48);
}

Result<std::string> RewriteLayerManifest(std::string_view text, std::string_view library_path) {
    const auto root = ParseJson(text);
    if (!root || root->kind != JsonValue::Kind::Object || root->String("file_format_version").empty()) {
        return Status::Fail("the file is not a Vulkan layer manifest");
    }
    std::size_t layers = 0;
    if (const JsonValue* one = root->Find("layer")) {
        if (const Status ok = CheckLayer(*one); !ok.ok) {
            return ok;
        }
        layers = 1;
    } else if (const JsonValue* many = root->Find("layers");
               many != nullptr && many->kind == JsonValue::Kind::Array && !many->items.empty()) {
        for (const JsonValue& layer : many->items) {
            if (const Status ok = CheckLayer(layer); !ok.ok) {
                return ok;
            }
        }
        layers = many->items.size();
    } else {
        return Status::Fail("the file is not a Vulkan layer manifest");
    }
    // The text is kept as it is; only the value after each "library_path" key is replaced.
    const std::string value = CompactJson(JsonString(std::string(library_path)));
    std::string out;
    std::size_t replaced = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] != '"') {
            out += text[at++];
            continue;
        }
        const std::size_t end = StringEnd(text, at);
        if (end == std::string_view::npos) {
            return Status::Fail("the file is not a Vulkan layer manifest");
        }
        out += text.substr(at, end - at);
        const bool key = text.substr(at, end - at) == "\"library_path\"";
        at = end;
        const std::size_t colon = SkipSpace(text, at);
        if (!key || colon >= text.size() || text[colon] != ':') {
            continue;
        }
        const std::size_t start = SkipSpace(text, colon + 1);
        const std::size_t stop = start < text.size() && text[start] == '"' ? StringEnd(text, start) : std::string_view::npos;
        if (stop == std::string_view::npos) {
            return Status::Fail("the file is not a Vulkan layer manifest");
        }
        out += text.substr(at, start - at);
        out += value;
        at = stop;
        ++replaced;
    }
    if (replaced != layers) {
        return Status::Fail("the file is not a Vulkan layer manifest");
    }
    return out;
}

namespace {

// Checks the layers and makes the manifests as they would be written for `home`.
Result<std::vector<LayerFiles>> Prepare(const std::string& name, const std::vector<LayerOrder>& layers,
                                        const std::string& plugin, const fs::path& home) {
    if (!IsLayerName(name)) {
        return Status::Fail("a layer's name is made of letters, digits, '.', '-' and '_'");
    }
    if (layers.empty() || layers.size() > MAX_LAYERS) {
        return Status::Fail("layer takes 1 to 8 layers");
    }
    std::vector<LayerFiles> out;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const LayerOrder& layer = layers[i];
        LayerFiles files;
        const std::string manifest = PathToUtf8(layer.manifest.filename());
        files.library = PathToUtf8(layer.library.filename());
        if (!IsSimpleName(manifest, 100) || !ToLower(manifest).ends_with(".json") || !IsFile(layer.manifest, MAX_MANIFEST)) {
            return Status::Fail(manifest + " is not a layer manifest (a .json file)");
        }
        if (!IsSimpleName(files.library, 100) || files.library == "record.json" || !IsFile(layer.library, MAX_ENTRY)) {
            return Status::Fail(files.library + " is not a layer's library");
        }
        files.manifest = "emuden-" + plugin + "-" + manifest;
        for (std::size_t j = 0; j < i; ++j) {
            if (EqualsNoCase(out[j].manifest, files.manifest)) {
                return Status::Fail(manifest + " is named twice");
            }
            if (EqualsNoCase(out[j].library, files.library) && layers[j].library != layer.library) {
                return Status::Fail("two libraries are named " + files.library);
            }
        }
        const auto text = ReadTextFile(layer.manifest);
        Result<std::string> rewritten =
            RewriteLayerManifest(text.value_or(std::string()), PathToUtf8(home / PathFromUtf8(files.library)));
        if (!rewritten) {
            return Status::Fail(manifest + ": " + rewritten.error());
        }
        files.text = rewritten.take();
        out.push_back(std::move(files));
    }
    return out;
}

} // namespace

Result<std::vector<std::string>> Store::PlanLayer(const std::string& name, const std::vector<LayerOrder>& layers,
                                                  const std::string& plugin) const {
    const Result<std::vector<LayerFiles>> prepared = Prepare(name, layers, plugin, root_ / "layers" / name);
    if (!prepared) {
        return prepared.status();
    }
    std::vector<std::string> out;
    for (const LayerFiles& files : prepared.value()) {
        out.push_back(files.manifest);
    }
    return out;
}

Status Store::ApplyLayer(const std::string& name, const std::vector<LayerOrder>& layers, const fs::path& manifest_dir,
                         const std::string& plugin, std::int64_t time) {
    std::error_code ec;
    if (manifest_dir.empty() || !manifest_dir.is_absolute()) {
        return Status::Fail("this computer has no folder for Vulkan layers");
    }
    const fs::path home = fs::absolute(root_ / "layers" / PathFromUtf8(name), ec);
    const Result<std::vector<LayerFiles>> prepared = Prepare(name, layers, plugin, home);
    if (!prepared) {
        return prepared.status();
    }
    if (const Status undone = RemoveLayer(name); !undone.ok) {
        return Status::Fail("the earlier setup could not be undone: " + undone.error);
    }
    LayerRecord record;
    record.name = name;
    record.time = time;
    record.manifest_dir = manifest_dir;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const LayerFiles& files = prepared.value()[i];
        record.manifests.push_back({files.manifest, Sha256Hex(files.text.data(), files.text.size())});
        const bool listed = std::any_of(record.libraries.begin(), record.libraries.end(),
                                        [&](const Written& w) { return w.file == files.library; });
        if (!listed) {
            record.libraries.push_back({files.library, Sha256File(layers[i].library).value_or(std::string())});
        }
    }
    // The record first: a setup cut short can be removed.
    fs::create_directories(home, ec);
    if (ec || !Write(home, record)) {
        fs::remove_all(home, ec);
        return Status::Fail("the record of the layer could not be written");
    }
    const auto failed = [&](const std::string& what) {
        const Status undone = RemoveLayer(name);
        return Status::Fail(what + (undone.ok ? "" : "; undoing it failed: " + undone.error));
    };
    fs::create_directories(manifest_dir, ec);
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const LayerFiles& files = prepared.value()[i];
        fs::copy_file(layers[i].library, home / PathFromUtf8(files.library), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return failed(files.library + " could not be copied");
        }
        if (!WriteFileAtomic(manifest_dir / PathFromUtf8(files.manifest), files.text)) {
            return failed(files.manifest + " could not be written");
        }
    }
    return {};
}

std::optional<LayerState> Store::Layer(const std::string& name) const {
    const fs::path home = root_ / "layers" / PathFromUtf8(name);
    const std::optional<LayerRecord> record = IsLayerName(name) ? Read(home) : std::nullopt;
    if (!record) {
        return std::nullopt;
    }
    LayerState state;
    state.time = record->time;
    state.intact = true;
    for (const Written& manifest : record->manifests) {
        state.manifests.push_back(manifest.file);
        state.intact = state.intact && Sha256File(record->manifest_dir / PathFromUtf8(manifest.file)) == manifest.sha256;
    }
    for (const Written& library : record->libraries) {
        state.intact = state.intact && Sha256File(home / PathFromUtf8(library.file)) == library.sha256;
    }
    return state;
}

Status Store::RemoveLayer(const std::string& name) {
    if (!IsLayerName(name)) {
        return Status::Fail("a layer's name is made of letters, digits, '.', '-' and '_'");
    }
    const fs::path home = root_ / "layers" / PathFromUtf8(name);
    const std::optional<LayerRecord> record = Read(home);
    std::error_code ec;
    if (!record) {
        return {};
    }
    for (const Written& manifest : record->manifests) {
        // Only names this app gives are deleted from the shared folder.
        if (!manifest.file.starts_with("emuden-")) {
            continue;
        }
        const fs::path file = record->manifest_dir / PathFromUtf8(manifest.file);
        fs::remove(file, ec);
        if (ec && fs::exists(fs::symlink_status(file, ec))) {
            return Status::Fail(manifest.file + " could not be deleted");
        }
    }
    fs::remove_all(home, ec);
    return ec ? Status::Fail("the layer's libraries could not be deleted (is a game running?)") : Status();
}

std::vector<std::string> Store::Layers() const {
    std::vector<std::string> out;
    for (const fs::path& home : ListDirectory(root_ / "layers")) {
        const std::string name = PathToUtf8(home.filename());
        if (IsLayerName(name) && IsRegularFile(home / "record.json")) {
            out.push_back(name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace den::plugins::feature::mods
