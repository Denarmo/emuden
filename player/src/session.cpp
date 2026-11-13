// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Opening and closing a game, input, options and the play loop.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/store/settings.h"
#include "pad_map.h"
#include "session_impl.h"

namespace den::player {

namespace fs = std::filesystem;

namespace {

constexpr double SAVE_EVERY_SECONDS = 10.0;
constexpr double LOCK_TOLERANCE = 0.012;
constexpr int MAX_CATCH_UP_FRAMES = 3;
constexpr double FAST_FORWARD_BUDGET_SECONDS = 0.012;
constexpr std::size_t REWIND_BUDGET_BYTES = 64u << 20;
constexpr std::size_t REWIND_MAX_STATE_BYTES = 16u << 20;

} // namespace

PlayerSession::PlayerSession(const Config& config) : config_(config) {}

PlayerSession::~PlayerSession() {
    Close();
}

Status PlayerSession::Fail(std::string message) {
    core_.reset();
    hw_.reset();
    audio_.Close();
    rewind_.reset();
    log::Error("player", "{}", message);
    snapshot_.state = State::Failed;
    snapshot_.error = message;
    return Status::Fail(std::move(message));
}

Status PlayerSession::Open(const OpenRequest& request) {
    Close();
    snapshot_ = Snapshot{};
    snapshot_.game_id = request.game_id;
    return Start(request);
}

Status PlayerSession::Start(const OpenRequest& request) {
    const store::Settings* settings = config_.settings;
    resume_enabled_ = request.resume && (!settings || settings->GetBool("player.resume", true));
    pad_by_label_ = !settings || settings->GetBool("player.pad_by_label", true);
    integer_scale_ = settings && settings->GetBool("player.integer_scale", false);
    smooth_ = !settings || settings->GetBool("player.smooth", true);
    fast_forward_speed_ = std::clamp(
        static_cast<int>(settings ? settings->GetNumber("player.fast_forward_speed", 3) : 3), 2, 16);

    paths_ = MakeGamePaths(config_.data_dir, request.system, request.game_id, request.content);
    std::error_code ec;
    fs::create_directories(paths_.save_dir, ec);
    fs::create_directories(paths_.state_dir, ec);
    if (!request.system_dir.empty()) {
        fs::create_directories(request.system_dir, ec);
    }

    LibretroCore::LoadParams params;
    params.core = request.core;
    params.system_dir = request.system_dir;
    params.save_dir = paths_.save_dir;
    for (const auto& [key, value] : ScreenOptionHints(request.system)) {
        params.options[key] = value;
    }
    for (const auto& [key, value] : request.option_hints) {
        params.options[key] = value;
    }
    if (config_.device) {
        hw_ = std::make_unique<HwContext>(config_.device->Gles());
        params.hw_host = hw_.get();
    }
    std::string error;
    core_ = LibretroCore::Load(params, &error);
    if (!core_) {
        return Fail(error);
    }
    snapshot_.core_name = core_->Info().name;
    system_ = request.system;
    game_key_ = request.game_id;
    for (char& c : game_key_) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    ReloadButtons();
    ApplyStoredOptions();

    fs::path content = request.content;
    if (!content.empty() && !core_->Info().block_extract) {
        content_cache_ = config_.data_dir / "cache" / "content";
        auto resolved = ResolveContent(content, core_->Info().extensions, content_cache_);
        if (!resolved) {
            return Fail(resolved.error());
        }
        content = resolved.take();
    }
    if (!core_->LoadGame(content, &error)) {
        return Fail(error);
    }
    // Options a core only declares once it has the game.
    ApplyStoredOptions();
    ReadAvInfo();
    core_->SetAudioSink([this](std::span<const std::int16_t> samples) { audio_.Push(samples); });
    audio_.Open(av_.sample_rate, !core_->HardwareRendered());
    if (settings) {
        audio_.SetVolume(static_cast<float>(settings->GetNumber("player.volume", 1.0)));
    }
    LoadBatterySave();
    SetupRewind();

    due_ = 0.0;
    step_ = 1.0;
    fps_window_ = 0.0;
    fps_frames_ = 0;
    save_timer_ = 0.0;
    uploaded_serial_ = 0;
    pending_resume_ = resume_enabled_ && IsRegularFile(paths_.Resume());
    snapshot_.state = State::Running;
    snapshot_.picture = picture_;
    snapshot_.can_save_states = core_->StateSize() > 0;
    snapshot_.hardware_rendered = core_->HardwareRendered();
    log::Info("player", "{} {} runs {} ({}x{}, {:.2f} fps, {} Hz{})", snapshot_.core_name,
              core_->Info().version, PathToUtf8(content.filename()), av_.base_width,
              av_.base_height, av_.fps, static_cast<int>(av_.sample_rate),
              snapshot_.hardware_rendered ? ", GPU" : "");
    return {};
}

void PlayerSession::Close() {
    if (core_) {
        FlushSaves();
        SaveResumeState();
        core_.reset();
    }
    hw_.reset();
    audio_.Close();
    rewind_.reset();
    ReleasePicture();
    if (config_.device) {
        ReleasePreviewFrame(*config_.device);
    }
    if (texture_ && config_.device) {
        config_.device->DestroyTexture(texture_);
    }
    texture_ = gfx::Texture{};
    if (!content_cache_.empty()) {
        std::error_code ec;
        fs::remove_all(content_cache_, ec);
        content_cache_.clear();
    }
    snapshot_ = Snapshot{};
}

void PlayerSession::ReadAvInfo() {
    av_ = core_->GetAvInfo();
    snapshot_.core_fps = av_.fps;
    snapshot_.frame_width = static_cast<int>(av_.base_width);
    snapshot_.frame_height = static_cast<int>(av_.base_height);
    if (av_.aspect_ratio > 0) {
        snapshot_.aspect = av_.aspect_ratio;
    }
}

std::string PlayerSession::OptionSettingKey(const std::string& key) const {
    std::string name;
    for (const char c : snapshot_.core_name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    std::string safe = key;
    std::replace(safe.begin(), safe.end(), '.', '_');
    return "core_options." + name + "." + safe;
}

std::string PlayerSession::GameOptionKey(const std::string& key) const {
    std::string safe = key;
    std::replace(safe.begin(), safe.end(), '.', '_');
    return "game_options." + system_ + "." + game_key_ + "." + safe;
}

void PlayerSession::ApplyStoredOptions() {
    if (!config_.settings) {
        return;
    }
    for (const OptionInfo& option : core_->Options()) {
        // This game's own value wins over the console's.
        const std::string game = GameOptionKey(option.key);
        const std::string console = OptionSettingKey(option.key);
        if (config_.settings->Has(game)) {
            core_->SetOption(option.key, config_.settings->GetString(game));
        } else if (config_.settings->Has(console)) {
            core_->SetOption(option.key, config_.settings->GetString(console));
        }
    }
}

std::vector<CoreOption> PlayerSession::Options() const {
    std::vector<CoreOption> out;
    if (!core_) {
        return out;
    }
    for (const OptionInfo& option : core_->Options()) {
        if (!option.visible) {
            continue;
        }
        CoreOption item;
        item.key = option.key;
        item.label = option.label;
        item.value = option.value;
        item.for_game = config_.settings && config_.settings->Has(GameOptionKey(option.key));
        for (const OptionChoice& value : option.values) {
            item.values.push_back(value.value);
        }
        out.push_back(std::move(item));
    }
    return out;
}

void PlayerSession::SetOption(const std::string& key, const std::string& value) {
    SetOption(key, value, OptionScope::Console);
}

void PlayerSession::SetOption(const std::string& key, const std::string& value, OptionScope scope) {
    if (!core_) {
        return;
    }
    core_->SetOption(key, value);
    if (!config_.settings) {
        return;
    }
    if (scope == OptionScope::Game) {
        config_.settings->SetString(GameOptionKey(key), value);
    } else {
        config_.settings->SetString(OptionSettingKey(key), value);
        config_.settings->Remove(GameOptionKey(key));
    }
}

void PlayerSession::ClearGameOptions() {
    if (!core_ || !config_.settings) {
        return;
    }
    for (const OptionInfo& option : core_->Options()) {
        const std::string game = GameOptionKey(option.key);
        if (!config_.settings->Has(game)) {
            continue;
        }
        config_.settings->Remove(game);
        const std::string console = OptionSettingKey(option.key);
        core_->SetOption(option.key, config_.settings->Has(console)
                                         ? config_.settings->GetString(console)
                                         : option.default_value);
    }
}

void PlayerSession::ReloadButtons() {
    for (std::size_t family = 0; family < button_maps_.size(); ++family) {
        button_maps_[family] = LoadButtons(config_.settings, system_,
                                           static_cast<input::PadFamily>(family), pad_by_label_);
    }
}

void PlayerSession::SaveResumeState() {
    if (core_ && resume_enabled_ && snapshot_.can_save_states && snapshot_.frames > 0) {
        if (const Status status = WriteStateFile(paths_.Resume()); !status) {
            log::Warn("player", "no resume state: {}", status.error);
        }
    }
}

void PlayerSession::Pause() {
    if (snapshot_.state == State::Running) {
        snapshot_.state = State::Paused;
        audio_.SetPaused(true);
        FlushSaves();
    }
}

void PlayerSession::Resume() {
    if (snapshot_.state == State::Paused) {
        snapshot_.state = State::Running;
        audio_.SetPaused(false);
        due_ = 0.0;
    }
}

void PlayerSession::SetPad(int port, const input::PadState& pad) {
    if (!core_ || port < 0 || port >= static_cast<int>(MAX_PORTS)) {
        return;
    }
    const auto family = std::min(static_cast<std::size_t>(pad.family), button_maps_.size() - 1);
    const RetroPad mapped = MapPad(pad, system_, button_maps_[family]);
    const auto index = static_cast<unsigned>(port);
    core_->SetButtons(index, mapped.buttons);
    if (index < snapshot_.game_buttons.size()) {
        snapshot_.game_buttons[index] = mapped.buttons;
    }
    core_->SetAnalog(index, 0, mapped.lx, mapped.ly);
    core_->SetAnalog(index, 1, mapped.rx, mapped.ry);
}

void PlayerSession::SetPointer(float x, float y, bool pressed) {
    if (!core_) {
        return;
    }
    const gfx::Rect& r = picture_rect_;
    if (!pressed || r.w <= 0 || r.h <= 0 || !r.Contains(x, y)) {
        core_->SetPointer(0, 0, false);
        return;
    }
    const auto axis = [](float t) {
        return static_cast<std::int16_t>(std::clamp(t * 2.0f - 1.0f, -1.0f, 1.0f) * 32767.0f);
    };
    core_->SetPointer(axis((x - r.x) / r.w), axis((y - r.y) / r.h), true);
}

void PlayerSession::SetScreenPointer(int screen, float u, float v, bool pressed) {
    if (!core_) {
        return;
    }
    const std::vector<GameScreen> screens = Screens();
    if (!pressed || screen < 0 || screen >= static_cast<int>(screens.size()) ||
        !screens[static_cast<std::size_t>(screen)].touch) {
        core_->SetPointer(0, 0, false);
        return;
    }
    // The core takes the pointer across its whole picture.
    const gfx::Rect& part = screens[static_cast<std::size_t>(screen)].part;
    const auto axis = [](float t) {
        return static_cast<std::int16_t>(std::clamp(t * 2.0f - 1.0f, -1.0f, 1.0f) * 32767.0f);
    };
    core_->SetPointer(axis(part.x + std::clamp(u, 0.0f, 1.0f) * part.w),
                      axis(part.y + std::clamp(v, 0.0f, 1.0f) * part.h), true);
}

void PlayerSession::SetFastForward(bool on) {
    if (snapshot_.fast_forward == on) {
        return;
    }
    snapshot_.fast_forward = on;
    due_ = 0.0;
    if (!on) {
        audio_.Clear();
    }
}

void PlayerSession::SetRewind(bool on) {
    on = on && rewind_.has_value();
    if (snapshot_.rewinding == on) {
        return;
    }
    snapshot_.rewinding = on;
    due_ = 0.0;
    audio_.Clear();
}

void PlayerSession::Reset() {
    if (!core_) {
        return;
    }
    core_->Reset();
    pending_resume_ = false;
    if (rewind_) {
        rewind_->Clear();
    }
    audio_.Clear();
}

void PlayerSession::SetupRewind() {
    rewind_.reset();
    const bool wanted = !config_.settings || config_.settings->GetBool("player.rewind", true);
    const std::size_t size = core_->StateSize();
    // A GPU core's states are too slow to take every few frames.
    if (!wanted || core_->HardwareRendered() || size == 0 || size > REWIND_MAX_STATE_BYTES) {
        snapshot_.can_rewind = false;
        return;
    }
    rewind_interval_ = size <= (1u << 20) ? 2 : size <= (4u << 20) ? 4 : 8;
    frames_since_capture_ = 0;
    rewind_.emplace(REWIND_BUDGET_BYTES);
    snapshot_.can_rewind = true;
}

void PlayerSession::CaptureRewind() {
    if (!rewind_ || ++frames_since_capture_ < rewind_interval_) {
        return;
    }
    frames_since_capture_ = 0;
    state_scratch_.resize(core_->StateSize());
    if (core_->SaveState(state_scratch_)) {
        rewind_->Push(state_scratch_);
    }
}

bool PlayerSession::StepRewind() {
    state_scratch_.resize(core_->StateSize());
    if (!rewind_->StepBack(state_scratch_) || !core_->LoadState(state_scratch_)) {
        return false;
    }
    // A state holds no picture: run the frame after it, silently, to draw one.
    core_->SetAudioMuted(true);
    core_->RunFrame();
    core_->SetAudioMuted(false);
    frames_since_capture_ = 0;
    return true;
}

void PlayerSession::RunForward(int frames, bool sound_on_all) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        core_->SetAudioMuted(i > 0 && !sound_on_all);
        core_->RunFrame();
        ++snapshot_.frames;
        ++fps_frames_;
        CaptureRewind();
        if (pending_resume_) {
            // Some cores only take a state once they have run a frame.
            pending_resume_ = false;
            snapshot_.resumed = static_cast<bool>(ReadStateFile(paths_.Resume()));
        }
        const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
        if (!sound_on_all && spent.count() > FAST_FORWARD_BUDGET_SECONDS) {
            break;
        }
    }
    core_->SetAudioMuted(false);
}

