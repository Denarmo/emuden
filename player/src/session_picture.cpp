// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The picture preset of a session: which one, its slider values, and its compiled chain.

#include <algorithm>
#include <cmath>

#include "den/base/log.h"
#include "den/store/settings.h"
#include "retro_preset.h"
#include "session_impl.h"

namespace den::player {

namespace {

std::string ParamKey(const std::string& preset, const std::string& param) {
    return "player.shader." + preset + "." + param;
}

} // namespace

void PlayerSession::SetPicture(const std::string& id) {
    const std::string wanted = id == PICTURE_AUTO ? AutoPicture(system_) : id;
    const PresetData* preset = FindPreset(wanted);
    if (!preset || preset->info.id == "plain") {
        preset = nullptr;
    }
    const std::string resolved = preset ? preset->info.id : "plain";
    if (resolved == picture_ && preset == preset_) {
        return;
    }
    picture_ = resolved;
    preset_ = preset;
    snapshot_.picture = picture_;
    chain_stale_ = true;
    picture_values_.clear();
    if (!preset_) {
        return;
    }
    for (std::size_t i = 0; i < preset_->uniforms.size(); ++i) {
        float value = preset_->uniforms[i].second;
        if (i < preset_->info.params.size() && config_.settings) {
            const PictureParam& param = preset_->info.params[i];
            value = std::clamp(static_cast<float>(config_.settings->GetNumber(
                                   ParamKey(picture_, param.id), param.fallback)),
                               param.min, param.max);
        }
        picture_values_.push_back(value);
    }
}

std::vector<PictureParam> PlayerSession::PictureParams() const {
    if (!preset_) {
        return {};
    }
    std::vector<PictureParam> params = preset_->info.params;
    for (std::size_t i = 0; i < params.size() && i < picture_values_.size(); ++i) {
        params[i].value = picture_values_[i];
    }
    return params;
}

void PlayerSession::SetPictureParam(const std::string& id, float value) {
    if (!preset_) {
        return;
    }
    for (std::size_t i = 0; i < preset_->info.params.size() && i < picture_values_.size(); ++i) {
        const PictureParam& param = preset_->info.params[i];
        if (param.id != id) {
            continue;
        }
        value = std::clamp(value, param.min, param.max);
        picture_values_[i] = value;
        chain_.SetParam(i, value);
        retro_.SetParam(i, value);
        if (config_.settings) {
            // Only what differs from the preset is kept, so a changed preset shows through.
            if (std::fabs(value - param.fallback) < param.step * 0.25f) {
                config_.settings->Remove(ParamKey(picture_, id));
            } else {
                config_.settings->SetNumber(ParamKey(picture_, id), value);
            }
        }
        return;
    }
}

void PlayerSession::ResetPictureParams() {
    if (!preset_) {
        return;
    }
    for (const PictureParam& param : preset_->info.params) {
        SetPictureParam(param.id, param.fallback);
    }
}

bool PlayerSession::ReadyChain(gfx::Device& device) {
    const bool retro = preset_ && !preset_->retro.empty();
    if (!preset_ || (preset_->passes.empty() && !retro)) {
        return false;
    }
    if (chain_stale_) {
        chain_stale_ = false;
        chain_.Release();
        retro_.Release();
        Status status;
        if (retro) {
            const auto spec = LoadRetroPreset(preset_->retro);
            status = spec ? retro_.Load(device, spec.value()) : spec.status();
        } else {
            const auto spec = BuildEffect(*preset_, ShaderDir());
            status = !spec ? spec.status() : chain_.Load(device, spec.value()) ? Status{} : Status::Fail("");
        }
        if (!status) {
            log::Warn("player", "the {} picture is not available on this device{}{}", picture_,
                      status.error.empty() ? "" : ": ", status.error);
            // Plain from here on; the screen sees it in the snapshot.
            preset_ = nullptr;
            picture_ = "plain";
            snapshot_.picture = picture_;
            return false;
        }
        for (std::size_t i = 0; i < picture_values_.size(); ++i) {
            chain_.SetParam(i, picture_values_[i]);
            retro_.SetParam(i, picture_values_[i]);
        }
    }
    return retro ? retro_.Loaded() : chain_.Loaded();
}

void PlayerSession::ReleasePicture() {
    chain_.Release();
    retro_.Release();
    chain_stale_ = true;
    preset_ = nullptr;
    picture_values_.clear();
    picture_ = "plain";
}

} // namespace den::player
