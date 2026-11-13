// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <format>
#include <memory>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "model_render.h"

namespace den::app {

namespace {

using plugins::Element;
using plugins::ElementType;
using plugins::PageModel;
using plugins::UiEvent;

constexpr float TILE_W = 144, TILE_GAP = 18, DETAIL_COVER_W = 240, DETAIL_GAP = 36, SHOT_H = 150;
constexpr float DEFAULT_ASPECT = 0.75f; // width / height of a cover
constexpr int MAX_INPUT = 64;
constexpr std::size_t LONG_TEXT = 400; // from here on a pad steps through the paragraphs

struct Builder {
    const ModelSink& sink;
    ModelNodes& nodes;

    std::string NodeId(const Element& element, const std::string& fallback) const {
        return ModelNodeId(element.id.empty() ? "#" + fallback : element.id);
    }

    std::function<void()> Action(const Element& element) const {
        if (element.action.empty()) {
            return {};
        }
        return [send = sink.send, action = element.action, arg = element.arg_json] {
            send(UiEvent{action, arg, {}});
        };
    }

    // Pictures that are still loading get their file later, without building the page again.
    void Watch(const std::string& image, std::function<void(const std::string&)> set) {
        if (!image.empty()) {
            nodes.images.emplace_back(image, std::move(set));
        }
    }

    ui::ImageView* AddPicture(ui::Node* parent, const std::string& image, float w, float h) {
        auto* view = parent->Emplace<ui::ImageView>(sink.image(image));
        view->width = w;
        view->height = h;
        view->radius = Svc().theme->radius;
        view->max_side = 1024;
        Watch(image, [view](const std::string& path) { view->path = path; });
        return view;
    }

    static float Aspect(const Element& element) {
        return element.aspect > 0.2f && element.aspect < 5.0f ? element.aspect : DEFAULT_ASPECT;
    }

    ui::Tile* AddTile(ui::Node* parent, const Element& element, const std::string& fallback) {
        const std::string title = element.title.empty() ? element.label : element.title;
        auto* tile = parent->Emplace<ui::Tile>(title, sink.image(element.image), TILE_W, TILE_W / Aspect(element));
        tile->id = NodeId(element, fallback);
        tile->subtitle = element.subtitle;
        tile->badge = element.badge;
        tile->on_activate = Action(element);
        Watch(element.image, [tile](const std::string& path) { tile->image_path = path; });
        return tile;
    }

    void AddParagraphs(ui::Node* parent, const std::string& text, float width, const std::string& id) {
        const bool stops = text.size() > LONG_TEXT;
        int index = 0;
        for (std::size_t start = 0; start < text.size();) {
            std::size_t end = text.find("\n\n", start);
            end = end == std::string::npos ? text.size() : end;
            auto* paragraph = parent->Emplace<Paragraph>(text.substr(start, end - start), width);
            paragraph->focusable = stops;
            paragraph->id = std::format("{}.text{}", id, index++);
            paragraph->label = "Text";
            start = end + 2;
        }
    }

    void AddButtons(ui::Node* parent, const std::vector<Element>& buttons, const std::string& id) {
        int index = 0;
        for (const Element& button : buttons) {
            if (button.type == ElementType::Button) {
                AddButton(parent, button, std::format("{}.b{}", id, index));
            }
            ++index;
        }
    }

    ModelButton* AddButton(ui::Node* parent, const Element& element, const std::string& fallback) {
        auto* button = parent->Emplace<ModelButton>(element.label.empty() ? element.title : element.label,
                                                    Action(element));
        button->id = NodeId(element, fallback);
        button->SetEnabled(element.enabled);
        return button;
    }

    void AddShelf(ui::Node* list, const Element& element, const std::string& path) {
        ui::Node* section = list->Add(ui::MakeBox(ui::Axis::Column, 6));
        if (!element.title.empty()) {
            auto heading = ui::MakeHeading(element.title);
            heading->size = 20;
            heading->tone = ui::Tone::Dim;
            section->Add(std::move(heading));
        }
        ui::Node* shelf = section->Add(ui::MakeShelf(TILE_GAP));
        shelf->id = NodeId(element, path);
        shelf->SetPadding(10, 10);
        shelf->pad_right = 32;
        int index = 0;
        for (const Element& tile : element.items) {
            AddTile(shelf, tile, std::format("{}.{}", path, index++));
        }
    }

