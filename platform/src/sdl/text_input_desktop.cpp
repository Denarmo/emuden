// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The desktop has no keyboard of its own to show: a real one types through den::input.

#include "den/platform/text_input.h"

namespace den::pal {

namespace {

class NoTextInput final : public TextInput {
public:
    bool Available() const override {
        return false;
    }
    bool Begin(const TextInputRequest&, TextDisplay) override {
        return false;
    }
    void SetText(const std::string&) override {}
    void End() override {}
    bool Active() const override {
        return false;
    }
    void Poll(std::vector<TextInputEvent>&) override {}
    float CoveredShare() const override {
        return 0;
    }
};

} // namespace

TextInput& GetTextInput() {
    static NoTextInput none;
    return none;
}

} // namespace den::pal
