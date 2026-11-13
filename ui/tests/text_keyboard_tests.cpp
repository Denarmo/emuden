// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The text entry with a platform keyboard (pal::TextInput), played by a fake.

#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "den/ui/page.h"
#include "den/ui/text_entry.h"

using namespace den;
using namespace den::ui;
using Kind = pal::TextInputEvent::Kind;

namespace {

class FakeKeyboard final : public pal::TextInput {
public:
    bool Available() const override {
        return available;
    }
    bool Begin(const pal::TextInputRequest& request, pal::TextDisplay display) override {
        if (!available || refuse) {
            return false;
        }
        ++begun;
        active = true;
        text = request.text;
        max_length = request.max_length;
        where = display;
        return true;
    }
    void SetText(const std::string& value) override {
        text = value;
        ++set;
    }
    void End() override {
        if (active) {
            ++ended;
        }
        active = false;
        events.clear();
    }
    bool Active() const override {
        return active;
    }
    void Poll(std::vector<pal::TextInputEvent>& out) override {
        out.insert(out.end(), events.begin(), events.end());
        events.clear();
    }
    float CoveredShare() const override {
        return covered;
    }

    // What the user does on it.
    void Show() {
        events.push_back({Kind::Shown, {}});
    }
    void Type(const std::string& value) {
        text = value;
        events.push_back({Kind::Text, value});
    }
    void Send(Kind kind) {
        events.push_back({kind, {}});
    }

    bool available = true, refuse = false, active = false;
    int begun = 0, ended = 0, set = 0, max_length = 0;
    float covered = 0;
    std::string text;
    std::optional<pal::TextDisplay> where;
    std::vector<pal::TextInputEvent> events;
};

KeyboardSetup OneDisplay(FakeKeyboard& keyboard) {
    KeyboardSetup setup;
    setup.system = &keyboard;
    setup.prefer_system = true;
    return setup;
}

KeyboardSetup TwoDisplays(FakeKeyboard& keyboard) {
    KeyboardSetup setup = OneDisplay(keyboard);
    setup.display = pal::TextDisplay::Second;
    setup.apart = true;
    setup.keys_apart = true;
    return setup;
}

input::Event PadNav(input::Nav nav) {
    return input::Event::MakeNav(nav, input::PadFamily::Xbox);
}

} // namespace

TEST_CASE("the system keyboard types into the line and its done key finishes") {
    FakeKeyboard keyboard;
    std::string result, followed;
    TextRequest request;
    request.text = "Sav";
    request.max_length = 8;
    request.on_change = [&](const std::string& text) { followed = text; };
    {
        TextEntry entry(request, [&](const std::string& text) { result = text; }, OneDisplay(keyboard), false);
        CHECK(keyboard.begun == 1);
        CHECK(keyboard.where == pal::TextDisplay::Main);
        CHECK(keyboard.text == "Sav");
        CHECK(keyboard.max_length == 8);
        CHECK(std::string(entry.Mode()) == "system");

        keyboard.Show();
        keyboard.Type("Save 1");
        CHECK(entry.Update(0.016f) == TextEntry::Outcome::Open);
        CHECK(entry.Text() == "Save 1");
        CHECK(followed == "Save 1");
        CHECK(keyboard.set == 0); // what it typed itself is not sent back

        keyboard.Send(Kind::Done);
        CHECK(entry.Update(0.016f) == TextEntry::Outcome::Done);
        entry.Finish();
        CHECK(result == "Save 1");
        CHECK(keyboard.ended == 0);
    }
    CHECK(keyboard.ended == 1); // the session ends with the entry
}

TEST_CASE("the system keyboard's text keeps the entry's rules") {
    FakeKeyboard keyboard;
    TextRequest request;
    request.max_length = 4;
    TextEntry entry(request, {}, OneDisplay(keyboard), false);
    keyboard.Show();

    // Done on an empty line is refused; Back on the keyboard cancels.
    keyboard.Send(Kind::Done);
    CHECK(entry.Update(0.016f) == TextEntry::Outcome::Open);

    // Too long, with a leading space and a line break: cut, and the keyboard's field is told.
    keyboard.Type(" ab\ncdef");
    CHECK(entry.Update(0.016f) == TextEntry::Outcome::Open);
    CHECK(entry.Text() == "abcd");
    CHECK(keyboard.text == "abcd");
    CHECK(keyboard.set == 1);

    keyboard.Send(Kind::Cancel);
    CHECK(entry.Update(0.016f) == TextEntry::Outcome::Cancelled);
}

TEST_CASE("a request made with the pad keeps the app's keys beside the keyboard on the second display") {
    FakeKeyboard keyboard;
    TextEntry entry(TextRequest{}, {}, TwoDisplays(keyboard), true);
    CHECK(keyboard.begun == 1);
    CHECK(keyboard.where == pal::TextDisplay::Second);
    keyboard.Show();
    entry.Update(0.016f);

    // The pad types on the app's keys: the keyboard's field follows.
    CHECK(entry.SelectedKey() == "Q");
    CHECK(entry.HandleEvent(PadNav(input::Nav::Confirm), 1.0f) == TextEntry::Outcome::Open);
    CHECK(entry.Text() == "Q");
    CHECK(keyboard.text == "Q");
    // And the keyboard goes on from there.
    keyboard.Type("Qa");
    entry.Update(0.016f);
    CHECK(entry.Text() == "Qa");
    CHECK(entry.HandleEvent(PadNav(input::Nav::Option), 1.0f) == TextEntry::Outcome::Open);
    CHECK(keyboard.text == "Q");
    CHECK(entry.HandleEvent(PadNav(input::Nav::Menu), 1.0f) == TextEntry::Outcome::Done);
}

