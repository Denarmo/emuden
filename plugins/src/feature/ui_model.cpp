// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <optional>

#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

using Kind = JsonValue::Kind;

std::optional<ElementType> TypeOf(const std::string& name) {
    static const std::pair<const char*, ElementType> TYPES[] = {
        {"shelf", ElementType::Shelf},   {"grid", ElementType::Grid},
        {"tile", ElementType::Tile},     {"detail", ElementType::Detail},
        {"text", ElementType::Text},     {"image", ElementType::Image},
        {"button", ElementType::Button}, {"row", ElementType::Row},
        {"input", ElementType::Input},   {"progress", ElementType::Progress},
        {"empty", ElementType::Empty},   {"qr", ElementType::Qr},
    };
    for (const auto& [text, type] : TYPES) {
        if (name == text) {
            return type;
        }
    }
    return std::nullopt;
}

// Text fields also take numbers and booleans ("value = 3").
bool ReadText(const JsonValue& object, const char* key, std::string& out) {
    const JsonValue* value = object.Find(key);
    if (value == nullptr) {
        return false;
    }
    if (value->kind == Kind::String) {
        out = CutText(value->string);
    } else if (value->kind == Kind::Number) {
        const JsonValue number = JsonNumber(value->number);
        out = WriteJson(number);
    } else if (value->kind == Kind::Bool) {
        out = value->boolean ? "true" : "false";
    } else {
        return false;
    }
    return true;
}

bool ReadNumber(const JsonValue& object, const char* key, float& out) {
    const JsonValue* value = object.Find(key);
    if (value == nullptr || value->kind != Kind::Number || !std::isfinite(value->number)) {
        return false;
    }
    out = static_cast<float>(value->number);
    return true;
}

bool ReadBool(const JsonValue& object, const char* key, bool& out) {
    const JsonValue* value = object.Find(key);
    if (value == nullptr || value->kind != Kind::Bool) {
        return false;
    }
    out = value->boolean;
    return true;
}

// The fields every element may carry; also what den.ui.patch may change.
void ReadFields(const JsonValue& object, Element& element) {
    ReadText(object, "id", element.id);
    ReadText(object, "title", element.title);
    ReadText(object, "subtitle", element.subtitle);
    ReadText(object, "text", element.text);
    ReadText(object, "label", element.label);
    ReadText(object, "value", element.value);
    ReadText(object, "style", element.style);
    ReadText(object, "badge", element.badge);
    ReadText(object, "image", element.image);
    ReadText(object, "action", element.action);
    ReadText(object, "more_action", element.more_action);
    if (const JsonValue* arg = object.Find("arg")) {
        element.arg_json = CutText(CompactJson(*arg));
    }
    ReadNumber(object, "aspect", element.aspect);
    ReadNumber(object, "height", element.height);
    ReadNumber(object, "fraction", element.fraction);
    float columns = 0;
    if (ReadNumber(object, "columns", columns)) {
        element.columns = static_cast<int>(columns);
    }
    ReadBool(object, "enabled", element.enabled);
    ReadBool(object, "secret", element.secret);
    if (const JsonValue* facts = object.Find("facts")) {
        element.facts.clear();
        for (const JsonValue& item : facts->items) {
            Fact fact;
            // A fact is {label, value} by name or by position.
            if (item.kind == Kind::Array && item.items.size() == 2) {
                const JsonValue pair =
                    JsonObject({{"label", item.items[0]}, {"value", item.items[1]}});
                ReadText(pair, "label", fact.label);
                ReadText(pair, "value", fact.value);
            } else {
                ReadText(item, "label", fact.label);
                ReadText(item, "value", fact.value);
            }
            element.facts.push_back(std::move(fact));
        }
    }
    if (const JsonValue* shots = object.Find("screenshots")) {
        element.screenshots.clear();
        for (const JsonValue& item : shots->items) {
            if (item.kind == Kind::String) {
                element.screenshots.push_back(CutText(item.string));
            }
        }
    }
}

