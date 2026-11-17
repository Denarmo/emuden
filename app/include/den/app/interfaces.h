// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Which interface draws the Home screen: EmuDEN's own (the default) or an interface plugin
// (kind "interface") the user chose under Settings, Interface ("ui.home.interface").

#pragma once

#include <memory>
#include <string>

#include "den/ui/page.h"
#include "interface_spec.h"

namespace den::app {

/// The chosen interface plugin's look, or null for the standard Home (also while the chosen
/// one is not installed or cannot be read).
std::shared_ptr<const InterfaceSpec> ChosenInterface();
/// Makes `key` the Home interface ("" = the standard one, "<plugin>" its first look,
/// "<plugin>/<look>" one of its looks) and shows Home with it.
void UseInterface(const std::string& key);

/// The Home page of the chosen interface, or null for the standard Home.
std::unique_ptr<ui::Page> MakeChosenHome();

/// The carousel layout (carousel_home.cpp).
std::unique_ptr<ui::Page> MakeCarouselHome(std::shared_ptr<const InterfaceSpec> spec);

} // namespace den::app
