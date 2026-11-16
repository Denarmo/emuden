// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The library learns the consoles of the installed plugins: whenever the plugins change, the
// consoles that are ready (their core or engine app is installed) are handed to it. A console
// it did not have has its games searched for at once; one whose plugin is gone is dropped.
// After an update, a console plugin the app no longer brings is kept if it was in use.

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

// A console plugin an update no longer brings stays where it was installed or the library has
// games of it, and goes elsewhere. One at a time; one that cannot be fetched now is tried next start.
void SettleCarryOvers(Services& svc) {
    static std::set<std::string> tried;
    static JobPtr job;
    if (job && !job->Finished()) {
        return;
    }
    if (job && job->State() == JobState::Failed) {
        log::Warn("plugins", "a console plugin could not be kept: {}", job->Error());
    }
    job.reset();
    const std::set<std::string> seen = svc.library->SystemsWithRecords();
    for (const plugins::CarryOver& carry : svc.plugins->CarryOvers()) {
        if (!tried.insert(carry.plugin_id).second) {
            continue;
        }
        const bool keep = carry.installed || std::any_of(carry.systems.begin(), carry.systems.end(),
                                                         [&](const std::string& system) { return seen.contains(system); });
        log::Info("plugins", "{}: {} after the update", carry.plugin_id, keep ? "kept" : "no longer offered");
        job = svc.plugins->SettleCarryOver(carry.plugin_id, keep);
        return;
    }
}

void Frame() {
    Services& svc = Svc();
    if (svc.plugins == nullptr || svc.library == nullptr) {
        return;
    }
    SettleCarryOvers(svc);
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
