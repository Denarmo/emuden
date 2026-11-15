// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/net/discovery.h"

#include <algorithm>
#include <cstring>

#include "den/net/wire.h"

namespace den::net {

namespace {

constexpr std::uint32_t BEACON_MAGIC = 0x424e4445; // "EDNB"
constexpr std::uint32_t QUERY_MAGIC = 0x514e4445;  // "EDNQ"
constexpr double BEACON_SECONDS = 1.0;
constexpr double ASK_SECONDS = 1.0;
constexpr double INTERFACES_SECONDS = 10.0;
constexpr std::uint32_t LIMITED_BROADCAST = 0xffffffffu;

std::vector<std::uint8_t> Query() {
    Writer writer;
    writer.U32(QUERY_MAGIC).U16(PROTOCOL);
    return writer.Data();
}

bool IsQuery(std::span<const std::uint8_t> data) {
    Reader reader(data);
    return reader.U32() == QUERY_MAGIC && !reader.Failed();
}

// Every subnet's broadcast address, the limited broadcast and this device itself.
std::vector<std::uint32_t> Targets(const std::vector<pal::net::Interface>& interfaces) {
    std::vector<std::uint32_t> out{LIMITED_BROADCAST, pal::net::Address::Loopback(0).ip};
    for (const pal::net::Interface& item : interfaces) {
        if (std::find(out.begin(), out.end(), item.broadcast) == out.end()) {
            out.push_back(item.broadcast);
        }
    }
    return out;
}

} // namespace

std::vector<std::uint8_t> Beacon::Encode() const {
    Writer writer;
    writer.U32(BEACON_MAGIC).U16(PROTOCOL).U32(id).U16(port).U8(static_cast<std::uint8_t>(mode)).U8(busy ? 1 : 0);
    writer.Str(device).Str(title).Str(system).Str(rom_hash).Str(core);
    return writer.Data();
}

bool Beacon::Decode(std::span<const std::uint8_t> data) {
    Reader reader(data);
    if (reader.U32() != BEACON_MAGIC || reader.U16() != PROTOCOL) {
        return false;
    }
    id = reader.U32();
    port = reader.U16();
    const std::uint8_t kind = reader.U8();
    busy = reader.U8() != 0;
    device = reader.Str();
    title = reader.Str();
    system = reader.Str();
    rom_hash = reader.Str();
    core = reader.Str();
    if (reader.Failed() || (kind != static_cast<std::uint8_t>(Mode::Link) &&
                            kind != static_cast<std::uint8_t>(Mode::Lockstep))) {
        return false;
    }
    mode = static_cast<Mode>(kind);
    return true;
}

Announcer::Announcer(Beacon beacon, std::unique_ptr<pal::net::UdpSocket> socket, std::uint16_t port)
    : beacon_(std::move(beacon)), socket_(std::move(socket)), discovery_port_(port) {}

Result<std::unique_ptr<Announcer>> Announcer::Start(Beacon beacon, std::uint16_t discovery_port) {
    auto socket = pal::net::OpenUdp(discovery_port, true);
    if (!socket) {
        return socket.status();
    }
    return std::unique_ptr<Announcer>(new Announcer(std::move(beacon), socket.take(), discovery_port));
}

void Announcer::Broadcast(const std::vector<std::uint8_t>& data) {
    for (const std::uint32_t ip : Targets(interfaces_)) {
        socket_->SendTo({ip, discovery_port_}, data);
    }
}

void Announcer::Update(double now) {
    if (now - interfaces_read_ >= INTERFACES_SECONDS) {
        interfaces_read_ = now;
        interfaces_ = pal::net::LocalInterfaces();
    }
    const std::vector<std::uint8_t> beacon = beacon_.Encode();
    while (auto datagram = socket_->Receive()) {
        if (IsQuery(datagram->data)) {
            socket_->SendTo(datagram->from, beacon);
        }
    }
    if (now - last_sent_ >= BEACON_SECONDS) {
        last_sent_ = now;
        Broadcast(beacon);
    }
}

Browser::Browser(std::unique_ptr<pal::net::UdpSocket> asker, std::unique_ptr<pal::net::UdpSocket> listener,
                 std::uint16_t port)
    : asker_(std::move(asker)), listener_(std::move(listener)), discovery_port_(port) {}

Result<std::unique_ptr<Browser>> Browser::Start(std::uint16_t discovery_port) {
    auto asker = pal::net::OpenUdp(0, false);
    if (!asker) {
        return asker.status();
    }
    // Hearing beacons is a bonus: the answers to the queries are enough.
    auto listener = pal::net::OpenUdp(discovery_port, true);
    return std::unique_ptr<Browser>(
        new Browser(asker.take(), listener ? listener.take() : nullptr, discovery_port));
}

void Browser::Heard(const pal::net::UdpSocket::Datagram& datagram, double now) {
    Beacon beacon;
    if (!beacon.Decode(datagram.data)) {
        return;
    }
    const pal::net::Address address{datagram.from.ip, beacon.port};
    // The same host answers on every address it was asked at. Only a host on this device answers
    // on loopback, and loopback wins there: a firewall can block its network address.
    for (FoundHost& host : hosts_) {
        if (host.beacon.id == beacon.id && host.beacon.device == beacon.device) {
            if (address.ip == pal::net::Address::Loopback(0).ip) {
                host.address = address;
            }
            host.beacon = std::move(beacon);
            host.seen = now;
            return;
        }
    }
    hosts_.push_back({std::move(beacon), address, now});
}

void Browser::Update(double now) {
    if (now - last_asked_ >= ASK_SECONDS) {
        last_asked_ = now;
        const std::vector<std::uint8_t> query = Query();
        for (const std::uint32_t ip : Targets(pal::net::LocalInterfaces())) {
            asker_->SendTo({ip, discovery_port_}, query);
        }
    }
    while (auto datagram = asker_->Receive()) {
        Heard(*datagram, now);
    }
    while (listener_) {
        auto datagram = listener_->Receive();
        if (!datagram) {
            break;
        }
        Heard(*datagram, now);
    }
    std::erase_if(hosts_, [now](const FoundHost& host) { return now - host.seen > FORGET_SECONDS; });
}

std::vector<FoundHost> Browser::Hosts(double now) const {
    std::vector<FoundHost> out;
    for (const FoundHost& host : hosts_) {
        if (now - host.seen <= FORGET_SECONDS) {
            out.push_back(host);
        }
    }
    std::sort(out.begin(), out.end(), [](const FoundHost& a, const FoundHost& b) { return a.seen > b.seen; });
    return out;
}

} // namespace den::net
