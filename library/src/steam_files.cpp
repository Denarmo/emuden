// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Steam's own files: KeyValues text and binary, the library folders, shortcuts, the launch
// options and compatibility tool Steam last wrote, and which launch options are safe to set.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

#include "den/base/files.h"
#include "den/library/steam.h"

namespace den::library::steam {

namespace fs = std::filesystem;

const Vdf* Vdf::Find(std::string_view name) const {
    for (const Vdf& child : children) {
        if (EqualsNoCase(child.key, name)) {
            return &child;
        }
    }
    return nullptr;
}

std::string Vdf::Value(std::string_view name) const {
    const Vdf* child = Find(name);
    return child != nullptr ? child->value : std::string();
}

namespace {

class TextReader {
public:
    explicit TextReader(std::string_view text) : text_(text) {}

    // Reads the children up to a closing brace (or the end at the top level).
    bool Block(std::vector<Vdf>& out, bool top, int depth) {
        if (depth > 32) {
            return false;
        }
        while (true) {
            Skip();
            if (pos_ >= text_.size()) {
                return top;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return !top;
            }
            Vdf node;
            if (!Token(node.key)) {
                return false;
            }
            Skip();
            if (pos_ < text_.size() && text_[pos_] == '{') {
                ++pos_;
                if (!Block(node.children, false, depth + 1)) {
                    return false;
                }
            } else if (!Token(node.value)) {
                return false;
            }
            out.push_back(std::move(node));
        }
    }

private:
    void Skip() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                while (pos_ < text_.size() && text_[pos_] != '\n') {
                    ++pos_;
                }
            } else if (c == '[') { // a platform condition ("[$WIN32]"): ignored
                while (pos_ < text_.size() && text_[pos_] != ']') {
                    ++pos_;
                }
                ++pos_;
            } else {
                return;
            }
        }
    }

    bool Token(std::string& out) {
        if (pos_ >= text_.size()) {
            return false;
        }
        if (text_[pos_] != '"') {
            const std::size_t start = pos_;
            while (pos_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[pos_])) &&
                   text_[pos_] != '{' && text_[pos_] != '}' && text_[pos_] != '"') {
                ++pos_;
            }
            out.assign(text_.substr(start, pos_ - start));
            return !out.empty();
        }
        ++pos_;
        while (pos_ < text_.size() && text_[pos_] != '"') {
            char c = text_[pos_++];
            if (c == '\\' && pos_ < text_.size()) {
                const char next = text_[pos_++];
                c = next == 'n' ? '\n' : next == 't' ? '\t' : next;
            }
            out.push_back(c);
        }
        if (pos_ >= text_.size()) {
            return false;
        }
        ++pos_;
        return true;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

class BinaryReader {
public:
    explicit BinaryReader(std::string_view bytes) : bytes_(bytes) {}

    // One map: (type, name, value)... up to the end marker 0x08.
    bool Map(std::vector<std::pair<std::string, std::string>>& fields,
             std::vector<std::vector<std::pair<std::string, std::string>>>* entries, int depth) {
        if (depth > 8) {
            return false;
        }
        while (pos_ < bytes_.size()) {
            const auto type = static_cast<std::uint8_t>(bytes_[pos_++]);
            if (type == 0x08) {
                return true;
            }
            std::string name;
            if (!CString(name)) {
                return false;
            }
            if (type == 0x00) {
                std::vector<std::pair<std::string, std::string>> inner;
                // The list of shortcuts holds one map per shortcut; deeper maps ("tags") are
                // only skipped.
                if (!Map(inner, nullptr, depth + 1)) {
                    return false;
                }
                if (entries != nullptr) {
                    entries->push_back(std::move(inner));
                }
            } else if (type == 0x01) {
                std::string value;
                if (!CString(value)) {
                    return false;
                }
                fields.emplace_back(ToLower(name), std::move(value));
            } else if (type == 0x02) {
                if (pos_ + 4 > bytes_.size()) {
                    return false;
                }
                std::uint32_t value = 0;
                std::memcpy(&value, bytes_.data() + pos_, 4); // little-endian, as every host
                pos_ += 4;
                fields.emplace_back(ToLower(name), std::to_string(value));
            } else {
                return false;
            }
        }
        return false;
    }

private:
    bool CString(std::string& out) {
        const std::size_t end = bytes_.find('\0', pos_);
        if (end == std::string_view::npos) {
            return false;
        }
        out.assign(bytes_.substr(pos_, end - pos_));
        pos_ = end + 1;
        return true;
    }

