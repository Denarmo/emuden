// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The player's test endpoint: virtual gamepads, the sound counters and a recording of the
// sound, for checks without a pad in the hand or an ear on the speaker. It listens on the
// port after the automation endpoint's (EMUDEN_AUTOMATION=<port> gives <port>+1), one line in
// and one line out; tools/drive.py sends these commands there by itself.
//
//   vpad attach xbox|playstation|nintendo     ok <id>: a pad SDL plugs in like a real one
//   vpad detach [<id>]                        unplugs it (the newest without an id)
//   vpad press <button...> [ms] [pad=<id>]    holds buttons, 120 ms by default
//   vpad down|up <button...> [pad=<id>]
//   vpad stick left|right <x> <y> [ms]        -1..1; back to the middle after ms when given
//   vpad trigger left|right <0..1> [ms]
//   player                                    JSON: the session, its sound counters, the pads
//   audio reset                               the sound counters start again
//   wav <file> [seconds]                      records the device's output (3 s), answers
//                                             with what it held once the file is written
//
// Buttons: south east west north up down left right l1 r1 l2 r2 l3 r3 start select guide.

#include <cstdlib>
#include <format>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "dual_screen.h"

namespace den::app {

namespace {

using input::PadButton;
namespace vpad = input::virtual_pad;

struct Release {
    double at = 0;
    int pad = 0;
    std::vector<PadButton> buttons;
    int axis_x = -1, axis_y = -1; // virtual_pad::Axis to put back to rest; -1 = none
};

struct Endpoint {
    std::unique_ptr<pal::LineServer> server;
    std::vector<Release> releases;
    std::uint64_t wav_request = 0;
    std::string wav_path;
};

Endpoint& Get() {
    static Endpoint endpoint;
    return endpoint;
}

PadButton ButtonFromName(const std::string& name) {
    static const std::pair<const char*, PadButton> NAMES[] = {
        {"south", PadButton::South},    {"east", PadButton::East},      {"west", PadButton::West},
        {"north", PadButton::North},    {"up", PadButton::DpadUp},      {"down", PadButton::DpadDown},
        {"left", PadButton::DpadLeft},  {"right", PadButton::DpadRight}, {"l1", PadButton::L1},
        {"r1", PadButton::R1},          {"l", PadButton::L1},           {"r", PadButton::R1},
        {"l2", PadButton::L2},          {"r2", PadButton::R2},          {"l3", PadButton::L3},
        {"r3", PadButton::R3},          {"start", PadButton::Start},    {"select", PadButton::Select},
        {"guide", PadButton::Guide},
    };
    for (const auto& [text, button] : NAMES) {
        if (name == text) {
            return button;
        }
    }
    return PadButton::Count;
}

std::string Quoted(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(static_cast<unsigned char>(c) < 0x20 ? ' ' : c);
    }
    return out + "\"";
}

const char* StateName(player::State state) {
    switch (state) {
    case player::State::Running:
        return "running";
    case player::State::Paused:
        return "paused";
    case player::State::Failed:
        return "failed";
    default:
        return "closed";
    }
}

std::string PlayerJson() {
    Services& svc = Svc();
    const player::Snapshot s = svc.player->GetSnapshot();
    const player::AudioStats& a = s.audio;
    std::string pads;
    for (int i = 0; i < svc.input->PadCount(); ++i) {
        const input::PadState& pad = svc.input->Pad(i);
        pads += std::format("{}{{\"player\":{},\"name\":{},\"family\":\"{}\",\"buttons\":{},\"lx\":{:.2f},"
                            "\"ly\":{:.2f},\"rx\":{:.2f},\"ry\":{:.2f},\"lt\":{:.2f},\"rt\":{:.2f}}}",
                            i ? "," : "", i + 1, Quoted(pad.name), input::FamilyName(pad.family),
                            pad.buttons, pad.lx, pad.ly, pad.rx, pad.ry, pad.lt, pad.rt);
    }
    return std::format(
        "{{\"state\":\"{}\",\"game\":{},\"core\":{},\"frames\":{},\"fps\":{:.2f},\"core_fps\":{:.3f},"
        "\"display_fps\":{:.2f},\"fast_forward\":{},\"rewinding\":{},\"can_rewind\":{},\"rewind_states\":{},"
        "\"capturing\":{},\"game_buttons\":[{},{},{},{}],\"active_family\":\"{}\",\"pads\":[{}],"
        "\"audio\":{{\"sample_rate\":{:.0f},\"frames_queued\":{},\"frames_dropped\":{},\"overruns\":{},"
        "\"underruns\":{},\"frames_starved\":{},\"refills\":{},\"target_ms\":{:.1f},\"queue_ms\":{:.1f},\"queue_avg_ms\":{:.1f},"
        "\"queue_min_ms\":{:.1f},\"queue_max_ms\":{:.1f},\"ratio\":{:.5f},\"ratio_min\":{:.5f},"
        "\"ratio_max\":{:.5f}}}}}",
        StateName(s.state), Quoted(s.game_id), Quoted(s.core_name), s.frames, s.measured_fps, s.core_fps,
        svc.fps, s.fast_forward, s.rewinding, s.can_rewind, s.rewind_states, s.audio_capturing,
        s.game_buttons[0], s.game_buttons[1], s.game_buttons[2], s.game_buttons[3],
        input::FamilyName(svc.input->ActiveFamily()), pads, a.sample_rate, a.frames_queued,
        a.frames_dropped, a.overruns, a.underruns, a.frames_starved, a.refills, a.target_ms, a.queue_ms, a.queue_avg_ms,
        a.queue_min_ms, a.queue_max_ms, a.ratio, a.ratio_min, a.ratio_max);
}

std::string VirtualPad(std::istringstream& in, Endpoint& endpoint) {
    std::string verb;
    in >> verb;
    if (verb == "attach") {
        std::string family;
        in >> family;
        const int id = vpad::Attach(family == "playstation" ? input::PadFamily::PlayStation
                                    : family == "nintendo"  ? input::PadFamily::Nintendo
                                                            : input::PadFamily::Xbox);
        return id ? "ok " + std::to_string(id) : "error no virtual pad could be attached";
    }
    const std::vector<int> attached = vpad::Attached();
    if (attached.empty()) {
        return "error no virtual pad is attached";
    }
    if (verb == "detach") {
        int id = attached.back();
        in >> id;
        std::erase_if(endpoint.releases, [id](const Release& release) { return release.pad == id; });
        return vpad::Detach(id) ? "ok" : "error no such virtual pad";
    }
    // The rest: words, numbers and pad=<id> in any order.
    int id = attached.front();
    std::vector<std::string> words;
    std::vector<double> numbers;
    for (std::string word; in >> word;) {
        if (word.rfind("pad=", 0) == 0) {
            id = std::atoi(word.c_str() + 4);
        } else if (word.find_first_not_of("0123456789.-") == std::string::npos) {
            numbers.push_back(std::atof(word.c_str()));
        } else {
            words.push_back(word);
        }
    }
    const double now = pal::NowSeconds();
    if (verb == "press" || verb == "down" || verb == "up") {
        Release release;
        release.pad = id;
        for (const std::string& word : words) {
            const PadButton button = ButtonFromName(word);
            if (button == PadButton::Count) {
                return "error unknown button '" + word + "'";
            }
            if (!vpad::SetButton(id, button, verb != "up")) {
                return "error no such virtual pad";
            }
            release.buttons.push_back(button);
        }
        if (verb == "press") {
            release.at = now + (numbers.empty() ? 120.0 : numbers[0]) / 1000.0;
            endpoint.releases.push_back(std::move(release));
        }
        return "ok";
    }
    if ((verb == "stick" || verb == "trigger") && !words.empty()) {
        const bool right = words[0] == "right";
        Release release;
        release.pad = id;
        bool set = false;
        std::size_t timed = 0;
        if (verb == "stick" && numbers.size() >= 2) {
            release.axis_x = static_cast<int>(right ? vpad::Axis::RightX : vpad::Axis::LeftX);
            release.axis_y = release.axis_x + 1;
            set = vpad::SetAxis(id, static_cast<vpad::Axis>(release.axis_x), static_cast<float>(numbers[0])) &&
                  vpad::SetAxis(id, static_cast<vpad::Axis>(release.axis_y), static_cast<float>(numbers[1]));
            timed = 2;
        } else if (verb == "trigger" && !numbers.empty()) {
            release.axis_x = static_cast<int>(right ? vpad::Axis::RightTrigger : vpad::Axis::LeftTrigger);
            set = vpad::SetAxis(id, static_cast<vpad::Axis>(release.axis_x), static_cast<float>(numbers[0]));
            timed = 1;
        }
        if (!set) {
            return "error vpad " + verb + " needs a side and its values, on an attached pad";
        }
        if (numbers.size() > timed) {
            release.at = now + numbers[timed] / 1000.0;
            endpoint.releases.push_back(std::move(release));
        }
        return "ok";
    }
    return "error unknown vpad command '" + verb + "'";
}

void Pump() {
    Endpoint& endpoint = Get();
    if (!endpoint.server) {
        return;
    }
    Services& svc = Svc();
    const double now = pal::NowSeconds();
    for (auto it = endpoint.releases.begin(); it != endpoint.releases.end();) {
        if (now < it->at) {
            ++it;
            continue;
        }
        for (const PadButton button : it->buttons) {
            vpad::SetButton(it->pad, button, false);
        }
        for (const int axis : {it->axis_x, it->axis_y}) {
            if (axis >= 0) {
                vpad::SetAxis(it->pad, static_cast<vpad::Axis>(axis), 0.0f);
            }
        }
        it = endpoint.releases.erase(it);
    }
    if (endpoint.wav_request && !svc.player->GetSnapshot().audio_capturing) {
        auto saved = svc.player->SaveAudioCapture(PathFromUtf8(endpoint.wav_path));
        endpoint.server->Reply(
            endpoint.wav_request,
            saved ? std::format("ok {{\"sample_rate\":{},\"channels\":{},\"frames\":{},\"peak\":{:.4f},"
                                "\"rms\":{:.4f},\"longest_silence_ms\":{:.1f},\"silence_ms\":{:.1f},"
                                "\"silences\":{}}}",
                                saved->sample_rate, saved->channels, saved->frames, saved->peak, saved->rms,
                                saved->longest_silence_ms, saved->silence_ms, saved->silences)
                  : "error " + saved.error());
        endpoint.wav_request = 0;
    }
    while (auto request = endpoint.server->Next()) {
        std::istringstream in(request->line);
        std::string command;
        in >> command;
        std::string reply = "ok";
        if (command == "vpad") {
            reply = VirtualPad(in, endpoint);
        } else if (command == "player") {
            reply = PlayerJson();
        } else if (command == "screens") {
            reply = DualScreenJson();
        } else if (command == "audio") {
            svc.player->ResetAudioStats();
        } else if (command == "wav") {
            std::string path;
            double seconds = 3.0;
            in >> path >> seconds;
            if (path.empty()) {
                reply = "error wav needs a file path";
            } else if (endpoint.wav_request) {
                reply = "error a recording is already running";
            } else if (const Status started = svc.player->StartAudioCapture(seconds); !started) {
                reply = "error " + started.error;
            } else {
                endpoint.wav_request = request->id;
                endpoint.wav_path = path;
                continue; // answered when the recording is complete
            }
        } else {
            reply = "error unknown command '" + command + "'";
        }
        endpoint.server->Reply(request->id, reply);
    }
}

void Start() {
    const std::string port = pal::GetEnv("EMUDEN_AUTOMATION");
    if (port.empty()) {
        return;
    }
    const int own = std::atoi(port.c_str()) + 1;
    auto server = pal::StartLineServer(own);
    if (!server) {
        log::Error("automation", "player test endpoint: {}", server.error());
        return;
    }
    Get().server = server.take();
    log::Info("automation", "player test endpoint on 127.0.0.1:{}", own);
}

} // namespace

DEN_REGISTER_STARTUP([] { Start(); });
DEN_REGISTER_FRAME([] { Pump(); });

} // namespace den::app
