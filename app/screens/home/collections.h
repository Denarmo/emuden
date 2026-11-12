// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The user's own collections of games, and how the library is ordered and laid out.

#pragma once

#include <string>
#include <vector>

#include "den/library/library.h"

namespace den::app {

std::vector<std::string> Collections();
/// The ids in a collection, in the order they were added.
std::vector<std::string> CollectionIds(const std::string& name);
/// The collection's games that are in the library and not hidden.
std::vector<library::Entry> CollectionGames(const std::string& name);
bool InCollection(const std::string& name, const std::string& id);
void SetInCollection(const std::string& name, const std::string& id, bool in);
/// Makes an empty collection called "Collection N" and returns its name.
std::string CreateCollection();
void DeleteCollection(const std::string& name);

enum class Sort { Name, LastPlayed, PlayTime, TimesPlayed, Console, Size };
/// The texts of the Sort values, in their order.
std::vector<std::string> SortNames();
Sort LibrarySort();
void SetLibrarySort(Sort sort);
void SortGames(std::vector<library::Entry>& games, Sort sort);

/// The library as a list of rows instead of a grid of covers ("library.view").
bool LibraryListView();
void SetLibraryListView(bool list);

} // namespace den::app