    std::string_view bytes_;
    std::size_t pos_ = 0;
};

std::optional<Vdf> ReadVdf(const fs::path& file) {
    const auto text = ReadTextFile(file);
    return text ? ParseVdf(*text) : std::nullopt;
}

// A path down the tree, each step case-insensitive.
const Vdf* Walk(const Vdf& root, std::initializer_list<std::string_view> steps) {
    const Vdf* node = &root;
    for (const std::string_view step : steps) {
        node = node->Find(step);
        if (node == nullptr) {
            return nullptr;
        }
    }
    return node;
}

std::vector<fs::path> LibraryFolders(const fs::path& root) {
    std::vector<fs::path> out{root};
    if (const auto vdf = ReadVdf(root / "steamapps" / "libraryfolders.vdf")) {
        if (const Vdf* folders = vdf->Find("libraryfolders")) {
            for (const Vdf& folder : folders->children) {
                const std::string path = folder.children.empty() ? folder.value : folder.Value("path");
                if (!path.empty() && std::find(out.begin(), out.end(), PathFromUtf8(path)) == out.end()) {
                    out.push_back(PathFromUtf8(path));
                }
            }
        }
    }
    return out;
}

std::vector<fs::path> UserDirs(const fs::path& root) {
    std::vector<fs::path> out;
    for (const fs::path& dir : ListDirectory(root / "userdata")) {
        const std::string name = PathToUtf8(dir.filename());
        if (IsDirectory(dir) && !name.empty() && name != "0" &&
            std::all_of(name.begin(), name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            out.push_back(dir);
        }
    }
    return out;
}

bool IsToolName(const std::string& name) {
    static const char* const TOOLS[] = {"Proton", "Steam Linux Runtime", "Steamworks Common", "SteamVR"};
    return std::any_of(std::begin(TOOLS), std::end(TOOLS),
                       [&](const char* prefix) { return name.starts_with(prefix); });
}

bool SafeChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || std::strchr("_-.,=:/+@", c) != nullptr;
}

bool EnvAllowed(std::string_view name) {
    static const char* const PREFIXES[] = {"PROTON_", "DXVK_", "VKD3D_", "WINE_", "RADV_", "MESA_",
                                           "AMD_", "__GL_", "MANGOHUD", "GALLIUM_", "SDL_", "LFX",
                                           "ENABLE_VKBASALT", "WINEDLLOVERRIDES", "WINEDEBUG",
                                           "WINEESYNC", "WINEFSYNC", "DRI_PRIME", "mesa_glthread",
                                           "STAGING_", "SteamDeck", "LSFG_", "LSFGVK_", "DISABLE_LSFGVK",
                                           "ENABLE_GAMESCOPE_WSI",
                                           "PULSE_LATENCY_MSEC", "OBS_VKCAPTURE"};
    return std::any_of(std::begin(PREFIXES), std::end(PREFIXES),
                       [&](const char* prefix) { return name.starts_with(prefix); });
}

bool IsWrapper(std::string_view word) {
    static const char* const WRAPPERS[] = {"gamemoderun", "mangohud", "gamescope", "obs-gamecapture",
                                           "prime-run", "env"};
    return std::any_of(std::begin(WRAPPERS), std::end(WRAPPERS), [&](const char* w) { return word == w; });
}

} // namespace

std::optional<Vdf> ParseVdf(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);
    }
    Vdf root;
    TextReader reader(text);
    if (!reader.Block(root.children, true, 0)) {
        return std::nullopt;
    }
    return root;
}

std::vector<Shortcut> ParseShortcuts(std::string_view bytes) {
    std::vector<Shortcut> out;
    if (bytes.size() < 2 || bytes[0] != '\0') {
        return out;
    }
    const std::size_t end = bytes.find('\0', 1);
    if (end == std::string_view::npos || !EqualsNoCase(bytes.substr(1, end - 1), "shortcuts")) {
        return out;
    }
    BinaryReader list(bytes.substr(end + 1));
    std::vector<std::pair<std::string, std::string>> top;
    std::vector<std::vector<std::pair<std::string, std::string>>> entries;
    list.Map(top, &entries, 0);
    for (const auto& fields : entries) {
        Shortcut shortcut;
        for (const auto& [key, value] : fields) {
            if (key == "appid") {
                shortcut.appid = value;
            } else if (key == "appname") {
                shortcut.name = value;
            } else if (key == "exe") {
                shortcut.exe = value;
            } else if (key == "launchoptions") {
                shortcut.launch_options = value;
            }
        }
        if (!shortcut.appid.empty() && !shortcut.name.empty()) {
            out.push_back(std::move(shortcut));
        }
    }
    return out;
}

