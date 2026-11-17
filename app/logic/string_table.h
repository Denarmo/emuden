// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The launcher's texts: the English ones built in; over them the plugins' own texts in the app's
// language (their "translations", and what a language pack adds for them); over those the
// language pack's. A text is found by its key, which is either a dotted name ("common.ok") or
// the English text itself ("Check for updates"); what no layer has stays English.

#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace den::app {

using TextMap = std::map<std::string, std::string, std::less<>>;

/// The string members of a JSON object; nothing for text that is no JSON object.
std::optional<TextMap> ParseTexts(std::string_view json);

/// How many "{}" fields a text has ("{{" and "}}" are braces, not fields); -1 for a text
/// std::format would refuse (a lone brace, "{0}", "{:x}").
int CountFields(std::string_view text);

class StringTable {
public:
    void SetBase(TextMap texts);
    /// A pack's texts. Those whose "{}" fields do not match the English text's (the key's, or
    /// the base's for a dotted key) are left out, so a bad pack cannot break a format call.
    void SetPack(TextMap texts);
    void ClearPack();
    /// The plugins' texts, checked like a pack's.
    void SetPluginTexts(TextMap texts);

    /// The pack's text, else the plugins'; null when neither has one.
    const std::string* Pack(std::string_view key) const;
    /// The pack's text, else the plugins', else the base's; null when none has it.
    const std::string* Find(std::string_view key) const;

    bool HasPack() const { return !pack_.empty(); }
    /// Texts the last SetPack or SetPluginTexts left out for their fields.
    std::size_t Rejected() const { return rejected_; }
    std::size_t PackSize() const { return pack_.size(); }

private:
    TextMap Checked(TextMap texts);

    TextMap base_, pack_, plugins_;
    std::size_t rejected_ = 0;
};

} // namespace den::app
