// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/plugins/manifest.h"
#include "json_util.h"
#include "manifest_parts.h"

namespace den::plugins {

Result<Download> ParseDownload(const JsonValue& value) {
    static const char* const KNOWN[] = {"url",  "sha256",           "size",   "archive",
                                        "entry", "strip_components", "rename", "executable_bit"};
    if (value.kind != JsonValue::Kind::Object) {
        return Status::Fail("a download is not an object");
    }
    for (const auto& member : value.members) {
        if (std::none_of(std::begin(KNOWN), std::end(KNOWN),
                         [&](const char* key) { return member.first == key; })) {
            return Status::Fail("a download has the unknown field \"" + member.first + "\"");
        }
    }
    Download download;
    download.url = value.String("url");
    download.sha256 = ToLower(value.String("sha256"));
    download.size = static_cast<std::uint64_t>(json::Number(value, "size", 0));
    download.archive = value.String("archive");
    download.entry = value.String("entry");
    if (!download.url.starts_with("https://") || download.url.find('@') != std::string::npos) {
        return Status::Fail("a download is not a plain https address");
    }
    if (!IsSha256Hex(download.sha256) || download.size == 0) {
        return Status::Fail("a download has no checksum or size");
    }
    if (download.archive != "zip" && download.archive != "none") {
        return Status::Fail("a download has an unknown archive type");
    }
    if (download.archive == "zip" && !IsSafeRelativePath(download.entry)) {
        return Status::Fail("a download names no file to unpack");
    }
    return download;
}

namespace {

bool IsPluginId(std::string_view id) {
    if (id.size() < 3 || id.size() > 64 || id.find('.') == std::string_view::npos) {
        return false;
    }
    bool start = true;
    for (const char c : id) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (start ? !alnum : !(alnum || c == '-' || c == '.')) {
            return false;
        }
        start = c == '.';
    }
    return !start;
}

bool IsSystemId(std::string_view id) {
    if (id.size() < 2 || id.size() > 32 || id.front() == '-') {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}

// A component id becomes a file name, so it is kept to plain characters.
bool IsComponentId(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

Result<Component> ParseComponent(const JsonValue& value) {
    Component component;
    component.id = value.String("id");
    component.type = value.String("type");
    component.name = value.String("name");
    component.version = value.String("version");
    component.opengl = value.String("gpu") == "opengl";
    if (!IsComponentId(component.id)) {
        return Status::Fail("a component has a bad id");
    }
    if (component.name.empty()) {
        component.name = component.id;
    }
    if (const JsonValue* options = value.Find("forced_core_options")) {
        for (const auto& [key, option] : options->members) {
            if (option.kind == JsonValue::Kind::String) {
                component.forced_core_options.emplace_back(key, option.string);
            }
        }
    }
    if (const JsonValue* widescreen = value.Find("widescreen")) {
        if (const JsonValue* options = widescreen->Find("options")) {
            for (const auto& [key, option] : options->members) {
                if (option.kind == JsonValue::Kind::String) {
                    component.widescreen_options.emplace_back(key, option.string);
                }
            }
        }
        if (const JsonValue* restart = widescreen->Find("restart")) {
            component.widescreen_restart = restart->kind == JsonValue::Kind::Bool && restart->boolean;
        }
    }
    if (const JsonValue* games = value.Find("games")) {
        const JsonValue* offset = games->Find("offset");
        const JsonValue* length = games->Find("length");
        if (offset != nullptr && length != nullptr && offset->kind == JsonValue::Kind::Number &&
            length->kind == JsonValue::Kind::Number && offset->number >= 0 && offset->number < (1 << 24) &&
            length->number >= 1 && length->number <= 64) {
            component.games.offset = static_cast<std::uint64_t>(offset->number);
            component.games.length = static_cast<std::uint32_t>(length->number);
            for (std::string& code : json::Strings(*games, "codes", false)) {
                if (code.size() == component.games.length) {
                    component.games.codes.push_back(std::move(code));
                }
            }
        }
    }
    component.cores = json::Strings(value, "cores", false);
    if (component.type == TYPE_ENGINE_APP) {
        Result<EngineAppSpec> spec = ParseEngineApp(value);
        if (!spec) {
            return Status::Fail(component.id + ": " + spec.error());
        }
        component.engine_app = spec.take();
    }
    if (const JsonValue* downloads = value.Find("downloads")) {
        for (const auto& [platform, entry] : downloads->members) {
            Result<Download> download = ParseDownload(entry);
            // System files are a folder of a zip; a download that is not is left out, and
            // the console still plays.
            if (component.type == TYPE_SYSTEM_FILES &&
                (!download || download->archive != "zip" || !download->entry.ends_with("/"))) {
                continue;
            }
            if (!download) {
                return Status::Fail(component.id + ": " + download.error());
            }
            component.downloads.emplace(platform, download.take());
        }
    }
    return component;
}

void ParseContent(const JsonValue& content, Console& console) {
    console.extensions = json::Strings(content, "extensions", true);
    ScanRules& scan = console.scan;
    scan.compound_extensions = json::Strings(content, "compound_extensions", true);
    scan.folder_aliases = json::Strings(content, "folder_aliases", true);
    scan.folder_only_extensions = json::Strings(content, "folder_only_extensions", true);
    scan.cover_is_rom = json::Bool(content, "cover_is_rom", false);
    scan.scanner = content.String("scanner");
    const std::string zip = content.String("zip");
    if (zip == "unpack" || zip == "pass" || zip == "no") {
        scan.zip = zip;
    }
    if (const JsonValue* multi = content.Find("multi_file");
        multi != nullptr && multi->kind == JsonValue::Kind::Object) {
        scan.index_extensions = json::Strings(*multi, "index", true);
        scan.member_extensions = json::Strings(*multi, "members", true);
        scan.loose_members = json::Bool(*multi, "loose_members", false);
        scan.track_naming = multi->String("track_naming");
    }
    if (const JsonValue* titles = content.Find("header_title")) {
        for (const JsonValue& title : titles->items) {
            const double offset = json::Number(title, "offset", -1);
            const double length = json::Number(title, "length", 0);
            if (offset >= 0 && length > 0 && length <= 256) {
                scan.header_title.push_back(
                    {static_cast<std::uint64_t>(offset), static_cast<std::uint32_t>(length)});
            }
        }
    }
}

void ParseBios(const JsonValue& sets, Console& console) {
    for (const JsonValue& set : sets.items) {
        BiosSet out;
        out.label = set.String("label");
        out.any_one = json::Bool(set, "any_one", false);
        out.cores = json::Strings(set, "cores", false);
        if (const JsonValue* files = set.Find("files")) {
            for (const JsonValue& file : files->items) {
                const std::string path = file.String("path");
                if (path.starts_with("@") || IsSafeRelativePath(path)) {
                    out.files.push_back({path, json::Bool(file, "optional", false)});
                }
            }
        }
        if (!out.files.empty()) {
            console.bios.push_back(std::move(out));
        }
    }
}

Result<SystemDef> ParseSystem(const JsonValue& value, const Manifest& manifest) {
    SystemDef system;
    Console& console = system.console;
    console.id = value.String("id");
    console.name = value.String("name");
    if (!IsSystemId(console.id) || console.name.empty()) {
        return Status::Fail("a system has a bad id or no name");
    }
    console.short_name = value.String("short_name");
    if (console.short_name.empty()) {
        console.short_name = console.name;
    }
    console.plugin_id = manifest.id;
    console.runner = manifest.engine == "libretro" ? Runner::Core : Runner::Engine;
    console.group = value.String("group");
    console.order = static_cast<int>(json::Number(value, "order", 0));
    if (const JsonValue* content = value.Find("content")) {
        ParseContent(*content, console);
    }
    if (const JsonValue* presentation = value.Find("presentation")) {
        const double aspect = json::Number(*presentation, "cover_aspect", 1.0);
        console.cover_aspect = aspect > 0.1 && aspect < 10 ? static_cast<float>(aspect) : 1.0f;
        console.handheld = json::Bool(*presentation, "handheld", false);
        console.right_stick_holds = json::Bool(*presentation, "right_stick_holds", false);
        if (const JsonValue* buttons = presentation->Find("buttons")) {
            for (const JsonValue& button : buttons->items) {
                console.buttons.push_back({button.String("pad"), button.String("label")});
            }
        }
    }
    if (const JsonValue* artwork = value.Find("artwork")) {
        console.thumbnails = artwork->String("libretro_thumbnails");
        console.rdb = artwork->String("rdb");
        console.launchbox = artwork->String("launchbox");
        console.cover_by_id = json::Strings(*artwork, "cover_by_id", false);
    }
    if (const JsonValue* achievements = value.Find("achievements")) {
        console.rcheevos_console = static_cast<int>(json::Number(*achievements, "rcheevos_console", 0));
    }
    if (const JsonValue* bios = value.Find("bios")) {
        ParseBios(*bios, console);
    }
    system.cores = json::Strings(value, "cores", false);
    return system;
}

std::string ScalarText(const JsonValue& value) {
    switch (value.kind) {
    case JsonValue::Kind::String:
        return value.string;
    case JsonValue::Kind::Bool:
        return value.boolean ? "true" : "false";
    case JsonValue::Kind::Number: {
        if (value.number == std::floor(value.number)) {
            return std::to_string(static_cast<long long>(value.number));
        }
        return std::to_string(value.number);
    }
    default:
        return {};
    }
}

void ParseSettings(const JsonValue& settings, Manifest& manifest) {
    for (const JsonValue& value : settings.items) {
        ManifestSetting setting;
        SettingSpec& spec = setting.spec;
        spec.key = value.String("key");
        spec.label = value.String("label");
        spec.type = value.String("type");
        if (spec.key.empty() || spec.label.empty() || spec.type.empty()) {
            continue; // a setting bound to the engine's own has no label here
        }
        spec.help = value.String("help");
        spec.suffix = value.String("suffix");
        spec.min = json::Number(value, "min", 0);
        spec.max = json::Number(value, "max", 0);
        spec.step = json::Number(value, "step", 1);
        if (const JsonValue* fallback = value.Find("default")) {
            spec.default_value = ScalarText(*fallback);
        }
        if (const JsonValue* choices = value.Find("choices")) {
            for (const JsonValue& choice : choices->items) {
                const JsonValue* choice_value = choice.Find("value");
                spec.choices.emplace_back(choice_value ? ScalarText(*choice_value) : std::string(),
                                          choice.String("label"));
            }
        }
        setting.systems = json::Strings(value, "systems", false);
        manifest.settings.push_back(std::move(setting));
    }
}

} // namespace

std::string Version::Text() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::optional<Version> Version::Parse(std::string_view text) {
    int parts[3] = {0, 0, 0};
    int part = 0;
    int digits = 0;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            if (++digits > 6) {
                return std::nullopt;
            }
            parts[part] = parts[part] * 10 + (c - '0');
        } else if (c == '.' && digits > 0 && part < 2) {
            ++part;
            digits = 0;
        } else {
            return std::nullopt;
        }
    }
    if (part != 2 || digits == 0) {
        return std::nullopt;
    }
    return Version{parts[0], parts[1], parts[2]};
}

