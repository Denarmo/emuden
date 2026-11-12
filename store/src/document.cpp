// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/base/files.h"
#include "den/store/document.h"

namespace den::store {

std::optional<Document> LoadDocument(const std::filesystem::path& file) {
    const auto text = ReadTextFile(file);
    if (!text) {
        return std::nullopt;
    }
    auto root = ParseJson(*text);
    if (!root || root->kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    const JsonValue* version = root->Find("version");
    const JsonValue* data = root->Find("data");
    if (version == nullptr || version->kind != JsonValue::Kind::Number || data == nullptr ||
        data->kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    Document document;
    document.version = static_cast<int>(version->number);
    document.data = *data;
    return document;
}

bool SaveDocument(const std::filesystem::path& file, int version, JsonValue data) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const JsonValue root =
        JsonObject({{"version", JsonNumber(version)}, {"data", std::move(data)}});
    return WriteFileAtomic(file, WriteJson(root));
}

} // namespace den::store
