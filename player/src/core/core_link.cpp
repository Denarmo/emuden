// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What a core needs for play between two devices: games loaded through a subsystem, and
// the netpacket interface of cores that emulate a link cable or local wireless themselves.

#include "core/core_impl.h"
#include "den/base/files.h"
#include "den/base/log.h"

namespace den::player {

namespace {

void RETRO_CALLCONV NetpacketSend(int /*flags*/, const void* buf, std::size_t len, std::uint16_t client_id) {
    // Every packet goes reliably and at once: TCP on a local network.
    if (g_core && g_core->netpacket_running && g_core->packet_send && buf && len > 0) {
        g_core->packet_send({static_cast<const std::uint8_t*>(buf), len}, client_id);
    }
}

void RETRO_CALLCONV NetpacketPollReceive() {
    if (g_core && g_core->netpacket_running && g_core->packet_poll) {
        g_core->packet_poll();
    }
}

} // namespace

bool SetNetpacketInterface(const void* data) {
    const auto* callback = static_cast<const retro_netpacket_callback*>(data);
    if (!g_core || !callback || !callback->start || !callback->receive) {
        return false;
    }
    g_core->netpacket = *callback;
    g_core->netpacket_version = callback->protocol_version ? callback->protocol_version : "";
    g_core->netpacket.protocol_version = nullptr; // the copy above is what is kept
    g_core->has_netpacket = true;
    log::Info("player", "the core plays over the network itself (netpacket{}{})",
              g_core->netpacket_version.empty() ? "" : " ", g_core->netpacket_version);
    return true;
}

bool LibretroCore::LoadGameSpecial(unsigned type, const std::vector<std::filesystem::path>& roms,
                                   std::string* error) {
    Impl& c = *impl_;
    const auto fail = [&](std::string message) {
        if (error) {
            *error = std::move(message);
        }
        return false;
    };
    if (!c.retro_load_game_special) {
        return fail("This core cannot load two games at once.");
    }
    if (c.game_loaded) {
        return fail("A game is already loaded.");
    }
    c.special_paths.clear();
    c.special_data.clear();
    for (const std::filesystem::path& rom : roms) {
        c.special_paths.push_back(PathToUtf8(rom));
        if (!c.info.need_fullpath) {
            auto data = ReadBinaryFile(rom);
            if (!data) {
                return fail("The game file could not be read: " + c.special_paths.back());
            }
            c.special_data.push_back(std::move(*data));
        }
    }
    std::vector<retro_game_info> games(roms.size());
    for (std::size_t i = 0; i < roms.size(); ++i) {
        games[i].path = c.special_paths[i].c_str();
        if (!c.info.need_fullpath) {
            games[i].data = c.special_data[i].data();
            games[i].size = c.special_data[i].size();
        }
    }
    if (!c.retro_load_game_special(type, games.data(), games.size())) {
        return fail("The core could not start these games together.");
    }
    c.rom_path = c.special_paths.empty() ? std::string() : c.special_paths.front();
    c.game_loaded = true;
    for (unsigned port = 0; port < MAX_PORTS; ++port) {
        c.retro_set_controller_port_device(port, RETRO_DEVICE_JOYPAD);
    }
    return true;
}

void LibretroCore::SetSaveRamId(unsigned id) {
    impl_->save_ram_id = id;
}

bool LibretroCore::HasNetpacket() const {
    return impl_->has_netpacket;
}

void LibretroCore::NetpacketStart(std::uint16_t client_id, PacketSend send, std::function<void()> poll) {
    Impl& c = *impl_;
    if (!c.has_netpacket || !c.game_loaded) {
        return;
    }
    if (c.netpacket_running) {
        NetpacketStop();
    }
    c.packet_send = std::move(send);
    c.packet_poll = std::move(poll);
    c.netpacket_running = true;
    c.netpacket.start(client_id, NetpacketSend, NetpacketPollReceive);
}

void LibretroCore::NetpacketReceive(std::span<const std::uint8_t> packet, std::uint16_t from) {
    if (impl_->netpacket_running) {
        impl_->netpacket.receive(packet.data(), packet.size(), from);
    }
}

void LibretroCore::NetpacketPoll() {
    if (impl_->netpacket_running && impl_->netpacket.poll) {
        impl_->netpacket.poll();
    }
}

bool LibretroCore::NetpacketConnected(std::uint16_t client_id) {
    if (!impl_->netpacket_running) {
        return false;
    }
    return !impl_->netpacket.connected || impl_->netpacket.connected(client_id);
}

void LibretroCore::NetpacketDisconnected(std::uint16_t client_id) {
    if (impl_->netpacket_running && impl_->netpacket.disconnected) {
        impl_->netpacket.disconnected(client_id);
    }
}

void LibretroCore::NetpacketStop() {
    Impl& c = *impl_;
    if (!c.netpacket_running) {
        return;
    }
    c.netpacket_running = false;
    if (c.netpacket.stop) {
        c.netpacket.stop();
    }
    c.packet_send = nullptr;
    c.packet_poll = nullptr;
}

} // namespace den::player