std::optional<Element> ReadElement(const JsonValue& object, std::optional<ElementType> fallback,
                                   std::size_t& budget);

void ReadItems(const JsonValue* list, ElementType fallback, Element& into, std::size_t& budget) {
    if (list == nullptr) {
        return;
    }
    for (const JsonValue& item : list->items) {
        if (std::optional<Element> child = ReadElement(item, fallback, budget)) {
            into.items.push_back(std::move(*child));
        }
    }
}

std::optional<Element> ReadElement(const JsonValue& object, std::optional<ElementType> fallback,
                                   std::size_t& budget) {
    if (object.kind != Kind::Object || budget == 0) {
        return std::nullopt;
    }
    const std::string name = object.String("type");
    const std::optional<ElementType> type = name.empty() ? fallback : TypeOf(name);
    if (!type) {
        return std::nullopt; // a type of a later API version
    }
    --budget;
    Element element;
    element.type = *type;
    ReadFields(object, element);
    switch (*type) {
    case ElementType::Shelf:
    case ElementType::Grid:
        ReadItems(object.Find("items"), ElementType::Tile, element, budget);
        break;
    case ElementType::Detail:
        ReadItems(object.Find("buttons"), ElementType::Button, element, budget);
        break;
    case ElementType::Empty:
        if (const JsonValue* button = object.Find("button")) {
            if (std::optional<Element> child = ReadElement(*button, ElementType::Button, budget)) {
                element.items.push_back(std::move(*child));
            }
        }
        break;
    default:
        break;
    }
    return element;
}

Element* FindElement(std::vector<Element>& elements, const std::string& id) {
    for (Element& element : elements) {
        if (element.id == id) {
            return &element;
        }
        if (Element* inner = FindElement(element.items, id)) {
            return inner;
        }
    }
    return nullptr;
}

} // namespace

std::string CutText(std::string text) {
    if (text.size() > MAX_STRING) {
        std::size_t end = MAX_STRING;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) {
            --end;
        }
        text.resize(end);
    }
    return text;
}

PageModel PageFromJson(const JsonValue& page) {
    PageModel model;
    if (page.kind != Kind::Object) {
        return model;
    }
    ReadText(page, "title", model.title);
    ReadText(page, "subtitle", model.subtitle);
    ReadBool(page, "busy", model.busy);
    if (const JsonValue* search = page.Find("search"); search && search->kind == Kind::Object) {
        model.has_search = true;
        ReadText(*search, "placeholder", model.search_placeholder);
        ReadText(*search, "value", model.search_value);
        ReadText(*search, "action", model.search_action);
    }
    if (const JsonValue* chips = page.Find("chips"); chips && chips->kind == Kind::Object) {
        ReadText(*chips, "action", model.chips_action);
        ReadBool(*chips, "multi", model.chips_multi);
        if (const JsonValue* items = chips->Find("items")) {
            for (const JsonValue& item : items->items) {
                Chip chip;
                if (ReadText(item, "id", chip.id) && model.chips.size() < 64) {
                    ReadText(item, "label", chip.label);
                    ReadBool(item, "selected", chip.selected);
                    model.chips.push_back(std::move(chip));
                }
            }
        }
    }
    std::size_t budget = MAX_ELEMENTS;
    if (const JsonValue* body = page.Find("body")) {
        for (const JsonValue& item : body->items) {
            if (std::optional<Element> element = ReadElement(item, std::nullopt, budget)) {
                model.body.push_back(std::move(*element));
            }
        }
    }
    return model;
}

bool PatchElement(PageModel& page, const std::string& id, const JsonValue& fields) {
    Element* element = id.empty() ? nullptr : FindElement(page.body, id);
    if (element == nullptr || fields.kind != Kind::Object) {
        return false;
    }
    const std::string kept = element->id;
    ReadFields(fields, *element);
    element->id = kept;
    return true;
}

} // namespace den::plugins::feature