    // Rows of tiles straight in the page's list, so the page scrolls as one.
    void AddGrid(ui::Node* list, const Element& element, const std::string& path) {
        const int fit = std::max(1, static_cast<int>((sink.width - 20 + TILE_GAP) / (TILE_W + TILE_GAP)));
        const int columns = element.columns > 0 ? std::min(element.columns, fit) : fit;
        ui::Node* row = nullptr;
        int in_row = 0, index = 0;
        for (const Element& tile : element.items) {
            if (!row || in_row == columns) {
                row = list->Add(ui::MakeBox(ui::Axis::Row, TILE_GAP));
                row->align = ui::Align::Start;
                row->pad_left = 10;
                row->pad_top = 6;
                in_row = 0;
            }
            AddTile(row, tile, std::format("{}.{}", path, index++));
            ++in_row;
        }
        if (!element.more_action.empty()) {
            nodes.more_action = element.more_action;
            nodes.more_count = element.items.size();
        }
    }

    void AddDetail(ui::Node* list, const Element& element, const std::string& path) {
        const std::string id = NodeId(element, path);
        ui::Node* top = list->Add(ui::MakeBox(ui::Axis::Row, DETAIL_GAP));
        top->align = ui::Align::Start;
        top->pad_top = 4;
        AddPicture(top, element.image, DETAIL_COVER_W, DETAIL_COVER_W / Aspect(element));
        ui::Node* side = top->Add(ui::MakeBox(ui::Axis::Column, 6));
        side->grow = 1;
        const ui::Theme& theme = *Svc().theme;
        side->Emplace<ui::Label>(element.title, theme.text_title, gfx::FONT_BOLD);
        if (!element.subtitle.empty()) {
            auto* subtitle = side->Emplace<ui::Label>(element.subtitle, theme.text_heading, gfx::FONT_BOLD);
            subtitle->tone = ui::Tone::Dim;
            subtitle->pad_bottom = 8;
        }
        for (const plugins::Fact& fact : element.facts) {
            ui::Node* line = side->Add(ui::MakeBox(ui::Axis::Row, 12));
            auto* name = line->Emplace<ui::Label>(fact.label);
            name->tone = ui::Tone::Dim;
            name->width = 150;
            line->Emplace<ui::Label>(fact.value)->grow = 1;
        }
        if (!element.items.empty()) {
            ui::Node* buttons = side->Add(ui::MakeBox(ui::Axis::Column, 8));
            buttons->pad_top = 14;
            buttons->width = std::min(420.0f, sink.width - DETAIL_COVER_W - DETAIL_GAP);
            AddButtons(buttons, element.items, id);
            for (const auto& child : buttons->Children()) {
                static_cast<ui::Button*>(child.get())->centered = false;
            }
        }
        if (!element.text.empty()) {
            ui::Node* text = list->Add(ui::MakeBox(ui::Axis::Column, 12));
            text->pad_top = 10;
            AddParagraphs(text, element.text, std::min(sink.width - 12, 900.0f), id);
        }
        if (!element.screenshots.empty()) {
            ui::Node* shelf = list->Add(ui::MakeShelf(14));
            shelf->id = id + ".shots";
            shelf->SetPadding(10, 10);
            int index = 0;
            for (const std::string& shot : element.screenshots) {
                auto* view = AddPicture(shelf, shot, SHOT_H / 0.75f, SHOT_H);
                view->id = std::format("{}.shot{}", id, index++);
                view->label = "Picture";
                view->focusable = true;
            }
        }
    }

    void AddText(ui::Node* list, const Element& element, const std::string& path) {
        const ui::Theme& theme = *Svc().theme;
        const std::string id = NodeId(element, path);
        if (element.style == "heading") {
            auto heading = ui::MakeHeading(element.text);
            heading->pad_top = 8;
            list->Add(std::move(heading));
            return;
        }
        if (element.style == "caption" || element.style == "warning") {
            auto* paragraph = list->Emplace<Paragraph>(element.text, std::min(sink.width - 12, 900.0f));
            paragraph->id = id;
            if (element.style == "caption") {
                paragraph->size = theme.text_small;
                paragraph->tone = ui::Tone::Dim;
            } else {
                paragraph->boxed = true;
                paragraph->font = gfx::FONT_BOLD;
            }
            return;
        }
        ui::Node* block = list->Add(ui::MakeBox(ui::Axis::Column, 12));
        AddParagraphs(block, element.text, std::min(sink.width - 12, 900.0f), id);
    }

