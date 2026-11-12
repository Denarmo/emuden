// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "den/base/hash.h"
#include "den/base/jobs.h"
#include "den/base/json.h"
#include "den/base/utf8.h"

using namespace den;

TEST_CASE("hashes match their published test vectors") {
    CHECK(Md5Hex("abc", 3) == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(Md5Hex("", 0) == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(Sha256Hex("abc", 3) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Crc32Update(0, "123456789", 9) == 0xCBF43926u);
    CHECK(Crc32Update(Crc32Update(0, "1234", 4), "56789", 5) == 0xCBF43926u);
}

TEST_CASE("json round trip") {
    const auto value = ParseJson(R"({"name":"Café","n":[1,2,3],"ok":true})");
    REQUIRE(value);
    CHECK(value->String("name") == "Caf\xC3\xA9");
    REQUIRE(value->Find("n"));
    CHECK(value->Find("n")->items.size() == 3);
    const auto again = ParseJson(WriteJson(*value));
    REQUIRE(again);
    CHECK(again->Find("ok")->boolean);
    CHECK_FALSE(ParseJson("{\"a\":"));
}

TEST_CASE("utf8 decoding") {
    std::size_t at = 0;
    const std::string text = "a\xC3\xA9\xE2\x82\xAC";
    CHECK(NextCodePoint(text, at) == 0x61);
    CHECK(NextCodePoint(text, at) == 0xE9);
    CHECK(NextCodePoint(text, at) == 0x20AC);
    CHECK(NextCodePoint(text, at) == 0);
    CHECK(CodePointCount(text) == 3);
    CHECK(CodePointCount("\xE2\x82") == 2);
}

TEST_CASE("jobs report progress, completion and cancel") {
    Jobs jobs(2);
    bool done_on_main = false;
    auto job = jobs.Run(
        "work",
        [](Job& j) {
            j.SetProgress(0.5f);
            j.SetMessage("half");
            return Status{};
        },
        [&](Job& j) { done_on_main = j.State() == JobState::Done; });
    jobs.Wait(job);
    CHECK(done_on_main);
    CHECK(job->Message() == "half");

    std::atomic<bool> started{false};
    auto endless = jobs.Run("endless", [&](Job& j) {
        started = true;
        while (!j.CancelRequested()) {
            std::this_thread::yield();
        }
        return Status::Fail("stopped");
    });
    while (!started) {
        std::this_thread::yield();
    }
    endless->Cancel();
    jobs.Wait(endless);
    CHECK(endless->State() == JobState::Cancelled);
}
