// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/store/document.h"

using namespace den;

TEST_CASE("a document keeps its version and data") {
    const auto dir = std::filesystem::temp_directory_path() / "emuden-document-test";
    std::filesystem::remove_all(dir);
    const auto file = dir / "nested" / "records.json";
    CHECK_FALSE(store::LoadDocument(file));

    REQUIRE(store::SaveDocument(file, 3, JsonObject({{"name", JsonString("Tetris")}})));
    const auto loaded = store::LoadDocument(file);
    REQUIRE(loaded);
    CHECK(loaded->version == 3);
    CHECK(loaded->data.String("name") == "Tetris");

    REQUIRE(WriteFileAtomic(file, "{\"data\": 1}"));
    CHECK_FALSE(store::LoadDocument(file));
    REQUIRE(WriteFileAtomic(file, "not json"));
    CHECK_FALSE(store::LoadDocument(file));
    std::filesystem::remove_all(dir);
}