void PlayerSession::Advance(double seconds) {
    if (!core_ || snapshot_.state != State::Running) {
        return;
    }
    seconds = std::clamp(seconds, 0.0, 0.1);
    const double frames = seconds * av_.fps;
    step_ += (frames - step_) * 0.1;
    if (snapshot_.rewinding) {
        // One kept state back per the frames between two states: the game's own speed.
        due_ += frames / static_cast<double>(rewind_interval_);
        while (due_ >= 1.0) {
            due_ -= 1.0;
            if (!StepRewind()) {
                due_ = 0.0;
                break;
            }
        }
    } else if (snapshot_.fast_forward) {
        RunForward(fast_forward_speed_, false);
    } else if (std::fabs(step_ - 1.0) < LOCK_TOLERANCE) {
        // The display runs at the core's rate: one frame per display frame, no judder. The
        // sound's rate control takes up the small difference.
        due_ = 0.0;
        RunForward(1, true);
    } else {
        due_ += frames;
        int count = static_cast<int>(due_);
        due_ -= count;
        if (count > MAX_CATCH_UP_FRAMES) {
            count = MAX_CATCH_UP_FRAMES;
            due_ = 0.0;
        }
        RunForward(count, true);
    }
    audio_.Tune();
    if (core_->TakeAvChanged()) {
        const double old_rate = av_.sample_rate;
        ReadAvInfo();
        if (std::fabs(old_rate - av_.sample_rate) > 1.0) {
            audio_.Open(av_.sample_rate, !core_->HardwareRendered());
            if (config_.settings) {
                audio_.SetVolume(static_cast<float>(config_.settings->GetNumber("player.volume", 1.0)));
            }
        }
    }
    MeasureFps(seconds);
    snapshot_.audio_queued_ms = audio_.QueuedSeconds() * 1000.0;
    save_timer_ += seconds;
    if (save_timer_ >= SAVE_EVERY_SECONDS) {
        save_timer_ = 0.0;
        FlushSaves();
    }
}

