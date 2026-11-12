// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <format>

#include "den/app/app.h"
#include "den/app/pages.h"

namespace den::app {

void RebuildTop() {
    if (ui::Page* page = Svc().stage->Top()) {
        page->Rebuild();
    }
}

ui::RowSpec ActionRow(std::string id, std::string label, std::function<void()> action,
                      std::string description) {
    ui::RowSpec row;
    row.kind = ui::RowSpec::Kind::Action;
    row.id = std::move(id);
    row.label = std::move(label);
    row.description = std::move(description);
    row.action = std::move(action);
    return row;
}

ui::RowSpec ValueRow(std::string id, std::string label, std::function<std::string()> value,
                     std::function<void()> action) {
    ui::RowSpec row;
    row.kind = ui::RowSpec::Kind::Value;
    row.id = std::move(id);
    row.label = std::move(label);
    row.value = std::move(value);
    row.action = std::move(action);
    return row;
}

ui::RowSpec ToggleRow(std::string id, std::string label, std::function<bool()> get,
                      std::function<void(bool)> set) {
    ui::RowSpec row;
    row.kind = ui::RowSpec::Kind::Toggle;
    row.id = std::move(id);
    row.label = std::move(label);
    row.get_bool = std::move(get);
    row.set_bool = std::move(set);
    return row;
}

ui::RowSpec ChoiceRow(std::string id, std::string label, std::vector<std::string> options,
                      std::function<int()> get, std::function<void(int)> set) {
    ui::RowSpec row;
    row.kind = ui::RowSpec::Kind::Choice;
    row.id = std::move(id);
    row.label = std::move(label);
    row.options = std::move(options);
    row.get_index = std::move(get);
    row.set_index = std::move(set);
    return row;
}

ui::RowSpec SliderRow(std::string id, std::string label, float min, float max, float step,
                      std::function<float()> get, std::function<void(float)> set,
                      std::function<std::string(float)> format) {
    ui::RowSpec row;
    row.kind = ui::RowSpec::Kind::Slider;
    row.id = std::move(id);
    row.label = std::move(label);
    row.min = min;
    row.max = max;
    row.step = step;
    row.get_number = std::move(get);
    row.set_number = std::move(set);
    row.format = std::move(format);
    return row;
}

void AddRows(ui::Node* list, std::vector<ui::RowSpec> rows, float row_height) {
    for (ui::RowSpec& spec : rows) {
        auto* row = list->Emplace<ui::Row>(std::move(spec));
        if (row_height > 0) {
            row->height = row_height;
        }
    }
}

std::string JobText(const JobPtr& job, const std::string& idle) {
    if (!job || job->Finished()) {
        return idle;
    }
    std::string text = job->Message();
    if (text.empty()) {
        text = "Working";
    }
    const float progress = job->Progress();
    if (progress >= 0) {
        text += std::format(" {}%", static_cast<int>(progress * 100.0f + 0.5f));
    }
    return text;
}

} // namespace den::app