std::optional<fs::path> FindRoot() {
    std::vector<fs::path> candidates;
    if (const std::string env = GetEnv("EMUDEN_STEAM_DIR"); !env.empty()) {
        candidates.push_back(PathFromUtf8(env));
    }
    for (const char* var : {"ProgramFiles(x86)", "ProgramFiles"}) {
        if (const std::string dir = GetEnv(var); !dir.empty()) {
            candidates.push_back(PathFromUtf8(dir) / "Steam");
        }
    }
    if (const std::string home = GetEnv("HOME"); !home.empty()) {
        candidates.push_back(PathFromUtf8(home) / ".local" / "share" / "Steam");
        candidates.push_back(PathFromUtf8(home) / ".steam" / "steam");
        candidates.push_back(PathFromUtf8(home) / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");
    }
    for (const fs::path& dir : candidates) {
        if (IsRegularFile(dir / "steamapps" / "libraryfolders.vdf")) {
            std::error_code ec;
            const fs::path real = fs::canonical(dir, ec);
            return ec ? dir : real;
        }
    }
    return std::nullopt;
}

std::string ProtonToolName(std::string_view display) {
    std::string text = ToLower(Trim(display));
    if (!text.starts_with("proton")) {
        return {};
    }
    text = Trim(text.substr(6));
    if (text.empty() || text == "experimental" || text == "next") {
        return text.empty() ? std::string() : "proton_" + text;
    }
    if (text.starts_with("hotfix")) {
        return "proton_hotfix";
    }
    // "9.0", "9.0-4", "6.3-8": the major, and the minor unless it is 0.
    std::string major, minor;
    std::size_t i = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        major.push_back(text[i++]);
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            minor.push_back(text[i++]);
        }
    }
    if (major.empty()) {
        return {};
    }
    return "proton_" + major + (minor.empty() || minor == "0" ? std::string() : minor);
}

Status CheckLaunchOptions(std::string_view text) {
    if (text.size() > 1000) {
        return Status::Fail("the launch options are too long");
    }
    std::vector<std::string> words;
    std::string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(std::move(word));
                word.clear();
            }
        } else if (SafeChar(c) || c == '%') {
            word.push_back(c);
        } else {
            return Status::Fail(std::string("launch options may not contain \"") + c + "\"");
        }
    }
    if (!word.empty()) {
        words.push_back(std::move(word));
    }
    const auto command = std::find(words.begin(), words.end(), "%command%");
    for (const std::string& w : words) {
        if (w.find('%') != std::string::npos && w != "%command%") {
            return Status::Fail("launch options may name %command% only");
        }
    }
    if (std::count(words.begin(), words.end(), "%command%") > 1) {
        return Status::Fail("%command% may appear once");
    }
    // Without %command% every word is an argument of the game. Before it, only environment
    // assignments and known wrappers with their flags run.
    bool wrapped = false;
    for (auto it = words.begin(); it != command && command != words.end(); ++it) {
        const std::size_t equals = it->find('=');
        if (!wrapped && equals != std::string::npos && equals > 0) {
            const std::string_view name(it->data(), equals);
            if (!std::all_of(name.begin(), name.end(), [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                }) || !EnvAllowed(name)) {
                return Status::Fail("the variable " + std::string(name) + " may not be set");
            }
        } else if (IsWrapper(*it)) {
            wrapped = true;
        } else if (wrapped && it->find('/') == std::string::npos && *it != "env") {
            // A wrapper's flag or value.
        } else {
            return Status::Fail("\"" + *it + "\" before %command% is not allowed");
        }
    }
    return {};
}

Install::Install(fs::path root) : root_(std::move(root)) {}

