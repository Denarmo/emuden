// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// rc_client and its server: requests are sent from helper threads and their answers given
// back to rcheevos on the main thread in Pump, so every rcheevos callback runs there.

#include <thread>

#include "den/achievements/games.h"
#include "den/base/log.h"
#include "rc_error.h"
#include "runtime_impl.h"

namespace den::achievements {

namespace {

Runtime::Impl& Of(const rc_client_t* client) {
    return *static_cast<Runtime::Impl*>(rc_client_get_userdata(client));
}

void RC_CCONV ServerCall(const rc_api_request_t* request, rc_client_server_callback_t callback, void* data,
                         rc_client_t* client) {
    Runtime::Impl& self = Of(client);
    HttpRequest out;
    out.url = request->url ? request->url : "";
    out.post_data = request->post_data ? request->post_data : "";
    out.content_type = request->content_type ? request->content_type : "";
    std::shared_ptr<Inbox> inbox = self.inbox;
    HttpFn http = self.config.http;
    self.config.spawn([inbox, http, out = std::move(out), callback, data] {
        Inbox::Answer answer{callback, data, http ? http(out) : HttpResponse{}};
        std::lock_guard lock(inbox->mutex);
        if (!inbox->closed) {
            inbox->answers.push_back(std::move(answer));
        }
    });
}

void RC_CCONV Log(const char* message, const rc_client_t*) {
    log::Info("achievements", "{}", message ? message : "");
}

std::string Message(int result, const char* error) {
    if (error && *error) {
        return error;
    }
    return std::string("RetroAchievements: ") + rc_error_str(result);
}

void RC_CCONV LoginDone(int result, const char* error, rc_client_t* client, void*) {
    Runtime::Impl& self = Of(client);
    if (auto done = std::move(self.login_done)) {
        self.login_done = nullptr;
        done(result, error);
    }
}

void RC_CCONV LoadDone(int result, const char* error, rc_client_t* client, void*) {
    Runtime::Impl& self = Of(client);
    Status status;
    if (result == RC_OK) {
        self.load = LoadState::Loaded;
        self.MapMemory();
    } else if (result == RC_NO_GAME_LOADED || result == RC_NOT_FOUND) {
        self.load = LoadState::Unknown;
        status = Status::Fail("RetroAchievements does not know this version of the game.");
    } else {
        self.load = LoadState::Failed;
        status = Status::Fail(Message(result, error));
    }
    if (auto done = std::move(self.load_done)) {
        self.load_done = nullptr;
        done(status);
    }
}

} // namespace

Runtime::Runtime(Config config) : impl_(std::make_unique<Impl>()) {
    Impl& self = *impl_;
    self.config = std::move(config);
    if (!self.config.spawn) {
        self.config.spawn = [](std::function<void()> work) { std::thread(std::move(work)).detach(); };
    }
    self.client = rc_client_create(ReadMemory, ServerCall);
    rc_client_set_userdata(self.client, &self);
    rc_client_set_event_handler(self.client, HandleEvent);
    rc_client_enable_logging(self.client, RC_CLIENT_LOG_LEVEL_WARN, Log);
    rc_client_set_hardcore_enabled(self.client, 0);
    rc_client_set_unofficial_enabled(self.client, 0);
    if (!self.config.host.empty()) {
        rc_client_set_host(self.client, self.config.host.c_str());
    }
}

Runtime::~Runtime() {
    Impl& self = *impl_;
    {
        std::lock_guard lock(self.inbox->mutex);
        self.inbox->closed = true;
        self.inbox->answers.clear();
    }
    self.login_done = nullptr;
    self.load_done = nullptr;
    rc_client_destroy(self.client);
    self.FreeMemory();
}

void Runtime::Pump() {
    Impl& self = *impl_;
    for (;;) {
        Inbox::Answer answer;
        {
            std::lock_guard lock(self.inbox->mutex);
            if (self.inbox->answers.empty()) {
                break;
            }
            answer = std::move(self.inbox->answers.front());
            self.inbox->answers.pop_front();
        }
        rc_api_server_response_t response{};
        response.body = answer.response.body.c_str();
        response.body_length = answer.response.body.size();
        response.http_status_code =
            answer.response.status == 0 ? RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR : answer.response.status;
        answer.callback(&response, answer.data);
    }
    if (self.load != LoadState::Loaded && rc_client_get_user_info(self.client) != nullptr) {
        // Retries of failed unlocks run from the client's scheduler.
        rc_client_idle(self.client);
    }
}

void Runtime::LoginWithToken(const Credentials& credentials, std::function<void(Status)> done) {
    impl_->login_done = [done = std::move(done)](int result, const char* error) {
        if (done) {
            done(result == RC_OK ? Status{} : Status::Fail(Message(result, error)));
        }
    };
    rc_client_begin_login_with_token(impl_->client, credentials.user.c_str(), credentials.token.c_str(), LoginDone,
                                     nullptr);
}

void Runtime::LoginWithPassword(const std::string& user, const std::string& password,
                                std::function<void(Result<Credentials>)> done) {
    Impl& self = *impl_;
    self.login_done = [&self, done = std::move(done)](int result, const char* error) {
        if (!done) {
            return;
        }
        const rc_client_user_t* info = rc_client_get_user_info(self.client);
        if (result != RC_OK || !info || !info->token) {
            const bool wrong = result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN;
            done(Status::Fail(wrong && !(error && *error) ? "The user name or password is not right."
                                                         : Message(result, error)));
            return;
        }
        done(Credentials{info->username ? info->username : "", info->token});
    };
    rc_client_begin_login_with_password(self.client, user.c_str(), password.c_str(), LoginDone, nullptr);
}

void Runtime::Logout() {
    UnloadGame();
    rc_client_logout(impl_->client);
}

bool Runtime::SignedIn() const {
    return rc_client_get_user_info(impl_->client) != nullptr;
}

std::optional<User> Runtime::UserInfo() const {
    const rc_client_user_t* info = rc_client_get_user_info(impl_->client);
    if (!info) {
        return std::nullopt;
    }
    User user;
    user.name = info->username ? info->username : "";
    user.display_name = info->display_name ? info->display_name : user.name;
    user.avatar_url = info->avatar_url ? info->avatar_url : "";
    user.score = info->score;
    user.score_softcore = info->score_softcore;
    user.unread_messages = info->num_unread_messages;
    return user;
}

void Runtime::SetHardcore(bool on) {
    rc_client_set_hardcore_enabled(impl_->client, on ? 1 : 0);
}

bool Runtime::Hardcore() const {
    return rc_client_get_hardcore_enabled(impl_->client) != 0;
}

void Runtime::LoadGame(std::uint32_t console, const std::string& hash, GameMemory memory,
                       std::function<void(Status)> done) {
    Impl& self = *impl_;
    UnloadGame();
    self.console = console;
    self.memory = std::move(memory);
    self.load = LoadState::Loading;
    self.load_done = std::move(done);
    // Mapped before the game data arrives: rcheevos checks the addresses as it activates.
    self.MapMemory();
    rc_client_begin_load_game(self.client, hash.c_str(), LoadDone, nullptr);
}

void Runtime::UnloadGame() {
    Impl& self = *impl_;
    self.load_done = nullptr;
    if (self.load != LoadState::None) {
        rc_client_unload_game(self.client);
    }
    self.load = LoadState::None;
    self.FreeMemory();
    self.memory = {};
    self.console = 0;
}

LoadState Runtime::GameState() const {
    return impl_->load;
}

void Runtime::RefreshMemory() {
    if (impl_->load != LoadState::None) {
        impl_->MapMemory();
    }
}

void Runtime::DoFrame() {
    if (impl_->load == LoadState::Loaded || impl_->load == LoadState::Loading) {
        rc_client_do_frame(impl_->client);
    }
}

void Runtime::Idle() {
    rc_client_idle(impl_->client);
}

void Runtime::Reset() {
    if (impl_->load == LoadState::Loaded) {
        rc_client_reset(impl_->client);
    }
}

void Runtime::StateLoaded() {
    if (impl_->load == LoadState::Loaded) {
        // No progress was kept with the state: every achievement waits for its start again.
        rc_client_deserialize_progress_sized(impl_->client, nullptr, 0);
    }
}

std::vector<Event> Runtime::TakeEvents() {
    std::vector<Event> out;
    out.swap(impl_->events);
    return out;
}

void Runtime::Impl::MapMemory() {
    FreeMemory();
    if (!memory.region && !memory.map) {
        return;
    }
    static Impl* mapping = nullptr; // rc_libretro's callback carries no user pointer
    mapping = this;
    const auto info = [](uint32_t id, rc_libretro_core_memory_info_t* out) {
        out->data = nullptr;
        out->size = 0;
        if (mapping && mapping->memory.region) {
            const std::span<std::uint8_t> region = mapping->memory.region(id);
            out->data = region.data();
            out->size = region.size();
        }
    };
    regions_ready = rc_libretro_memory_init(&regions, memory.map, info, console) != 0;
    mapping = nullptr;
    if (!regions_ready) {
        log::Warn("achievements", "the core shows no memory for console {}", console);
    }
}

void Runtime::Impl::FreeMemory() {
    if (regions_ready) {
        rc_libretro_memory_destroy(&regions);
    }
    regions = {};
    regions_ready = false;
}

} // namespace den::achievements