TEST_CASE("with one display a request made with the pad uses the app's keys only") {
    FakeKeyboard keyboard;
    TextEntry entry(TextRequest{}, {}, OneDisplay(keyboard), true);
    CHECK(keyboard.begun == 0);
    CHECK(std::string(entry.Mode()) == "keys");
}

TEST_CASE("the app's keys are used where the user prefers them or there is no system keyboard") {
    FakeKeyboard keyboard;
    KeyboardSetup setup = TwoDisplays(keyboard);
    setup.prefer_system = false;
    TextEntry own(TextRequest{}, {}, setup, false);
    CHECK(keyboard.begun == 0);

    keyboard.available = false;
    TextEntry none(TextRequest{}, {}, TwoDisplays(keyboard), false);
    CHECK(keyboard.begun == 0);
    CHECK(std::string(none.Mode()) == "keys");

    keyboard.available = true;
    keyboard.refuse = true;
    TextEntry refused(TextRequest{}, {}, TwoDisplays(keyboard), false);
    CHECK(std::string(refused.Mode()) == "keys");
}

TEST_CASE("a keyboard that does not appear within a second gives way to the app's keys") {
    FakeKeyboard keyboard;
    SECTION("on the second display the keys go there") {
        TextEntry entry(TextRequest{}, {}, TwoDisplays(keyboard), false);
        CHECK(entry.Update(0.5f) == TextEntry::Outcome::Open);
        CHECK(std::string(entry.Mode()) == "system");
        CHECK_FALSE(entry.KeysApart());
        CHECK(entry.Update(0.6f) == TextEntry::Outcome::Open);
        CHECK(keyboard.ended == 1);
        CHECK(entry.KeysApart());
        CHECK(std::string(entry.Mode()) == "apart");
        // The pad still types on them.
        entry.HandleEvent(PadNav(input::Nav::Confirm), 1.0f);
        CHECK(entry.Text() == "Q");
        CHECK(keyboard.set == 0);
    }
    SECTION("on one display they are in the entry itself") {
        TextEntry entry(TextRequest{}, {}, OneDisplay(keyboard), false);
        entry.Update(1.1f);
        CHECK(keyboard.ended == 1);
        CHECK_FALSE(entry.KeysApart());
        CHECK(std::string(entry.Mode()) == "keys");
    }
    SECTION("one that appeared in time stays") {
        TextEntry entry(TextRequest{}, {}, TwoDisplays(keyboard), false);
        entry.Update(0.4f);
        keyboard.Show();
        entry.Update(0.4f);
        entry.Update(5.0f);
        CHECK(keyboard.ended == 0);
        CHECK(std::string(entry.Mode()) == "system");
    }
}

TEST_CASE("a keyboard that is put away gives way to the app's keys and the text stays") {
    FakeKeyboard keyboard;
    TextEntry entry(TextRequest{}, {}, TwoDisplays(keyboard), false);
    keyboard.Show();
    keyboard.Type("abc");
    keyboard.Send(Kind::Hidden);
    keyboard.Type("never read");
    CHECK(entry.Update(0.016f) == TextEntry::Outcome::Open);
    CHECK(entry.Text() == "abc");
    CHECK(entry.KeysApart());
    CHECK(keyboard.ended == 1);
    // Without a session nothing more is asked of the keyboard.
    CHECK(entry.Update(2.0f) == TextEntry::Outcome::Open);
    CHECK(keyboard.ended == 1);
}

TEST_CASE("without the app's keys Confirm finishes and Back cancels") {
    FakeKeyboard keyboard;
    TextRequest request;
    request.text = "Name";
    TextEntry entry(request, {}, TwoDisplays(keyboard), false);
    keyboard.Show();
    entry.Update(0.016f);
    CHECK(entry.HandleEvent(PadNav(input::Nav::Down), 1.0f) == TextEntry::Outcome::Open);
    CHECK(entry.HandleEvent(PadNav(input::Nav::Confirm), 1.0f) == TextEntry::Outcome::Done);
    CHECK(entry.HandleEvent(PadNav(input::Nav::Back), 1.0f) == TextEntry::Outcome::Cancelled);
}

TEST_CASE("the stage starts the keyboard by how the request was made") {
    FakeKeyboard keyboard;
    Stage stage;
    stage.SetKeyboard(TwoDisplays(keyboard));

    // A touch: the line alone, the system keyboard on the second display.
    input::Event press;
    press.type = input::Event::Type::PointerDown;
    stage.HandleEvent(press);
    stage.AskText(TextRequest{}, {});
    REQUIRE(stage.Entry());
    CHECK(keyboard.begun == 1);
    CHECK(keyboard.where == pal::TextDisplay::Second);
    stage.HandleEvent(PadNav(input::Nav::Back));
    CHECK_FALSE(stage.TextEntryOpen());
    CHECK(keyboard.ended == 1);

    // A pad on one display: the app's keys, no session.
    stage.SetKeyboard(OneDisplay(keyboard));
    stage.AskText(TextRequest{}, {});
    CHECK(keyboard.begun == 1);
    CHECK(std::string(stage.Entry()->Mode()) == "keys");

    // Asking again while one is open ends the old session before the new one starts.
    stage.HandleEvent(press);
    stage.AskText(TextRequest{}, {});
    CHECK(keyboard.begun == 2);
    stage.AskText(TextRequest{}, {});
    CHECK(keyboard.begun == 3);
    CHECK(keyboard.ended == 2);
    CHECK(keyboard.active);
}
