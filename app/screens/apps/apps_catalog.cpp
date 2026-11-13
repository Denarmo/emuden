// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// With EMUDEN_FAKE_APPS=1 a made-up list stands in where the platform has none (the desktop),
// so the screens can be driven there; opening one of them only says so.

#include <memory>

#include "../companion/second_screen.h"
#include "../player/player_screen.h"
#include "apps_catalog.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/log.h"
#include "den/platform/apps.h"
#include "den/platform/text_input.h"

namespace den::app {

namespace {

constexpr const char* FAVOURITES_KEY = "apps.favourites";
constexpr const char* RECENT_KEY = "apps.recent";
constexpr double FOCUS_WAIT = 5.0; // seconds an app opened beside the game has to take the keys

struct Catalog {
    system::AppShelf shelf;
    std::uint64_t revision = 1;
    std::uint64_t listed = 0; // the platform's revision the list was read at
    JobPtr job;
    bool loaded = false;
    // An app opened beside the game.
    bool beside = false, focus_lost = false;
    double beside_since = 0;
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
    if (c.beside) {
        const bool focused = pal::apps::HasInputFocus();
        if (!focused) {
            c.focus_lost = true;
        } else if (c.focus_lost || pal::NowSeconds() - c.beside_since > FOCUS_WAIT) {
            c.beside = false; // the keys are back, or the app never took them
        }
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
    PlayerScreen* screen = PlayerScreen::Current();
    pal::apps::Display where = pal::apps::Display::Default;
    bool game_waits = false;
    if (beside_game && screen) {
        if (SecondScreenReady() && !screen->Swapped()) {
            where = pal::apps::Display::Second;
        } else {
            // The app takes the display this window is on (the only one, or the one the
            // quick settings were swapped to): the game waits, with its save on disk.
            where = SecondScreenReady() ? pal::apps::Display::Main : pal::apps::Display::Default;
            game_waits = true;
        }
    }
    if (game_waits) {
        screen->Pause();
        Svc().player->FlushSaves();
    }
    if (Fake()) {
        stage.Toast("Would open " + app.label);
    } else {
        const Status opened = pal::apps::Launch(id, where);
        if (!opened) {
            stage.Toast(opened.error.empty() ? "The app could not be opened" : opened.error, 5.0f);
            return;
        }
        for (const std::string& note : opened.notes) {
            stage.Toast(note, 5.0f);
        }
        if (where == pal::apps::Display::Second && opened.notes.empty()) {
            c.beside = true;
            c.focus_lost = false;
            c.beside_since = pal::NowSeconds();
        } else if (where == pal::apps::Display::Second && screen) {
            // It opened over the game after all.
            screen->Pause();
            Svc().player->FlushSaves();
        }
        log::Info("apps", "opened {}{}", app.id, where == pal::apps::Display::Second ? " beside the game" : "");
    }
    c.shelf.NoteOpened(id);
    Keep(c);
}

bool PadWithOtherApp() {
    const Catalog& c = Get();
    return c.beside && c.focus_lost && !pal::apps::HasInputFocus() && !pal::GetTextInput().Active();
}

DEN_REGISTER_STARTUP(Start);
DEN_REGISTER_FRAME(Frame);

} // namespace den::app
