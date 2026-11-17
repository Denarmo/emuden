// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Search: a query typed on the on-screen keyboard (pad, touch) or a real keyboard, and the
// matching games as a grid beside it. Route "search", optional param q.

#include <algorithm>
#include <cctype>
#include <cmath>

#include "den/app/game_widgets.h"
#include "den/app/games.h"
#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "den/platform/text_input.h"
#include "page_widths.h"

namespace den::app {

namespace {

constexpr float KEY = 46, KEY_GAP = 6;
constexpr int KEY_COLUMNS = 6;
constexpr float KEYBOARD_W = KEY * KEY_COLUMNS + KEY_GAP * (KEY_COLUMNS - 1);
constexpr float MIN_RESULTS_W = 320; // the results beside the keys need two tiles or so
constexpr int KEYCODE_BACKSPACE = 8, KEYCODE_SPACE = 32, KEYCODE_DELETE = 127;
constexpr std::size_t MAX_QUERY = 48;

// The query with a caret, on a panel tint.
class QueryField final : public ui::Node {
public:
    QueryField() {
        height = 52;
    }
    std::string text, placeholder;

protected:
    void DrawContent(ui::Context& ctx) override {
        const ui::Theme& theme = *ctx.theme;
        ctx.canvas->FillRect(rect, theme.panel, rect.h * 0.5f);
        const float size = theme.text_heading;
        const float y = rect.y + (rect.h - ctx.Text().Metrics(size).line_height) * 0.5f;
        const float x = rect.x + 24;
        if (text.empty()) {
            ctx.canvas->DrawText(placeholder, x + 8, y, size, theme.text_dim);
        }
        const std::string shown = ctx.Text().Elide(text, size, rect.w - 56);
        const float w = ctx.canvas->DrawText(shown, x, y, size, theme.fg);
        if (std::fmod(ctx.time, 1.0) < 0.55) {
            ctx.canvas->FillRect(gfx::Rect{x + w + 2, rect.y + 13, 2, rect.h - 26}, theme.fg);
        }
    }
};

class SearchScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        revision_ = GamesRevision();
        width_ = CanvasWidth();
        if (!started_) {
            query_ = Param("q");
            started_ = true;
        }
        ui::Node* content = BuildRailFrame(*this, Tr("search.title"));
        content->gap = 14;
        content->pad_right = svc.theme->margin;

        field_ = content->Emplace<QueryField>();
        field_->text = query_;
        field_->placeholder = Tr("search.placeholder");
        // A narrow page has no room for the keys beside the results: the field opens the
        // text entry instead, and the results get the width.
        keys_ = RailContentWidth(width_, svc.theme->margin) >= KEYBOARD_W + 28 + MIN_RESULTS_W;
        if (!keys_ || pal::GetTextInput().Available()) {
            // Where the device has a keyboard of its own, the field opens it (the text entry
            // decides between it and the app's keys); the results follow the typing.
            field_->id = "search.field";
            field_->label = Tr("search.title");
            field_->focusable = true;
            field_->chrome = keys_; // the keys below keep the first focus
            field_->on_activate = [this] { AskQuery(); };
        }

        ui::Node* body = content->Add(ui::MakeBox(ui::Axis::Row, 28));
        body->grow = 1;
        body->align = ui::Align::Stretch;
        if (keys_) {
            BuildKeyboard(body);
        }

        results_ = body->Add(ui::MakeBox(ui::Axis::Column, 8));
        results_->grow = 1;
        BuildResults();
    }

    void Update(ui::Context&) override {
        if (GamesRevision() != revision_ || CanvasWidth() != width_) {
            Rebuild();
        }
    }

    bool OnEvent(const input::Event& event) override {
        using Type = input::Event::Type;
        if (event.type == Type::Key) {
            if (!event.down) {
                return false;
            }
            typed_key_ = TypeKey(event.key);
            return typed_key_;
        }
        if (event.type == Type::Text) {
            // A device with text input on sends the same character again as text.
            if (!typed_key_) {
                for (const char c : event.text) {
                    Append(c);
                }
            }
            typed_key_ = false;
            return true;
        }
        if (event.type == Type::Nav) {
            // The keyboard's letters also arrive as navigation (X, Y, Q, E, Space, Backspace).
            const bool swallow = typed_key_ && event.family == input::PadFamily::Keyboard;
            typed_key_ = false;
            return swallow;
        }
        return false;
    }

    bool OnNav(input::Nav nav) override {
        switch (nav) {
        case input::Nav::Option:
            Backspace();
            return true;
        case input::Nav::Extra:
            Append(' ');
            return true;
        case input::Nav::Back:
            if (Svc().stage->Depth() > 1) {
                Svc().stage->Pop();
            } else {
                Svc().stage->Replace("home");
            }
            return true;
        default:
            return false;
        }
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, Tr("hint.select")},
                {input::Nav::Option, Tr("hint.delete")},
                {input::Nav::Extra, Tr("hint.space")},
                {input::Nav::Back, Tr("common.back")}};
    }

