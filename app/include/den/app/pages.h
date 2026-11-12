// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared pieces for screens: rows as data, and the in-app file and folder browser.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "den/base/jobs.h"
#include "den/ui/page.h"
#include "den/ui/widgets.h"

namespace den::app {

/// Rebuilds the page on top before the next frame (its rows depend on state that changed).
void RebuildTop();

ui::RowSpec ActionRow(std::string id, std::string label, std::function<void()> action,
                      std::string description = {});
/// A row with a text at the right; `action`, when given, runs on Confirm.
ui::RowSpec ValueRow(std::string id, std::string label, std::function<std::string()> value,
                     std::function<void()> action = {});
ui::RowSpec ToggleRow(std::string id, std::string label, std::function<bool()> get,
                      std::function<void(bool)> set);
ui::RowSpec ChoiceRow(std::string id, std::string label, std::vector<std::string> options,
                      std::function<int()> get, std::function<void(int)> set);
ui::RowSpec SliderRow(std::string id, std::string label, float min, float max, float step,
                      std::function<float()> get, std::function<void(float)> set,
                      std::function<std::string(float)> format = {});

/// Adds a Row per spec to `list`; `row_height` above 0 overrides the kit's height.
void AddRows(ui::Node* list, std::vector<ui::RowSpec> rows, float row_height = 0);

/// What a running job is doing ("Downloading 42%"), or `idle` when there is none.
std::string JobText(const JobPtr& job, const std::string& idle);

struct PickOptions {
    std::string title = "Choose a folder";
    std::string start;                   // UTF-8 folder to open in; empty = the list of places
    bool files = false;                  // pick a file instead of a folder
    std::vector<std::string> extensions; // lower case without the dot; empty = every file
};

/// Opens the in-app browser. `on_pick` gets the UTF-8 path after the browser has closed; it
/// is not called when the user backs out.
void Pick(PickOptions options, std::function<void(const std::string&)> on_pick);

/// Lets the user choose the program of a system's out-of-app engine and remembers it under
/// the engine host's path setting.
void ChooseEnginePath(const std::string& system);

} // namespace den::app
