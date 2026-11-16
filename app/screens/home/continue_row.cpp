// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <utility>

#include "../details/launch.h"
#include "../sync/sync_glue.h"
#include "continue_row.h"
#include "den/app/app.h"
#include "den/base/files.h"
#include "den/sync/names.h"
#include "den/sync/plan.h"
#include "game_widgets.h"
#include "games.h"

namespace den::app {

namespace fs = std::filesystem;

float DrawChip(ui::Context& ctx, const gfx::Rect& image, const std::string& text, bool right, float room);

namespace {

constexpr std::size_t CARDS = 4;

std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// The download a card started: one at a time.
struct Download {
    std::string key;
    JobPtr job;
};
Download& Current() {
    static Download download;
    return download;
}

bool Downloading(const ContinueCard& card) {
    const Download& d = Current();
    return d.job && !d.job->Finished() && d.key == card.item.key;
}

// The online game library is listed once a run, when a card needs it.
void ListOnlineGames() {
    static JobPtr listed;
    sync::Engine* engine = SyncSvc();
    if (listed || engine == nullptr || engine->Active().empty()) {
        return;
    }
    const std::vector<std::string> on = engine->Categories(engine->Active());
    if (std::find(on.begin(), on.end(), std::string(sync::GAMES)) != on.end()) {
        listed = engine->RefreshRemoteGames();
    }
}

std::string DeviceText(const sync::ContinueItem& item) {
    return item.device.empty() ? std::string("this device") : item.device;
}

} // namespace

std::vector<ContinueCard> ContinueCards() {
    Services& svc = Svc();
    sync::ContinueRequest request;
    request.data_dir = svc.data_dir;
    request.own_id = svc.settings ? svc.settings->GetString("sync.device_id") : std::string();
    request.entries = WithoutHidden(svc.library->Entries());
    request.own_play = svc.library->OwnPlay();
    request.limit = CARDS;
    for (const plugins::Console& console : svc.plugins->Consoles()) {
        if (!console.sync) {
            request.never.insert(console.id);
        }
    }
    std::map<std::string, const library::Entry*> by_id;
    for (const library::Entry& entry : request.entries) {
        by_id[entry.id] = &entry;
    }
    const std::vector<sync::RemoteGame> online = SyncSvc() ? SyncSvc()->RemoteGames() : std::vector<sync::RemoteGame>{};
    std::vector<ContinueCard> cards;
    bool needs_online = false;
    for (sync::ContinueItem& item : sync::ContinueGames(request)) {
        ContinueCard card;
        if (const auto found = by_id.find(item.game_id); found != by_id.end()) {
            card.game = *found->second;
        } else {
            needs_online = true;
            card.game.system = item.system;
            card.game.name = item.stem;
            const auto remote = std::find_if(online.begin(), online.end(), [&](const sync::RemoteGame& game) {
                return game.system == item.system && EqualsNoCase(game.name, item.stem);
            });
            if (remote != online.end()) {
                card.online = *remote;
            }
        }
        card.item = std::move(item);
        cards.push_back(std::move(card));
    }
    if (needs_online) {
        ListOnlineGames();
    }
    return cards;
}

std::string ContinueSignature(const std::vector<ContinueCard>& cards) {
    static std::map<std::string, std::int64_t> pictures; // path -> its time when last shown
    std::string out;
    for (const ContinueCard& card : cards) {
        const std::string picture = PathToUtf8(card.item.screenshot);
        const std::int64_t time = picture.empty() ? 0 : sync::FileTime(card.item.screenshot);
        if (!picture.empty()) {
            auto& known = pictures[picture];
            if (known != 0 && known != time && Svc().textures) {
                Svc().textures->Forget(picture); // a newer moment came down under the same name
            }
            known = time;
        }
        out += std::format("{}|{}|{}|{}|{}|{}|{}\n", card.item.key, card.item.game_id, card.item.time,
                           card.item.device, time, card.online.has_value(), Downloading(card));
    }
    return out;
}

std::string ContinueLine(const ContinueCard& card) {
    if (Downloading(card)) {
        const float fraction = SyncSvc() ? SyncSvc()->State().fraction : -1;
        return fraction >= 0 ? std::format("Downloading {}%", static_cast<int>(std::lround(fraction * 100)))
                             : std::string("Downloading\xE2\x80\xA6");
    }
    std::string line = "On " + DeviceText(card.item) + " \xC2\xB7 " + sync::AgoText(card.item.time, Now());
    if (card.item.game_id.empty()) {
        line += card.online ? " \xC2\xB7 Download " + SizeText(card.online->size) : std::string(" \xC2\xB7 Not here");
    }
    return line;
}

bool ContinueNews(const std::vector<ContinueCard>& cards) {
    static std::string seen;
    if (cards.empty()) {
        return false;
    }
    const std::string newest = cards.front().item.key + "@" + std::to_string(cards.front().item.time);
    return std::exchange(seen, newest) != newest;
}

void RefreshContinue() {
    constexpr std::int64_t STALE = 60;
    sync::Engine* engine = SyncSvc();
    if (engine == nullptr || engine->Active().empty()) {
        return;
    }
    const sync::SyncState state = engine->State();
    if (!state.running && Now() - state.last_sync > STALE) {
        engine->SyncNow();
    }
}

std::string ContinueId(const ContinueCard& card) {
    return card.item.game_id.empty() ? "online:" + card.item.key : card.item.game_id;
}

void ChooseContinue(const ContinueCard& card) {
    ui::Stage& stage = *Svc().stage;
    if (!card.item.game_id.empty()) {
        StartGame(card.item.game_id, false, {{"continue", "1"}, {"from", card.item.device}});
        return;
    }
    const std::string name = card.game.name;
    if (Downloading(card)) {
        stage.Toast(name + " is downloading.");
        return;
    }
    if (!card.online || SyncSvc() == nullptr) {
        stage.Toast(name + " is only on " + DeviceText(card.item) +
                        ". Turn on \"Upload my games\" there to download it here.",
                    5.0f);
        return;
    }
    stage.Confirm("Download " + name + "?",
                  "It was last played on " + DeviceText(card.item) + ". Download it (" + SizeText(card.online->size) +
                      ") to pick up where you left off.",
                  "Download",
                  [game = *card.online, key = card.item.key] {
                      if (SyncSvc() != nullptr) {
                          Current() = {key, SyncSvc()->StartDownload(game)};
                      }
                  },
                  "Cancel", true);
}

ContinueTile::ContinueTile(const ContinueCard& card)
    : ui::Tile(card.game.name, {}, CONTINUE_W, CONTINUE_H) {
    height = CONTINUE_H + 60;
    line = ContinueLine(card);
    screenshot = !card.item.screenshot.empty();
    image_path = screenshot ? PathToUtf8(card.item.screenshot) : card.game.cover;
    cover = screenshot ? card.game.cover : std::string();
    badge = SystemShortName(card.game.system);
    hack = IsHackGame(card.game);
    label = title + ", " + line;
}

void ContinueTile::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    const gfx::Rect image = FocusRect();
    const gfx::Texture texture = image_path.empty() ? gfx::Texture{} : ctx.textures->Get(image_path, 1024);
    if (texture) {
        DrawCover(ctx, image, texture, false);
        if (hack && !screenshot) {
            DrawHackSticker(ctx, image, texture, false);
        }
    } else {
        ctx.canvas->FillRect(image, theme.panel, theme.radius);
        const std::string shown = ctx.Text().Elide(title, theme.text_heading, image.w - 32, gfx::FONT_BOLD);
        const float w = ctx.Text().Measure(shown, theme.text_heading, gfx::FONT_BOLD);
        ctx.canvas->DrawText(shown, image.CenterX() - w * 0.5f, image.CenterY() - theme.text_heading * 0.6f,
                             theme.text_heading, theme.fg, gfx::FONT_BOLD);
    }
    // The box in a corner of the moment, so the game is known at a glance.
    if (!cover.empty()) {
        if (const gfx::Texture box = ctx.textures->Get(cover)) {
            const float h = image.h * 0.42f;
            const float w = std::min(h * static_cast<float>(box.width) / std::max(1, box.height), image.w * 0.4f);
            const gfx::Rect corner{image.x + 10, image.Bottom() - h - 10, w, h};
            ctx.canvas->FillRect(corner.Inset(-2), theme.dialog, theme.radius_small + 2);
            ctx.canvas->DrawImage(corner, box, gfx::Color{255, 255, 255, 255}, theme.radius_small,
                                  gfx::CoverUv(box.width, box.height, corner));
            if (hack) {
                DrawHackSticker(ctx, corner, box, true);
            }
        }
    }
    if (!badge.empty()) {
        DrawChip(ctx, image, badge, false, image.w - 16);
    }
    const float y = image.Bottom() + 8;
    const auto text = [&](const std::string& s, float at, float size, int font, gfx::Color color) {
        ctx.canvas->DrawText(ctx.Text().Elide(s, size, image_width, font), rect.x, at, size, color, font);
    };
    text(title, y, theme.text_body, focused ? gfx::FONT_BOLD : gfx::FONT_REGULAR, focused ? theme.fg : theme.text_dim);
    text(line, y + theme.text_body + 8, theme.text_small, gfx::FONT_REGULAR, theme.text_dim);
}

MomentPicture::MomentPicture(float w, float h) {
    width = w;
    height = h;
}

void MomentPicture::DrawContent(ui::Context& ctx) {
    const gfx::Texture texture = path.empty() ? gfx::Texture{} : ctx.textures->Get(path, 1024);
    if (texture) {
        const gfx::Rect fit = gfx::FitRect(texture.width, texture.height, rect);
        ctx.canvas->FillRect(fit.Inset(-3), ctx.theme->dialog, ctx.theme->radius + 3);
        ctx.canvas->DrawImage(fit, texture, gfx::Color{255, 255, 255, 255}, ctx.theme->radius);
    }
}

} // namespace den::app