void PlayerSession::MeasureFps(double seconds) {
    fps_window_ += seconds;
    if (fps_window_ >= 0.5) {
        snapshot_.measured_fps = fps_frames_ / fps_window_;
        fps_window_ = 0.0;
        fps_frames_ = 0;
    }
}

Snapshot PlayerSession::GetSnapshot() const {
    Snapshot snapshot = snapshot_;
    if (core_) {
        snapshot.audio = audio_.Stats();
        // The same gap is seen from both ends: the device missing sound, the queue restarted.
        snapshot.audio_underruns = std::max(snapshot.audio.underruns, snapshot.audio.refills);
        snapshot.audio_capturing = audio_.Capturing();
        snapshot.rewind_states = rewind_ ? rewind_->Count() : 0;
    }
    return snapshot;
}

Status PlayerSession::StartAudioCapture(double seconds) {
    if (!core_ || !audio_.StartCapture(seconds)) {
        return Status::Fail("There is no sound device to record.");
    }
    return {};
}

Result<AudioCapture> PlayerSession::SaveAudioCapture(const std::filesystem::path& wav) {
    const AudioRecording& recording = audio_.FinishCapture();
    if (const Status written = recording.WriteWav(wav); !written) {
        return written;
    }
    return recording.Measure();
}

std::unique_ptr<Session> CreateSession(const Config& config) {
    return std::make_unique<PlayerSession>(config);
}

} // namespace den::player
