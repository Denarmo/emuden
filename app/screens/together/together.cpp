// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "together.h"

#include <format>
#include <map>
#include <mutex>

#include "../details/game_prefs.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/log.h"
#include "den/platform/device.h"

namespace den::app {

namespace {

constexpr unsigned GAMEBOY_LINK_2P = 0x101;   // RETRO_GAME_TYPE_GAMEBOY_LINK_2P
constexpr unsigned GAMEBOY_1_SRAM = 0x100;    // RETRO_MEMORY_GAMEBOY_1_SRAM
constexpr unsigned GAMEBOY_2_SRAM = 0x200;    // RETRO_MEMORY_GAMEBOY_2_SRAM
constexpr double BROWSE_IDLE_SECONDS = 3.0;   // a browser no page asked for in that long stops
constexpr std::size_t MAX_SAVE_BYTES = 1u << 20;

// The ROM hashes: worked out once per game file, off the main thread.
struct Hashes {
    std::mutex mutex;
    std::map<std::string, std::string> done; // path -> hex
    std::map<std::string, JobPtr> running;
};

Hashes& GetHashes() {
    static Hashes hashes;
    return hashes;
}

std::vector<std::uint8_t> ReadSave(const library::Entry& game) {
    const auto data = ReadBinaryFile(player::BatterySaveFile(Svc().data_dir, game.system, game.id));
    if (!data || data->size() > MAX_SAVE_BYTES) {
        return {};
    }
    return std::vector<std::uint8_t>(data->begin(), data->end());
}

} // namespace

std::string DeviceLabel() {
    static const std::string label = [] {
        // Two copies on one computer (tests) tell themselves apart with it.
        if (std::string name = pal::GetEnv("EMUDEN_DEVICE_NAME"); !name.empty()) {
            return name;
        }
        const pal::DeviceFacts facts = pal::GetDeviceFacts();
        if (!facts.device_name.empty()) {
            return facts.device_name;
        }
        if (!facts.model.empty()) {
            return facts.model;
        }
        return std::string("EmuDEN");
    }();
    return label;
}

TogetherPlan PlanFor(const library::Entry& game) {
    TogetherPlan plan;
    const auto console = Svc().plugins->FindConsole(game.system);
    if (!console || console->runner != plugins::Runner::Core) {
        plan.reason = "Games of this console cannot be played together yet.";
        return plan;
    }
    const auto has_core = [&](const std::string& id) {
        for (const plugins::CoreInfo& core : Svc().plugins->CoresFor(game.system)) {
            if (core.id == id) {
                return true;
            }
        }
        return false;
    };
    if ((game.system == "gb" || game.system == "gbc") && has_core("tgbdual")) {
        plan.mode = net::Mode::Lockstep;
        plan.core_id = "tgbdual";
        plan.subsystem = GAMEBOY_LINK_2P;
        plan.how = "A link cable between two Game Boys: each device runs both, linked, and shows its own. "
                   "Trades and battles work as on the real consoles; each keeps its own save.";
    } else if (game.system == "gba" && has_core("gpsp")) {
        plan.mode = net::Mode::Link;
        plan.core_id = "gpsp";
        plan.how = "A link cable or wireless adapter between two Game Boy Advance, played with gpSP. "
                   "Each device plays its own game and save; in Pokemon go to a Pokemon Center's upper floor.";
    } else if (game.system == "nds" && has_core("melondsds")) {
        plan.mode = net::Mode::Link;
        plan.core_id = "melondsds";
        plan.how = "Local wireless between two DS, played with melonDS DS. Each device plays its own game and save.";
    } else if (const auto core = CoreForGame(game)) {
        plan.mode = net::Mode::Lockstep;
        plan.core_id = core->id;
        plan.how = "Two players on one console: the host's game runs on both devices and each device is "
                   "one controller. Both need the same game file.";
    } else {
        plan.reason = "This console has no core on this device.";
        return plan;
    }
    plan.possible = true;
    return plan;
}

std::optional<plugins::CoreInfo> TogetherCore(const library::Entry& game, const TogetherPlan& plan) {
    for (const plugins::CoreInfo& core : Svc().plugins->CoresFor(game.system)) {
        if (core.id == plan.core_id) {
            return core;
        }
    }
    return std::nullopt;
}

Together& Together::Get() {
    static Together together;
    return together;
}

std::shared_ptr<net::Party> Together::PartyFor(const std::string& game_id) const {
    if (!party_ || (!game_id.empty() && game_id != game_id_)) {
        return nullptr;
    }
    return party_;
}

std::string Together::StatusText() const {
    if (!party_) {
        return "Off";
    }
    if (party_->Joined()) {
        const double ping = party_->PingMs();
        return ping >= 0 ? std::format("With {}  \xC2\xB7  {:.0f} ms", party_->Peer().device, ping)
                         : "With " + party_->Peer().device;
    }
    switch (party_->GetState()) {
    case net::Party::State::Hosting:
        return "Waiting for a player";
    case net::Party::State::Connecting:
        return "Connecting";
    default:
        return party_->EndReason();
    }
}

std::string Together::RomHash(const library::Entry& game) {
    Hashes& hashes = GetHashes();
    {
        std::lock_guard lock(hashes.mutex);
        if (const auto found = hashes.done.find(game.path); found != hashes.done.end()) {
            return found->second;
        }
        if (hashes.running.contains(game.path)) {
            return {};
        }
        hashes.running[game.path] = nullptr;
    }
    JobPtr job = Svc().jobs->Run("hash " + game.name, [path = game.path](Job&) {
        const auto hex = Sha256File(PathFromUtf8(path));
        Hashes& all = GetHashes();
        std::lock_guard inner(all.mutex);
        all.done[path] = hex ? *hex : std::string("unreadable");
        all.running.erase(path);
        return Status{};
    });
    std::lock_guard lock(hashes.mutex);
    if (hashes.running.contains(game.path)) {
        hashes.running[game.path] = std::move(job);
    }
    return {};
}

net::GameInfo Together::InfoFor(const library::Entry& game, const TogetherPlan& plan) const {
    net::GameInfo info;
    info.device = DeviceLabel();
    info.title = game.name;
    info.system = game.system;
    {
        Hashes& hashes = GetHashes();
        std::lock_guard lock(hashes.mutex);
        const auto found = hashes.done.find(game.path);
        info.rom_hash = found != hashes.done.end() ? found->second : std::string();
    }
    info.core = plan.core_id;
    info.mode = plan.mode;
    if (plan.subsystem != 0) {
        info.save = ReadSave(game);
    }
    return info;
}

Status Together::Host(const library::Entry& game, const TogetherPlan& plan) {
    Leave();
    auto party = net::Party::Host(InfoFor(game, plan));
    if (!party) {
        return party.status();
    }
    party_ = std::shared_ptr<net::Party>(party.take());
    game_id_ = game.id;
    plan_ = plan;
    return {};
}

Status Together::Join(const library::Entry& game, const TogetherPlan& plan, const net::FoundHost& host) {
    Leave();
    auto party = net::Party::Join(host.address, InfoFor(game, plan));
    if (!party) {
        return party.status();
    }
    party_ = std::shared_ptr<net::Party>(party.take());
    game_id_ = game.id;
    plan_ = plan;
    return {};
}

void Together::Leave() {
    if (party_) {
        party_->Leave(party_->IsHost() ? "The host ended the session." : "The other player left.");
    }
    party_.reset();
    game_id_.clear();
}

net::Browser* Together::Browse() {
    browse_used_ = pal::NowSeconds();
    if (!browser_) {
        auto browser = net::Browser::Start();
        if (!browser) {
            log::Warn("net", "no browser: {}", browser.error());
            return nullptr;
        }
        browser_ = browser.take();
    }
    return browser_.get();
}

void Together::StopBrowsing() {
    browser_.reset();
}

player::TogetherRequest Together::RequestFor(const library::Entry& game) const {
    player::TogetherRequest request;
    request.party = PartyFor(game.id);
    if (!request.party) {
        return request;
    }
    const bool host = request.party->IsHost();
    if (plan_.subsystem == GAMEBOY_LINK_2P) {
        // Each device runs both Game Boys: its own with its save, the other's with that one.
        request.subsystem = plan_.subsystem;
        request.save_memory = host ? GAMEBOY_1_SRAM : GAMEBOY_2_SRAM;
        request.peer_save_memory = host ? GAMEBOY_2_SRAM : GAMEBOY_1_SRAM;
        request.options = {{"tgbdual_gblink_enable", "enabled"},
                           {"tgbdual_single_screen_mp", host ? "player 1 only" : "player 2 only"},
                           {"tgbdual_audio_output", host ? "Game Boy #1" : "Game Boy #2"}};
    } else if (plan_.mode == net::Mode::Lockstep && !host) {
        request.write_saves = false; // the guest plays the host's game
    }
    return request;
}

void Together::Update() {
    const double now = pal::NowSeconds();
    if (browser_) {
        if (now - browse_used_ > BROWSE_IDLE_SECONDS) {
            browser_.reset();
        } else {
            browser_->Update(now);
        }
    }
    if (!party_) {
        return;
    }
    party_->Update(now);
    while (auto event = party_->NextEvent()) {
        Svc().stage->Toast(event->text, 4.0f);
    }
}

std::optional<plugins::CoreInfo> PlayerCoreFor(const library::Entry& game, bool together) {
    Together& party = Together::Get();
    if (together && party.PartyFor(game.id)) {
        return TogetherCore(game, party.Plan());
    }
    return CoreForGame(game);
}

player::TogetherRequest PlayerTogetherFor(const library::Entry& game, bool together) {
    Together::Get().SetRestarting(false);
    return together ? Together::Get().RequestFor(game) : player::TogetherRequest{};
}

void PlayerLeft(const std::string& game_id) {
    Together& together = Together::Get();
    if (!together.Restarting() && together.PartyFor(game_id)) {
        together.Leave();
    }
}

std::string TogetherWaitText() {
    const auto party = Together::Get().PartyFor();
    if (!party || !party->Joined()) {
        return "Waiting for the other player";
    }
    return party->PeerPaused() ? party->Peer().device + " paused the game" : "Waiting for " + party->Peer().device;
}

namespace {

DEN_REGISTER_FRAME([] { Together::Get().Update(); });

} // namespace

} // namespace den::app
