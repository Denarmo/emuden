// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// With EMUDEN_FAKE_APPS=1 a made-up list stands in where the platform has none (the desktop),
// so the screens can be driven there; opening one of them only says so.

#include <memory>

#include "den/app/player.h"
#include "den/app/apps_catalog.h"
#include "den/app/beside.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/log.h"
#include "den/platform/apps.h"

namespace den::app {

namespace {

constexpr const char* FAVOURITES_KEY = "apps.favourites";
constexpr const char* RECENT_KEY = "apps.recent";

struct Catalog {
    system::AppShelf shelf;
    std::uint64_t revision = 1;
    std::uint64_t listed = 0; // the platform's revision the list was read at
    JobPtr job;
    bool loaded = false;
};

Catalog& Get() {
    static Catalog catalog;
    return catalog;
}

bool Fake() {
    static const bool fake = !pal::apps::Available() && !pal::GetEnv("EMUDEN_FAKE_APPS").empty();
    return fake;
}

std::vector<system::AppItem> FakeApps() {
    std::vector<system::AppItem> apps;
    for (const char* name : {"YouTube", "Browser", "Files", "Music", "Gallery", "Camera", "Clock", "Calculator",
                             "Maps", "Mail", "Notes", "Podcasts", "Weather", "Radio", "Settings", "Store",
                             "Translate", "Video Player", "Voice Recorder", "Contacts"}) {
        apps.push_back({std::string("test.") + name + "/.Main", name, {}});
    }
    return apps;
}

void Keep(Catalog& c) {
    Svc().settings->SetList(FAVOURITES_KEY, c.shelf.FavouriteIds());
    Svc().settings->SetList(RECENT_KEY, c.shelf.RecentIds());
    ++c.revision;
}

void Load(Catalog& c) {
    c.listed = pal::apps::Revision();
    if (Fake()) {
        c.shelf.SetApps(FakeApps());
        c.loaded = true;
        ++c.revision;
        return;
    }
    const auto found = std::make_shared<std::vector<pal::apps::App>>();
    c.job = Svc().jobs->Run(
        "apps",
        [found](Job&) {
            *found = pal::apps::List();
            return Status{};
        },
        [found](Job&) {
            Catalog& c = Get();
            std::vector<system::AppItem> apps;
            for (pal::apps::App& app : *found) {
                apps.push_back({std::move(app.id), std::move(app.label), std::move(app.icon)});
            }
            c.shelf.SetApps(std::move(apps));
            c.job.reset();
            c.loaded = true;
            ++c.revision;
            log::Info("apps", "{} apps", c.shelf.Apps().size());
        });
}

void Start() {
    if (!AppsAvailable()) {
        return;
    }
    Catalog& c = Get();
    c.shelf.SetFavouriteIds(Svc().settings->GetList(FAVOURITES_KEY));
    c.shelf.SetRecentIds(Svc().settings->GetList(RECENT_KEY));
    Load(c);
}

void Frame() {
    if (!AppsAvailable()) {
        return;
    }
    Catalog& c = Get();
    // An app was installed, removed or changed.
    if (!c.job && c.listed != pal::apps::Revision()) {
        Load(c);
    }
}

} // namespace

bool AppsAvailable() {
    return pal::apps::Available() || Fake();
}

system::AppShelf& Apps() {
    return Get().shelf;
}

std::uint64_t AppsRevision() {
    return Get().revision;
}

bool AppsLoading() {
    return !Get().loaded;
}

bool AppsFake() {
    return Fake();
}

void ToggleAppFavourite(const std::string& id) {
    Catalog& c = Get();
    c.shelf.ToggleFavourite(id);
    Keep(c);
}

void OpenApp(const std::string& id, bool beside_game, ui::Stage& stage) {
    Catalog& c = Get();
    const system::AppItem* found = c.shelf.Find(id);
    if (!found) {
        return;
    }
    const system::AppItem app = *found;
    if (beside_game && CurrentPlayer()) {
        BesideOpen(app, stage);
    } else if (Fake()) {
        stage.Toast("Would open " + app.label);
    } else {
        const Status opened = pal::apps::Launch(id, pal::apps::Display::Default);
        if (!opened) {
            stage.Toast(opened.error.empty() ? "The app could not be opened" : opened.error, 5.0f);
            return;
        }
        for (const std::string& note : opened.notes) {
            stage.Toast(note, 5.0f);
        }
        log::Info("apps", "opened {}", app.id);
    }
    c.shelf.NoteOpened(id);
    Keep(c);
}

DEN_REGISTER_STARTUP(Start);
DEN_REGISTER_FRAME(Frame);

} // namespace den::app
