// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Opening and closing a game, input and the play loop.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/net/party.h"
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
    hardcore_ = request.hardcore;
    allowed_ = request.allowed;
    together_ = request.together;
    // A state of another core, or of the host's game, is no place to resume this one from.
    resume_enabled_ = !together_.party && request.resume && !hardcore_ && allowed_.save_states &&
                      (!settings || settings->GetBool("player.resume", true));
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
    for (const auto& [key, value] : request.option_hints) {
        params.options[key] = value;
    }
    if (config_.device) {
        hw_ = std::make_unique<HwHost>(config_.device->Gles());
        params.hw_host = hw_.get();
    }
    std::string error;
    core_ = LibretroCore::Load(params, &error);
    if (!core_) {
        return Fail(error);
    }
    snapshot_.core_name = core_->Info().name;
    system_ = request.system;
    screen_shapes_ = request.screens.empty() ? BuiltinScreens(request.system) : request.screens;
    game_key_ = request.game_id;
    for (char& c : game_key_) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    if (hw_) {
        // Before the game: cores pick their renderer while it loads.
        hw_->SetChoice(ParseRendererChoice(RendererValue()));
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
    if (!LoadContent(content, &error)) {
        return Fail(error);
    }
    content_path_ = content;
    // Options a core only declares once it has the game.
    ApplyStoredOptions();
    ReadAvInfo();
    core_->SetAudioSink([this](std::span<const std::int16_t> samples) { audio_.Push(samples); });
    audio_.Open(av_.sample_rate, !core_->HardwareRendered());
    if (settings) {
        audio_.SetVolume(static_cast<float>(settings->GetNumber("player.volume", 1.0)));
    }
    if (together_.save_memory != 0) {
        core_->SetSaveRamId(together_.save_memory);
    }
    LoadBatterySave();
    SetupRewind();
    StartTogether();

    due_ = 0.0;
    step_ = 1.0;
    fps_window_ = 0.0;
    fps_frames_ = 0;
    save_timer_ = 0.0;
    uploaded_serial_ = 0;
    pending_resume_ = resume_enabled_ && IsRegularFile(paths_.Resume());
    snapshot_.state = State::Running;
    snapshot_.picture = picture_;
    snapshot_.can_save_states = core_->StateSize() > 0 && allowed_.save_states;
    snapshot_.can_fast_forward = allowed_.fast_forward;
    snapshot_.hardware_rendered = core_->HardwareRendered();
    snapshot_.hardcore = hardcore_;
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
        StopTogether();
        core_.reset();
    }
    together_ = TogetherRequest{};
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

void PlayerSession::ReloadButtons() {
    for (std::size_t family = 0; family < button_maps_.size(); ++family) {
        button_maps_[family] = LoadButtons(config_.settings, system_,
                                           static_cast<input::PadFamily>(family), pad_by_label_);
    }
}

int PlayerSession::MicrophoneButton() const {
    if (!core_) {
        return -1;
    }
    for (const ButtonName& button : core_->ButtonNames()) {
        std::string name = button.name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (button.port == 0 && name.find("microphone") != std::string::npos) {
            return static_cast<int>(button.button);
        }
    }
    return -1;
}

void PlayerSession::SaveResumeState() {
    if (core_ && resume_enabled_ && snapshot_.can_save_states && snapshot_.frames > 0) {
        SettleForOtherCores();
        if (const Status status = WriteStateFile(paths_.Resume()); !status) {
            log::Warn("player", "no resume state: {}", status.error);
        } else {
            WriteThumbnail(paths_.ResumePicture()); // Home's Continue shows it, on every device
        }
    }
}

void PlayerSession::Pause() {
    if (together_.party) {
        together_.party->SetPaused(true);
    }
    if (snapshot_.state == State::Running) {
        snapshot_.state = State::Paused;
        audio_.SetPaused(true);
        FlushSaves();
    }
}

void PlayerSession::Resume() {
    if (together_.party) {
        together_.party->SetPaused(false);
    }
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
    RetroPad mapped = MapPad(pad, system_, button_maps_[family]);
    if (StoreLocalInput(port, mapped)) {
        return;
    }
    const auto index = static_cast<unsigned>(port);
    if (index == 0) {
        mapped.buttons |= held_buttons_;
    }
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
    on = on && !together_.party && allowed_.fast_forward; // the other device keeps the game's own speed
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
    on = on && rewind_.has_value() && !hardcore_ && !together_.party;
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
    Notify(FrameEvent::Reset);
    if (rewind_) {
        rewind_->Clear();
    }
    audio_.Clear();
}

void PlayerSession::SetupRewind() {
    rewind_.reset();
    const bool wanted =
        allowed_.rewind && (!config_.settings || config_.settings->GetBool("player.rewind", true));
    const std::size_t size = core_->StateSize();
    // A GPU core's states are too slow to take every few frames.
    if (!wanted || core_->HardwareRendered() || size == 0 || size > REWIND_MAX_STATE_BYTES) {
        snapshot_.can_rewind = false;
        return;
    }
    rewind_interval_ = size <= (1u << 20) ? 2 : size <= (4u << 20) ? 4 : 8;
    frames_since_capture_ = 0;
    rewind_.emplace(REWIND_BUDGET_BYTES);
    snapshot_.can_rewind = !hardcore_;
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
    Notify(FrameEvent::StateLoaded);
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
        if (core_->TakeMemoryMoved()) {
            Notify(FrameEvent::MemoryMoved);
        }
        Notify(FrameEvent::Frame);
        if (pending_resume_) {
            // Some cores only take a state once they have run a frame.
            pending_resume_ = false;
            const Status resumed = ReadStateFile(paths_.Resume());
            snapshot_.resumed = resumed.ok;
            if (!resumed) {
                // Another GBA core's, which did not convert: the game starts from its save.
                const std::string wrote = StateCoreName(ReadBinaryFile(paths_.Resume()).value_or(std::vector<std::uint8_t>()));
                if (!wrote.empty() && wrote != core_->Info().name) {
                    snapshot_.resume_problem = "Where you left off was saved with " + wrote + ", which " +
                                               core_->Info().name + " cannot load: starting from the game's save";
                }
            }
            Notify(FrameEvent::StateLoaded);
        }
        const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
        if (!sound_on_all && spent.count() > FAST_FORWARD_BUDGET_SECONDS) {
            break;
        }
    }
    core_->SetAudioMuted(false);
}

void PlayerSession::Advance(double seconds) {
    PlanBfiFrame();
    if (!core_ || snapshot_.state != State::Running) {
        return;
    }
    seconds = std::clamp(seconds, 0.0, 0.1);
    const double frames = seconds * av_.fps;
    step_ += (frames - step_) * 0.1;
    if (together_.party) {
        PumpTogether();
        LeftLockstep();
    }
    if (InLockstep()) {
        TickLockstep();
        if (core_ && InLockstep()) {
            AdvanceLockstep(frames);
        }
    } else if (snapshot_.rewinding) {
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
    } else if (AdvanceBfi(seconds)) {
        // One game frame per cycle of refreshes, the others black.
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
