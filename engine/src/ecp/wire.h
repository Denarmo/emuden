// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Emulator Control Protocol framing (section 4): a 4-byte little-endian length, then that
// many bytes of UTF-8 JSON, one object per frame. Plus the small codecs the messages need.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "den/base/json.h"
#include "den/engine/control.h"
#include "den/platform/stream.h"

namespace den::engine::ecp {

inline constexpr std::size_t MAX_FRAME = 16u << 20;
inline constexpr int PROTOCOL = 1;

/// The next frame as a JSON object; nullopt when the stream ended, the frame is too large or
/// it is not an object.
std::optional<JsonValue> ReadFrame(pal::Stream& stream);
bool WriteFrame(pal::Stream& stream, const JsonValue& message);

/// One line of compact JSON (the frames' text; WriteJson indents).
std::string CompactJson(const JsonValue& value);

std::string Base64Encode(const void* data, std::size_t size);
std::optional<std::vector<std::uint8_t>> Base64Decode(std::string_view text);

/// 32 random bytes as hex (the session token).
std::string NewToken();
/// Compares in time that depends only on the lengths.
bool SameToken(std::string_view a, std::string_view b);

/// A picture object ({"png", "width", "height"}); empty when the value is none.
Picture ReadPicture(const JsonValue* value);
/// A setting object of a schema; nullopt without a key.
std::optional<SettingDef> ReadSetting(const JsonValue& value);
/// A value as the text EmuDEN keeps it in ("true", "3", "16:9").
std::string ValueText(const JsonValue& value);
/// The value for a setting from that text, typed by the setting (a toggle is a bool, a range
/// a number); a string when the setting is not known.
JsonValue TypedValue(const SettingDef* setting, const std::string& text);

/// A member's number, or the fallback.
double NumberOf(const JsonValue& object, std::string_view key, double fallback);
bool BoolOf(const JsonValue& object, std::string_view key, bool fallback);
std::vector<std::string> StringsOf(const JsonValue& object, std::string_view key);

/// The message `{"type": type, ...fields}`, with "id" when id is above 0.
JsonValue Message(std::string type, std::int64_t id, std::vector<std::pair<std::string, JsonValue>> fields = {});

/// A typed error of a result ({"code", "message", "detail"}) as the user's text.
std::string ErrorText(const JsonValue& result);

} // namespace den::engine::ecp
