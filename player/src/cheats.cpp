// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/player/cheats.h"

#include <algorithm>
#include <charconv>
#include <map>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "game_files.h"

namespace den::player {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t MAX_CODE = 4096;
constexpr int MAX_CHT_CHEATS = 10000;

// A value of a RetroArch config line: quoted or not.
std::string Unquote(std::string_view value) {
    std::string text = Trim(value);
    if (text.size() >= 2 && text.front() == '"') {
        const std::size_t end = text.find('"', 1);
        return text.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    }
    return text;
}

bool IsTrue(std::string_view value) {
    return EqualsNoCase(value, "true") || value == "1";
}

} // namespace

fs::path CheatFile(const fs::path& data_dir, const std::string& system, const std::string& game_id) {
    return MakeGamePaths(data_dir, system, game_id, {}).Cheats();
}

Result<std::vector<Cheat>> ReadCheats(const fs::path& file) {
    std::vector<Cheat> cheats;
    if (!IsRegularFile(file)) {
        return cheats;
    }
    const auto text = ReadTextFile(file);
    const auto root = text ? ParseJson(*text) : std::nullopt;
    const JsonValue* list = root ? root->Find("cheats") : nullptr;
    if (!list || list->kind != JsonValue::Kind::Array) {
        return Status::Fail("The cheats of this game could not be read.");
    }
    for (const JsonValue& item : list->items) {
        Cheat cheat;
        cheat.name = item.String("name");
        cheat.code = item.String("code");
        const JsonValue* enabled = item.Find("enabled");
        cheat.enabled = enabled && enabled->kind == JsonValue::Kind::Bool && enabled->boolean;
        if (CheatProblem(cheat).empty()) {
            cheats.push_back(std::move(cheat));
        }
    }
    return cheats;
}

Status WriteCheats(const fs::path& file, const std::vector<Cheat>& cheats) {
    std::error_code ec;
    if (cheats.empty()) {
        fs::remove(file, ec);
        return {};
    }
    std::vector<JsonValue> items;
    for (const Cheat& cheat : cheats) {
        items.push_back(JsonObject({{"name", JsonString(cheat.name)},
                                    {"code", JsonString(cheat.code)},
                                    {"enabled", JsonBool(cheat.enabled)}}));
    }
    fs::create_directories(file.parent_path(), ec);
    const JsonValue root = JsonObject({{"version", JsonNumber(1)}, {"cheats", JsonArray(std::move(items))}});
    if (!WriteFileAtomic(file, WriteJson(root))) {
        return Status::Fail("The cheats could not be saved.");
    }
    return {};
}

std::vector<Cheat> LoadGameCheats(const fs::path& file, const fs::path& content) {
    if (IsRegularFile(file)) {
        auto kept = ReadCheats(file);
        if (!kept) {
            log::Warn("player", "{}: {}", PathToUtf8(file), kept.error());
            return {};
        }
        return kept.take();
    }
    const fs::path beside = ChtBeside(content);
    if (beside.empty()) {
        return {};
    }
    auto imported = ReadChtFile(beside);
    if (!imported) {
        log::Warn("player", "{}: {}", PathToUtf8(beside), imported.error());
        return {};
    }
    log::Info("player", "{} cheats from {}", imported->size(), PathToUtf8(beside.filename()));
    if (const Status written = WriteCheats(file, *imported); !written) {
        log::Warn("player", "{}", written.error);
    }
    return imported.take();
}

Result<std::vector<Cheat>> ParseCht(std::string_view text) {
    std::map<int, Cheat> found;
    bool counted = false;
    int count = -1;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string line = Trim(text.substr(start, end - start));
        start = end + 1;
        const std::size_t eq = line.find('=');
        if (line.empty() || line.front() == '#' || eq == std::string::npos) {
            continue;
        }
        const std::string key = ToLower(Trim(std::string_view(line).substr(0, eq)));
        const std::string value = Unquote(std::string_view(line).substr(eq + 1));
        if (key == "cheats") {
            counted = true;
            std::from_chars(value.data(), value.data() + value.size(), count);
            continue;
        }
        if (!key.starts_with("cheat")) {
            continue;
        }
        int index = -1;
        const char* key_end = key.data() + key.size();
        const auto [rest, error] = std::from_chars(key.data() + 5, key_end, index);
        if (error != std::errc() || index < 0 || index >= MAX_CHT_CHEATS || rest == key_end || *rest != '_') {
            continue;
        }
        const std::string_view field(rest + 1);
        Cheat& cheat = found[index];
        if (field == "desc") {
            cheat.name = value;
        } else if (field == "code") {
            cheat.code = CleanCode(value);
        } else if (field == "enable") {
            cheat.enabled = IsTrue(value);
        }
    }
    if (!counted && found.empty()) {
        return Status::Fail("This is not a RetroArch cheat file (.cht).");
    }
    std::vector<Cheat> cheats;
    for (auto& [index, cheat] : found) {
        if ((count < 0 || index < count) && CheatProblem(cheat).empty()) {
            if (cheat.name.empty()) {
                cheat.name = cheat.code;
            }
            cheats.push_back(std::move(cheat));
        }
    }
    if (cheats.empty()) {
        return Status::Fail("The file has no cheat codes.");
    }
    return cheats;
}

Result<std::vector<Cheat>> ReadChtFile(const fs::path& file) {
    const auto text = ReadTextFile(file);
    if (!text) {
        return Status::Fail("The file could not be read.");
    }
    return ParseCht(*text);
}

fs::path ChtBeside(const fs::path& content) {
    if (content.empty()) {
        return {};
    }
    for (const char* extension : {".cht", ".CHT"}) {
        fs::path file = content;
        file.replace_extension(extension);
        if (IsRegularFile(file)) {
            return file;
        }
    }
    return {};
}

std::string CleanCode(std::string_view code) {
    std::string out;
    bool joined = true; // at the start, or just after a '+'
    for (char c : code) {
        if (c == '\r' || c == '\n') {
            c = '+';
        }
        if (c == '+') {
            while (!out.empty() && out.back() == ' ') {
                out.pop_back();
            }
            if (!joined) {
                out.push_back('+');
            }
            joined = true;
        } else if (c == ' ' || c == '\t') {
            if (!joined && out.back() != ' ') {
                out.push_back(' ');
            }
        } else {
            out.push_back(c);
            joined = false;
        }
    }
    while (!out.empty() && (out.back() == '+' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::string CheatProblem(const Cheat& cheat) {
    const std::string code = CleanCode(cheat.code);
    if (code.empty()) {
        return "A cheat needs a code.";
    }
    if (code.size() > MAX_CODE) {
        return "That code is too long.";
    }
    return {};
}

Status TidyCheats(std::vector<Cheat>& cheats) {
    for (Cheat& cheat : cheats) {
        if (const std::string problem = CheatProblem(cheat); !problem.empty()) {
            return Status::Fail(problem);
        }
        cheat.code = CleanCode(cheat.code);
        cheat.name = Trim(cheat.name);
        if (cheat.name.empty()) {
            cheat.name = cheat.code;
        }
    }
    return {};
}

int MergeCheats(std::vector<Cheat>& cheats, const std::vector<Cheat>& imported) {
    int added = 0;
    for (const Cheat& cheat : imported) {
        const std::string code = CleanCode(cheat.code);
        const bool known = std::any_of(cheats.begin(), cheats.end(),
                                       [&](const Cheat& have) { return CleanCode(have.code) == code; });
        if (!known && CheatProblem(cheat).empty()) {
            cheats.push_back(cheat);
            ++added;
        }
    }
    return added;
}

} // namespace den::player