private:
    static float CanvasWidth() {
        const float width = Svc().canvas->Width();
        return width > 0 ? width : 1280.0f;
    }

    void BuildKeyboard(ui::Node* body) {
        ui::Node* keyboard = body->Add(ui::MakeBox(ui::Axis::Column, KEY_GAP));
        keyboard->id = "search.keyboard";
        keyboard->width = KEYBOARD_W;
        keyboard->pad_top = 6;
        const std::string keys = "abcdefghijklmnopqrstuvwxyz0123456789";
        ui::Node* row = nullptr;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (i % KEY_COLUMNS == 0) {
                row = keyboard->Add(ui::MakeBox(ui::Axis::Row, KEY_GAP));
            }
            const char c = keys[i];
            AddKey(row, std::string("key.") + c, std::string(1, c), KEY, [this, c] { Append(c); });
        }
        row = keyboard->Add(ui::MakeBox(ui::Axis::Row, KEY_GAP));
        const float wide = (KEYBOARD_W - KEY_GAP * 2) / 3.0f;
        AddKey(row, "key.space", Tr("search.key_space"), wide, [this] { Append(' '); });
        AddKey(row, "key.delete", Tr("search.key_delete"), wide, [this] { Backspace(); });
        AddKey(row, "key.clear", Tr("search.key_clear"), wide, [this] { SetQuery({}); });
    }

    static void AddKey(ui::Node* row, const std::string& id, const std::string& text, float width,
                       std::function<void()> action) {
        auto* key = row->Emplace<ui::Button>(text, std::move(action));
        key->id = id;
        key->width = width;
        key->height = KEY;
        key->SetPadding(2, 0);
    }

    void BuildResults() {
        results_->Clear();
        selected_.clear();
        std::vector<library::Entry> games = WithoutHidden(Svc().library->Entries());
        if (!query_.empty()) {
            std::erase_if(games, [&](const library::Entry& game) { return !Matches(game, query_); });
        }
        auto* count = results_->Emplace<ui::Label>(
            query_.empty()        ? GameCount(games.size())
            : games.size() == 1 ? TrF("search.count_one", query_)
                                : TrF("search.count", games.size(), query_));
        count->tone = ui::Tone::Dim;
        if (games.empty()) {
            return;
        }
        GameTileOptions options;
        options.id_prefix = "search";
        options.on_activate = [](const library::Entry& game) {
            Svc().stage->Push("details", {{"id", game.id}});
        };
        options.on_focus = [this](const library::Entry& game) { selected_ = game.id; };
        const float room = RailContentWidth(width_, Svc().theme->margin) - (keys_ ? KEYBOARD_W + 28 : 0.0f) - 10;
        AddGameGrid(results_, games, GridColumns(room), options)->id = "search.grid";
    }

    void AskQuery() {
        ui::TextRequest request;
        request.title = Tr("search.title");
        request.text = query_;
        request.placeholder = Tr("search.placeholder");
        request.max_length = static_cast<int>(MAX_QUERY);
        request.allow_empty = true;
        request.on_change = [this](const std::string& text) { SetTyped(text); };
        GetStage().AskText(std::move(request), [this](const std::string& text) { SetTyped(text); });
    }

    // A text from the text entry, held to what a query may be: lower case, plain characters.
    void SetTyped(const std::string& text) {
        std::string query;
        for (const char c : text) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u >= 32 && u < 127 && query.size() < MAX_QUERY && !(c == ' ' && query.empty())) {
                query.push_back(static_cast<char>(std::tolower(u)));
            }
        }
        if (query != query_) {
            SetQuery(std::move(query));
        }
    }

    // True when the key was a character of the query (or erased one).
    bool TypeKey(int key) {
        if (key == KEYCODE_BACKSPACE || key == KEYCODE_DELETE) {
            Backspace();
            return true;
        }
        if (key == KEYCODE_SPACE || (key >= '0' && key <= '9') || (key >= 'a' && key <= 'z')) {
            Append(static_cast<char>(key));
            return true;
        }
        return false;
    }

    void Append(char c) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || u >= 127 || query_.size() >= MAX_QUERY || (c == ' ' && query_.empty())) {
            return;
        }
        SetQuery(query_ + static_cast<char>(std::tolower(u)));
    }

    void Backspace() {
        if (!query_.empty()) {
            SetQuery(query_.substr(0, query_.size() - 1));
        }
    }

    // The results change in place when the focus is on the keyboard, so typing keeps its key.
    void SetQuery(std::string query) {
        query_ = std::move(query);
        field_->text = query_;
        const bool in_results = Focus() && Focus()->id.starts_with("search.");
        if (in_results) {
            Rebuild();
        } else {
            BuildResults();
        }
    }

    QueryField* field_ = nullptr;
    ui::Node* results_ = nullptr;
    std::string query_, selected_;
    bool started_ = false, typed_key_ = false;
    float width_ = 0;
    bool keys_ = true; // the app's keys beside the results
    std::uint64_t revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("search", SearchScreen);
DEN_REGISTER_RAIL(20, "Search", "search");

} // namespace den::app
