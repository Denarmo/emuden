// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The renderer choice (Auto, OpenGL, Vulkan) of a core that draws with the GPU. It is stored
// like a core option, for the console or for one game, and read when a game starts.

#include "den/store/settings.h"
#include "session_impl.h"

namespace den::player {

std::string PlayerSession::RendererValue() const {
    if (!config_.settings) {
        return "Auto";
    }
    const std::string game = GameOptionKey(RENDERER_OPTION);
    if (config_.settings->Has(game)) {
        return config_.settings->GetString(game);
    }
    const std::string console = OptionSettingKey(RENDERER_OPTION);
    return config_.settings->Has(console) ? config_.settings->GetString(console) : "Auto";
}

void PlayerSession::AddRendererOption(std::vector<CoreOption>& options) const {
    if (!hw_ || !core_ || (!core_->HardwareRendered() && !hw_->VulkanRefused())) {
        return;
    }
    CoreOption item;
    item.key = RENDERER_OPTION;
    const std::string in_use = hw_->InUse();
    item.label = "Renderer (on the next start";
    if (!in_use.empty()) {
        item.label += "; now " + in_use;
    } else if (hw_->VulkanRefused()) {
        item.label += "; Vulkan is not available on this device";
    }
    item.label += ")";
    item.value = RendererValue();
    item.default_value = "Auto";
    item.values = {"Auto", "OpenGL", "Vulkan"};
    item.for_game = config_.settings && config_.settings->Has(GameOptionKey(RENDERER_OPTION));
    options.push_back(std::move(item));
}

void PlayerSession::SetRenderer(const std::string& value, OptionScope scope) {
    if (!config_.settings) {
        return;
    }
    if (scope == OptionScope::Game) {
        config_.settings->SetString(GameOptionKey(RENDERER_OPTION), value);
    } else {
        config_.settings->SetString(OptionSettingKey(RENDERER_OPTION), value);
        config_.settings->Remove(GameOptionKey(RENDERER_OPTION));
    }
}

void PlayerSession::ClearGameRenderer() {
    if (config_.settings) {
        config_.settings->Remove(GameOptionKey(RENDERER_OPTION));
    }
}

} // namespace den::player
