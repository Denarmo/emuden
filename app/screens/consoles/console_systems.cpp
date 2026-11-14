// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The library learns the consoles of the installed plugins: whenever the plugins change, the
// consoles that are ready (their core or engine app is installed) are handed to it. A console
// it did not have has its games searched for at once; one whose plugin is gone is dropped.

#include <algorithm>
#include <set>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/log.h"
#include "den/library/library.h"
#include "den/library/systems.h"
#include "den/plugins/host.h"

namespace den::app {

namespace {

struct State {
    std::uint64_t revision = ~std::uint64_t(0);
    std::set<std::string> builtin;
    std::set<std::string> pushed; // the plugin consoles the library has now
};

State& Get() {
    static State state;
    return state;
}

library::SystemDef ToSystemDef(const plugins::Console& console) {
    library::SystemDef def;
    def.id = console.id;
    def.name = console.name;
    def.short_name = console.short_name;
    def.order = console.order;
    def.extensions = console.extensions;
    def.folder_aliases = console.scan.folder_aliases;
    def.folder_only_extensions = console.scan.folder_only_extensions;
    def.compound_extensions = console.scan.compound_extensions;
    def.zip = console.scan.zip;
    def.scanner = console.scan.scanner;
    def.cover_is_rom = console.scan.cover_is_rom;
    if (!console.scan.index_extensions.empty()) {
        def.multi_file = library::MultiFile{console.scan.index_extensions, console.scan.member_extensions,
                                            console.scan.loose_members, console.scan.track_naming};
    }
    for (const plugins::HeaderTitle& title : console.scan.header_title) {
        def.header_title.push_back({title.offset, title.length});
    }
    def.libretro_thumbnails = console.thumbnails;
    def.launchbox = console.launchbox;
    def.cover_by_id = console.cover_by_id;
    return def;
}

void Frame() {
    Services& svc = Svc();
    if (svc.plugins == nullptr || svc.library == nullptr) {
        return;
    }
    State& state = Get();
    const std::uint64_t revision = svc.plugins->Revision();
    if (revision == state.revision) {
        return;
    }
    state.revision = revision;
    if (state.builtin.empty()) {
        for (const library::SystemDef& def : library::BuiltinSystems()) {
            state.builtin.insert(def.id);
        }
    }
    std::vector<library::SystemDef> systems;
    std::set<std::string> pushed;
    std::vector<std::string> added;
    for (const plugins::Console& console : svc.plugins->Consoles()) {
        if (!console.installed || !console.available) {
            continue;
        }
        pushed.insert(console.id);
        if (!state.builtin.contains(console.id) && !state.pushed.contains(console.id)) {
            added.push_back(console.id);
        }
        systems.push_back(ToSystemDef(console));
    }
    state.pushed = std::move(pushed);
    svc.library->SetSystems(std::move(systems));
    if (!added.empty()) {
        std::string names;
        for (const std::string& id : added) {
            names += (names.empty() ? "" : ", ") + id;
        }
        log::Info("library", "new consoles from plugins: {}", names);
        svc.library->Scan();
    }
}

} // namespace

DEN_REGISTER_FRAME(Frame);

} // namespace den::app
