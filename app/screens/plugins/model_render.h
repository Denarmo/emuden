// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Turns a plugin's page (the UI model of docs/feature-plugins.md, section 5) into nodes of
// the UI kit. The page that hosts them owns focus, scrolling and the hint bar.

#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "den/ui/node.h"
#include "den/app/feature_api.h"
#include "den/app/model_widgets.h"

namespace den::app {

struct ModelSink {
    /// What the user did, for the plugin.
    std::function<void(plugins::UiEvent)> send;
    /// The local file of an element's image as UTF-8, empty while it loads.
    std::function<std::string(const std::string& image)> image;
    /// Room for the content in logical units.
    float width = 1216;
};

/// What a built page keeps for the frames after it.
struct ModelNodes {
    ui::Node* list = nullptr;                    // the scrolling body
    std::map<std::string, ProgressLine*> progress; // by element id
    /// Every picture of the page: the image as the model names it, and how its node takes a file.
    std::vector<std::pair<std::string, std::function<void(const std::string& path)>>> images;
    std::string more_action;                     // the grid's, sent when its end is reached
    std::size_t more_count = 0;                  // tiles in that grid
};

/// Fills `into` with the page under its title: subtitle, search and chips, then the body.
ModelNodes BuildModel(ui::Node* into, const plugins::PageModel& page, const ModelSink& sink);

/// Everything of a page except what progress elements show: two pages with the same shape
/// differ only in values PatchProgress can apply without building again.
std::string ModelShape(const plugins::PageModel& page);
void PatchProgress(const ModelNodes& nodes, const plugins::PageModel& page);
/// Gives every picture its file again: those that arrived since the page was built appear.
void PatchImages(const ModelNodes& nodes, const ModelSink& sink);

/// The id a model element's node gets ("pm.<element id>").
std::string ModelNodeId(const std::string& element_id);

} // namespace den::app
