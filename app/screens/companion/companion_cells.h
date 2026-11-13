// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Parts of the second screen's sections that take whatever room the layout gives them, so a
// section fills the display: a card with a title, and a state slot with its picture.

#pragma once

#include <functional>
#include <string>

#include "den/ui/node.h"

namespace den::app {

/// A tap target filling its rect, its title in the middle.
class CompanionCard final : public ui::Node {
public:
    CompanionCard(std::string title, std::function<void()> action);
    bool selected = false; // the current choice: the stronger tint and bold text

protected:
    ui::Size MeasureContent(ui::Context& ctx) override;
    void DrawContent(ui::Context& ctx) override;

private:
    std::string title_;
};

/// A state slot: its picture fitted in the rect (a panel when empty), its name below.
class CompanionSlot final : public ui::Node {
public:
    CompanionSlot(std::string title, std::string image);

protected:
    void DrawContent(ui::Context& ctx) override;

private:
    std::string title_, image_;
};

/// `count` cells in rows of `columns`, the rows sharing the height of `parent` (a column) and
/// the cells their row's width. `add` puts cell `index` in `row` and returns it.
void CompanionGrid(ui::Node& parent, int count, int columns, float gap,
                   const std::function<ui::Node*(ui::Node& row, int index)>& add);

} // namespace den::app
