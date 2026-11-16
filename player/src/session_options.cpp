// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The core's options: the values kept for the console and for one game, as the settings
// hold them, and what the pause menu lists.

#include <algorithm>
#include <cctype>

#include "den/store/settings.h"
#include "session_impl.h"

namespace den::player {

std::string PlayerSession::OptionSettingKey(const std::string& key) const {
    std::string name;
    for (const char c : snapshot_.core_name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    std::string safe = key;
    std::replace(safe.begin(), safe.end(), '.', '_');
    return "core_options." + name + "." + safe;
}

std::string PlayerSession::GameOptionKey(const std::string& key) const {
    std::string safe = key;
    std::replace(safe.begin(), safe.end(), '.', '_');
    return "game_options." + system_ + "." + game_key_ + "." + safe;
}

void PlayerSession::ApplyStoredOptions() {
    if (!config_.settings) {
        return;
    }
    for (const OptionInfo& option : core_->Options()) {
        // This game's own value wins over the console's.
        const std::string game = GameOptionKey(option.key);
        const std::string console = OptionSettingKey(option.key);
        if (config_.settings->Has(game)) {
            core_->SetOption(option.key, config_.settings->GetString(game));
        } else if (config_.settings->Has(console)) {
            core_->SetOption(option.key, config_.settings->GetString(console));
        }
    }
}

std::vector<CoreOption> PlayerSession::Options() const {
    std::vector<CoreOption> out;
    if (!core_) {
        return out;
    }
    for (const OptionInfo& option : core_->Options()) {
        if (!option.visible) {
            continue;
        }
        CoreOption item;
        item.key = option.key;
        item.label = option.label;
        item.value = option.value;
        item.default_value = option.default_value;
        item.for_game = config_.settings && config_.settings->Has(GameOptionKey(option.key));
        for (const OptionChoice& value : option.values) {
            item.values.push_back(value.value);
        }
        out.push_back(std::move(item));
    }
    AddRendererOption(out);
    return out;
}

void PlayerSession::SetOption(const std::string& key, const std::string& value) {
    SetOption(key, value, OptionScope::Console);
}

void PlayerSession::SetOption(const std::string& key, const std::string& value, OptionScope scope) {
    if (!core_) {
        return;
    }
    if (key == RENDERER_OPTION) {
        SetRenderer(value, scope);
        return;
    }
    core_->SetOption(key, value);
    if (!config_.settings) {
        return;
    }
    if (scope == OptionScope::Game) {
        config_.settings->SetString(GameOptionKey(key), value);
    } else {
        config_.settings->SetString(OptionSettingKey(key), value);
        config_.settings->Remove(GameOptionKey(key));
    }
}

void PlayerSession::ClearGameOptions() {
    if (!core_ || !config_.settings) {
        return;
    }
    ClearGameRenderer();
    for (const OptionInfo& option : core_->Options()) {
        const std::string game = GameOptionKey(option.key);
        if (!config_.settings->Has(game)) {
            continue;
        }
        config_.settings->Remove(game);
        const std::string console = OptionSettingKey(option.key);
        core_->SetOption(option.key, config_.settings->Has(console)
                                         ? config_.settings->GetString(console)
                                         : option.default_value);
    }
}

} // namespace den::player