    void AddInput(ui::Node* list, const Element& element, const std::string& path) {
        auto value = std::make_shared<std::string>(element.value);
        const bool secret = element.secret;
        ui::RowSpec row = ValueRow(
            NodeId(element, path), element.label,
            [value, secret] {
                return secret ? std::string(std::min<std::size_t>(value->size(), 12), '*') : *value;
            },
            [value, secret, send = sink.send, action = element.action, arg = element.arg_json,
             label = element.label] {
                ui::TextRequest request;
                request.title = label;
                request.text = secret ? std::string() : *value;
                request.max_length = MAX_INPUT;
                request.allow_empty = true;
                Svc().stage->AskText(std::move(request), [value, send, action, arg](const std::string& text) {
                    *value = text;
                    send(UiEvent{action, arg, text});
                });
            });
        list->Emplace<ui::Row>(std::move(row));
    }

    void AddEmpty(ui::Node* list, const Element& element, const std::string& path) {
        ui::Node* box = list->Add(ui::MakeBox(ui::Axis::Column, 10));
        box->align = ui::Align::Center;
        box->SetPadding(0, 36);
        box->Emplace<ui::Label>(element.title, Svc().theme->text_heading, gfx::FONT_BOLD);
        if (!element.text.empty()) {
            auto* text = box->Emplace<Paragraph>(element.text, std::min(sink.width - 12, 560.0f));
            text->tone = ui::Tone::Dim;
            text->centered = true;
        }
        if (!element.items.empty()) {
            ui::Node* buttons = box->Add(ui::MakeBox(ui::Axis::Row, 10));
            buttons->pad_top = 8;
            AddButtons(buttons, element.items, NodeId(element, path));
            for (const auto& child : buttons->Children()) {
                child->width = 220;
            }
        }
    }

    void AddElement(ui::Node* list, const Element& element, const std::string& path) {
        switch (element.type) {
        case ElementType::Shelf:
            AddShelf(list, element, path);
            break;
        case ElementType::Grid:
            AddGrid(list, element, path);
            break;
        case ElementType::Tile: {
            ui::Node* row = list->Add(ui::MakeBox(ui::Axis::Row, TILE_GAP));
            row->align = ui::Align::Start;
            row->pad_left = 10;
            AddTile(row, element, path);
            break;
        }
        case ElementType::Detail:
            AddDetail(list, element, path);
            break;
        case ElementType::Text:
            AddText(list, element, path);
            break;
        case ElementType::Image: {
            ui::Node* row = list->Add(ui::MakeBox(ui::Axis::Row, 0));
            row->align = ui::Align::Start;
            const float h = element.height > 0 ? std::clamp(element.height, 40.0f, 480.0f) : 200.0f;
            const float w = std::min(sink.width - 12, h * (element.aspect > 0 ? element.aspect : 16.0f / 9));
            AddPicture(row, element.image, w, h)->id = NodeId(element, path);
            break;
        }
        case ElementType::Button: {
            ui::Node* row = list->Add(ui::MakeBox(ui::Axis::Row, 0));
            row->align = ui::Align::Start;
            ModelButton* button = AddButton(row, element, path);
            button->width = std::min(420.0f, sink.width - 12);
            button->centered = false;
            break;
        }
        case ElementType::Row: {
            ui::RowSpec row = ValueRow(NodeId(element, path), element.label.empty() ? element.title : element.label,
                                       [value = element.value] { return value; }, Action(element));
            row.description = element.subtitle;
            list->Emplace<ui::Row>(std::move(row));
            break;
        }
        case ElementType::Input:
            AddInput(list, element, path);
            break;
        case ElementType::Progress: {
            auto* line = list->Emplace<ProgressLine>(element.label, element.fraction);
            line->id = NodeId(element, path);
            line->pad_top = 6;
            line->pad_bottom = 6;
            if (!element.id.empty()) {
                nodes.progress[element.id] = line;
            }
            break;
        }
        case ElementType::Empty:
            AddEmpty(list, element, path);
            break;
        }
    }