std::vector<App> Install::Apps() const {
    std::vector<App> out;
    for (const fs::path& folder : LibraryFolders(root_)) {
        for (const fs::path& file : ListDirectory(folder / "steamapps")) {
            const std::string name = PathToUtf8(file.filename());
            if (!name.starts_with("appmanifest_") || !name.ends_with(".acf")) {
                continue;
            }
            const auto vdf = ReadVdf(file);
            const Vdf* state = vdf ? vdf->Find("AppState") : nullptr;
            if (state == nullptr || state->Value("appid").empty()) {
                continue;
            }
            App app{state->Value("appid"), state->Value("name"), false, false};
            app.tool = IsToolName(app.name);
            if (std::none_of(out.begin(), out.end(), [&](const App& a) { return a.appid == app.appid; })) {
                out.push_back(std::move(app));
            }
        }
    }
    for (const fs::path& user : UserDirs(root_)) {
        const auto bytes = ReadTextFile(user / "config" / "shortcuts.vdf");
        for (Shortcut& shortcut : bytes ? ParseShortcuts(*bytes) : std::vector<Shortcut>{}) {
            if (std::none_of(out.begin(), out.end(), [&](const App& a) { return a.appid == shortcut.appid; })) {
                out.push_back({shortcut.appid, shortcut.name, true, false});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const App& a, const App& b) { return ToLower(a.name) < ToLower(b.name); });
    return out;
}

std::vector<Tool> Install::Tools() const {
    std::vector<Tool> out;
    for (const App& app : Apps()) {
        if (const std::string name = ProtonToolName(app.name); !app.shortcut && !name.empty()) {
            out.push_back({name, app.name, true});
        }
    }
    for (const fs::path& dir : ListDirectory(root_ / "compatibilitytools.d")) {
        const auto vdf = ReadVdf(dir / "compatibilitytool.vdf");
        const Vdf* tools = vdf ? Walk(*vdf, {"compatibilitytools", "compat_tools"}) : nullptr;
        for (const Vdf& tool : tools ? tools->children : std::vector<Vdf>{}) {
            const std::string display = tool.Value("display_name");
            out.push_back({tool.key, display.empty() ? tool.key : display, true});
        }
    }
    return out;
}

std::optional<GameFolder> Install::FolderOf(const std::string& appid) const {
    if (appid.empty() || appid.size() > 10 ||
        !std::all_of(appid.begin(), appid.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    for (const fs::path& folder : LibraryFolders(root_)) {
        const auto vdf = ReadVdf(folder / "steamapps" / ("appmanifest_" + appid + ".acf"));
        const Vdf* state = vdf ? vdf->Find("AppState") : nullptr;
        if (state == nullptr || state->Value("appid") != appid) {
            continue;
        }
        const std::string name = state->Value("installdir");
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos) {
            continue;
        }
        const fs::path dir = folder / "steamapps" / "common" / PathFromUtf8(name);
        std::error_code ec;
        if (fs::is_directory(fs::symlink_status(dir, ec))) {
            return GameFolder{state->Value("name"), dir};
        }
    }
    return std::nullopt;
}

AppConfig Install::ConfigFromFiles(const std::string& appid) const {
    AppConfig config;
    for (const fs::path& user : UserDirs(root_)) {
        const auto local = ReadVdf(user / "config" / "localconfig.vdf");
        const Vdf* apps = local ? Walk(*local, {"UserLocalConfigStore", "Software", "Valve", "Steam", "apps"}) : nullptr;
        if (const Vdf* app = apps ? apps->Find(appid) : nullptr) {
            config.launch_options = app->Value("LaunchOptions");
            break;
        }
        const auto bytes = ReadTextFile(user / "config" / "shortcuts.vdf");
        for (const Shortcut& shortcut : bytes ? ParseShortcuts(*bytes) : std::vector<Shortcut>{}) {
            if (shortcut.appid == appid) {
                config.launch_options = shortcut.launch_options;
            }
        }
    }
    if (const auto vdf = ReadVdf(root_ / "config" / "config.vdf")) {
        const Vdf* mapping = Walk(*vdf, {"InstallConfigStore", "Software", "Valve", "Steam", "CompatToolMapping"});
        if (const Vdf* app = mapping ? mapping->Find(appid) : nullptr) {
            config.compat_tool = app->Value("name");
            config.compat_tool_chosen = !config.compat_tool.empty();
        }
    }
    return config;
}

} // namespace den::library::steam