bool VersionInRange(const Version& version, std::string_view range) {
    const std::string text = Trim(range);
    if (text == "*") {
        return true;
    }
    if (text.empty()) {
        return false;
    }
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find(' ', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string_view term(text.data() + at, end - at);
        at = end + 1;
        if (term.empty()) {
            continue;
        }
        std::string_view op = "=";
        for (const std::string_view candidate : {">=", "<=", ">", "<", "="}) {
            if (term.starts_with(candidate)) {
                op = candidate;
                term.remove_prefix(candidate.size());
                break;
            }
        }
        const auto bound = Version::Parse(term);
        if (!bound) {
            return false;
        }
        const bool holds = op == ">="  ? version >= *bound
                           : op == "<=" ? version <= *bound
                           : op == ">"  ? version > *bound
                           : op == "<"  ? version < *bound
                                        : version == *bound;
        if (!holds) {
            return false;
        }
    }
    return true;
}

bool IsSafeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 512 || path.front() == '/' ||
        path.find_first_of("\\:") != std::string_view::npos) {
        return false;
    }
    std::size_t at = 0;
    while (at <= path.size()) {
        std::size_t end = path.find('/', at);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view part = path.substr(at, end - at);
        // An empty last part is the trailing slash of a folder.
        if (part == ".." || part == "." || (part.empty() && end != path.size())) {
            return false;
        }
        at = end + 1;
    }
    return std::none_of(path.begin(), path.end(),
                        [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

bool IsSha256Hex(std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

const std::vector<std::string>& HostCapabilities() {
    static const std::vector<std::string> capabilities = {
        "engine.libretro",    "engine.switch2", "settings.v1",
        "content.multi_file", "content.magic",  "presentation.buttons",
        "kind.feature",       "feature.lua.1",  "feature.web.1",
        "core.engine-app.1",  "kind.interface", "interface.1",
        "feature.tune.1",     "feature.achievements.1",
    };
    return capabilities;
}

const Download* Component::DownloadFor(std::string_view platform_key) const {
    auto it = downloads.find(platform_key);
    if (it == downloads.end()) {
        it = downloads.find(std::string_view("any"));
    }
    return it == downloads.end() ? nullptr : &it->second;
}

const Component* Manifest::FindComponent(std::string_view component_id) const {
    const auto it = std::find_if(components.begin(), components.end(),
                                 [&](const Component& c) { return c.id == component_id; });
    return it == components.end() ? nullptr : &*it;
}

std::vector<const Component*> Manifest::CoresFor(const SystemDef& system,
                                                 std::string_view platform_key) const {
    std::vector<const Component*> out;
    for (const std::string& core : system.cores) {
        const Component* component = FindComponent(core);
        if (component != nullptr && (component->type == TYPE_CORE || component->type == TYPE_ENGINE_APP) &&
            component->OffersPlatform(platform_key)) {
            out.push_back(component);
        }
    }
    return out;
}

std::vector<const Component*> Manifest::SystemFilesFor(std::string_view core_id,
                                                       std::string_view platform_key) const {
    std::vector<const Component*> out;
    for (const Component& component : components) {
        if (component.type == TYPE_SYSTEM_FILES &&
            component.DownloadFor(platform_key) != nullptr &&
            (component.cores.empty() || std::find(component.cores.begin(), component.cores.end(),
                                                  core_id) != component.cores.end())) {
            out.push_back(&component);
        }
    }
    return out;
}

Result<Manifest> ParseManifest(std::string_view text) {
    if (text.size() > MAX_MANIFEST_SIZE) {
        return Status::Fail("the manifest is too large");
    }
    const std::optional<JsonValue> root = ParseJson(text);
    if (!root || root->kind != JsonValue::Kind::Object) {
        return Status::Fail("the manifest is not JSON");
    }
    Manifest manifest;
    manifest.id = root->String("id");
    manifest.name = root->String("name");
    manifest.kind = root->String("kind");
    manifest.engine = root->String("engine");
    manifest.publisher = root->String("publisher");
    manifest.summary = root->String("summary");
    manifest.api = static_cast<int>(json::Number(*root, "api", 0));
    manifest.needs = json::Strings(*root, "needs", false);
    if (json::Number(*root, "manifest_version", 0) != 1 || manifest.api > API_VERSION) {
        return Status::Fail("Needs a newer EmuDEN");
    }
    const auto version = Version::Parse(root->String("version"));
    if (!IsPluginId(manifest.id) || !version || manifest.name.empty()) {
        return Status::Fail("the manifest has a bad id, name or version");
    }
    manifest.version = *version;
    if (manifest.kind != "console") {
        return Status::Fail(manifest.id + " is not a console plugin");
    }
    if (manifest.engine != "libretro" && manifest.engine != "switch2") {
        return Status::Fail(manifest.id + " uses an engine this version does not have");
    }
    const auto& capabilities = HostCapabilities();
    for (const std::string& need : manifest.needs) {
        if (std::find(capabilities.begin(), capabilities.end(), need) == capabilities.end()) {
            return Status::Fail("Needs a newer EmuDEN (" + need + ")");
        }
    }
    if (const JsonValue* components = root->Find("components")) {
        for (const JsonValue& value : components->items) {
            // A component of a type this version does not know is not its business.
            const std::string type = value.String("type");
            if (type != TYPE_CORE && type != TYPE_ENGINE && type != TYPE_SYSTEM_FILES && type != TYPE_ENGINE_APP) {
                continue;
            }
            Result<Component> component = ParseComponent(value);
            if (!component) {
                return Status::Fail(manifest.id + ": " + component.error());
            }
            manifest.components.push_back(component.take());
        }
    }
    if (const JsonValue* systems = root->Find("systems")) {
        for (const JsonValue& value : systems->items) {
            Result<SystemDef> system = ParseSystem(value, manifest);
            if (!system) {
                return Status::Fail(manifest.id + ": " + system.error());
            }
            manifest.systems.push_back(system.take());
        }
    }
    if (manifest.systems.empty()) {
        return Status::Fail(manifest.id + " has no systems");
    }
    if (const JsonValue* settings = root->Find("settings")) {
        ParseSettings(*settings, manifest);
    }
    return manifest;
}

} // namespace den::plugins