    void AddSearchAndChips(ui::Node* into, const PageModel& page) {
        ui::Node* bar = into->Add(ui::MakeBox(ui::Axis::Row, 14));
        bar->align = ui::Align::Center;
        if (page.has_search) {
            auto* field = bar->Emplace<SearchField>();
            field->id = "pm.search";
            field->label = "Search";
            field->text = page.search_value;
            field->placeholder = page.search_placeholder.empty() ? "Search" : page.search_placeholder;
            field->width = page.chips.empty() ? std::min(sink.width, 640.0f) : std::min(sink.width * 0.4f, 420.0f);
            field->on_activate = [field, send = sink.send, action = page.search_action] {
                ui::TextRequest request;
                request.title = field->placeholder;
                request.text = field->text;
                request.max_length = MAX_INPUT;
                request.allow_empty = true;
                Svc().stage->AskText(std::move(request), [send, action](const std::string& text) {
                    send(UiEvent{action, {}, text});
                });
            };
        }
        if (page.chips.empty()) {
            return;
        }
        ui::Node* shelf = bar->Add(ui::MakeShelf(10));
        shelf->id = "pm.chips";
        shelf->grow = 1;
        shelf->SetPadding(4, 6);
        auto chips = std::make_shared<std::vector<FilterChip*>>();
        auto ids = std::make_shared<std::vector<std::string>>();
        for (const plugins::Chip& chip : page.chips) {
            auto* node = shelf->Emplace<FilterChip>(chip.label);
            node->id = "pm.chip." + chip.id;
            node->selected = chip.selected;
            chips->push_back(node);
            ids->push_back(chip.id);
            node->on_activate = [node, chips, ids, multi = page.chips_multi, send = sink.send,
                                 action = page.chips_action] {
                if (!multi && node->selected) {
                    return; // a tab that is already chosen stays chosen
                }
                const bool on = !node->selected;
                if (!multi) {
                    for (FilterChip* other : *chips) {
                        other->selected = false;
                    }
                }
                node->selected = on;
                std::string value;
                for (std::size_t i = 0; i < chips->size(); ++i) {
                    if ((*chips)[i]->selected) {
                        value += (value.empty() ? "" : ",") + (*ids)[i];
                    }
                }
                send(UiEvent{action, {}, value});
            };
        }
    }
};

void ShapeOf(const Element& element, std::string& out) {
    const bool progress = element.type == ElementType::Progress;
    out += std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}{{", static_cast<int>(element.type), element.id,
                       element.title, element.subtitle, element.text, progress ? "" : element.label, element.value,
                       element.style, element.badge, element.image, element.action, element.arg_json, element.aspect,
                       element.height, element.enabled, element.more_action);
    for (const plugins::Fact& fact : element.facts) {
        out += fact.label + "=" + fact.value + ";";
    }
    for (const std::string& shot : element.screenshots) {
        out += shot + ";";
    }
    for (const Element& item : element.items) {
        ShapeOf(item, out);
    }
    out += "}";
}

void PatchIn(const ModelNodes& nodes, const std::vector<Element>& elements) {
    for (const Element& element : elements) {
        if (element.type == ElementType::Progress) {
            if (const auto found = nodes.progress.find(element.id); found != nodes.progress.end()) {
                found->second->Set(element.label, element.fraction);
            }
        }
        PatchIn(nodes, element.items);
    }
}

} // namespace

std::string ModelNodeId(const std::string& element_id) {
    return "pm." + element_id;
}

ModelNodes BuildModel(ui::Node* into, const PageModel& page, const ModelSink& sink) {
    ModelNodes nodes;
    Builder builder{sink, nodes};
    if (!page.subtitle.empty()) {
        auto* subtitle = into->Emplace<ui::Label>(page.subtitle);
        subtitle->tone = ui::Tone::Dim;
    }
    if (page.has_search || !page.chips.empty()) {
        builder.AddSearchAndChips(into, page);
    }
    if (page.busy && page.body.empty()) {
        // Nothing to show yet: the wait in the middle of the page.
        ui::Node* box = into->Add(ui::MakeBox(ui::Axis::Column, 14));
        box->grow = 1;
        box->align = ui::Align::Center;
        box->justify = ui::Align::Center;
        box->Emplace<ui::Label>("Loading")->tone = ui::Tone::Dim;
        box->Emplace<ProgressBar>()->width = 260;
        return nodes;
    }
    if (page.busy) {
        into->Emplace<ProgressBar>()->height = 6;
    }
    ui::Node* list = into->Add(ui::MakeList(12));
    list->id = "pm.body";
    list->pad_bottom = 28;
    list->pad_right = 12;
    list->scroll_margin = 40;
    nodes.list = list;
    int index = 0;
    for (const Element& element : page.body) {
        builder.AddElement(list, element, std::to_string(index++));
    }
    return nodes;
}

std::string ModelShape(const PageModel& page) {
    std::string out = std::format("{}|{}|{}|{}|{}|{}|{}|{}|", page.title, page.subtitle, page.has_search,
                                  page.search_placeholder, page.search_value, page.search_action, page.chips_action,
                                  page.busy);
    for (const plugins::Chip& chip : page.chips) {
        out += std::format("{}:{}:{};", chip.id, chip.label, chip.selected);
    }
    for (const Element& element : page.body) {
        ShapeOf(element, out);
    }
    return out;
}

void PatchProgress(const ModelNodes& nodes, const PageModel& page) {
    PatchIn(nodes, page.body);
}

void PatchImages(const ModelNodes& nodes, const ModelSink& sink) {
    for (const auto& [image, set] : nodes.images) {
        set(sink.image(image));
    }
}

} // namespace den::app
