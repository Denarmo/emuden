// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "retro_names.h"

#include <charconv>
#include <string_view>

namespace den::gfx {

namespace {

using Sem = RetroSem;

std::optional<int> Number(std::string_view text) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || text.empty()) {
        return std::nullopt;
    }
    return value;
}

// "PassOutput3" with prefix "PassOutput" gives 3.
std::optional<int> Suffix(std::string_view name, std::string_view prefix, std::string_view tail = {}) {
    if (name.size() <= prefix.size() + tail.size() || name.substr(0, prefix.size()) != prefix ||
        name.substr(name.size() - tail.size()) != tail) {
        return std::nullopt;
    }
    return Number(name.substr(prefix.size(), name.size() - prefix.size() - tail.size()));
}

} // namespace

std::optional<std::pair<RetroSem, int>> ResolveRetroName(std::string name, int pass, const RetroNames& names) {
    if (const std::size_t bracket = name.find('['); bracket != std::string::npos) {
        name.resize(bracket);
    }
    if (const std::size_t dot = name.rfind('.'); dot != std::string::npos) {
        name = name.substr(dot + 1);
    }
    const auto found = [](Sem sem, int index = 0) { return std::optional<std::pair<RetroSem, int>>({sem, index}); };
    for (std::size_t i = 0; i < names.params.size(); ++i) {
        if (names.params[i] == name) {
            return found(Sem::Param, static_cast<int>(i));
        }
    }
    if (name == "MVP" || name == "MVPMatrix") {
        return found(Sem::Mvp);
    }
    if (name == "OutputSize") {
        return found(Sem::Output);
    }
    if (name == "FinalViewportSize") {
        return found(Sem::FinalViewport);
    }
    if (name == "SourceSize" || name == "TextureSize" || name == "InputSize") {
        return found(Sem::Source);
    }
    if (name == "OriginalSize" || name == "OrigTextureSize" || name == "OrigInputSize") {
        return found(Sem::Original);
    }
    if (name == "FrameCount") {
        return found(Sem::FrameCount);
    }
    if (name == "FrameDirection") {
        return found(Sem::FrameDirection);
    }
    if (name == "Source" || name == "Texture") {
        return found(Sem::TexSource);
    }
    if (name == "Original" || name == "OrigTexture") {
        return found(Sem::TexOriginal);
    }
    if (auto n = Suffix(name, "OriginalHistorySize")) {
        return found(Sem::HistorySize, *n);
    }
    if (auto n = Suffix(name, "OriginalHistory")) {
        return found(Sem::TexHistory, *n);
    }
    if (auto n = Suffix(name, "PassOutputSize")) {
        return found(Sem::PassOutputSize, *n);
    }
    if (auto n = Suffix(name, "PassOutput")) {
        return found(Sem::TexPassOutput, *n);
    }
    if (auto n = Suffix(name, "PassFeedbackSize")) {
        return found(Sem::PassFeedbackSize, *n);
    }
    if (auto n = Suffix(name, "PassFeedback")) {
        return found(Sem::TexPassFeedback, *n);
    }
    // The older GLSL names: passes counted back from this one, earlier pictures from one.
    for (const char* tail : {"TextureSize", "InputSize"}) {
        if (auto n = Suffix(name, "PassPrev", tail)) {
            const int k = pass - *n;
            return k < 0 ? found(Sem::Original) : found(Sem::PassOutputSize, k);
        }
        if (auto n = Suffix(name, "Prev", tail)) {
            return found(Sem::HistorySize, *n + 1);
        }
        if (name == std::string("Prev") + tail) {
            return found(Sem::HistorySize, 1);
        }
        if (auto n = Suffix(name, "Pass", tail)) {
            return found(Sem::PassOutputSize, *n - 1);
        }
    }
    if (auto n = Suffix(name, "PassPrev", "Texture")) {
        const int k = pass - *n;
        return k < 0 ? found(Sem::TexOriginal) : found(Sem::TexPassOutput, k);
    }
    if (auto n = Suffix(name, "Prev", "Texture")) {
        return found(Sem::TexHistory, *n + 1);
    }
    if (name == "PrevTexture") {
        return found(Sem::TexHistory, 1);
    }
    if (auto n = Suffix(name, "Pass", "Texture")) {
        return found(Sem::TexPassOutput, *n - 1);
    }
    for (std::size_t k = 0; k < names.aliases.size(); ++k) {
        const std::string& alias = names.aliases[k];
        if (alias.empty()) {
            continue;
        }
        const int index = static_cast<int>(k);
        if (name == alias || name == alias + "Texture") {
            return found(Sem::TexPassOutput, index);
        }
        if (name == alias + "Size" || name == alias + "TextureSize" || name == alias + "InputSize") {
            return found(Sem::PassOutputSize, index);
        }
        if (name == alias + "Feedback") {
            return found(Sem::TexPassFeedback, index);
        }
        if (name == alias + "FeedbackSize") {
            return found(Sem::PassFeedbackSize, index);
        }
    }
    for (std::size_t i = 0; i < names.luts.size(); ++i) {
        if (name == names.luts[i]) {
            return found(Sem::TexLut, static_cast<int>(i));
        }
        if (name == names.luts[i] + "Size") {
            return found(Sem::LutSize, static_cast<int>(i));
        }
    }
    return std::nullopt;
}

} // namespace den::gfx
