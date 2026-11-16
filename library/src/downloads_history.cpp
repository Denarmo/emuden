// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The list of setups (<data>/library/downloads.json) and taking one back.

#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <memory>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/library/hacks.h"
#include "downloads_work.h"

namespace den::library::downloads {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t KEPT = 200;

constexpr const char* NAMES[] = {"added",   "hack_added", "needs_base", "missing_base",
                                 "waiting", "bios",       "unknown",    "failed"};

Outcome OutcomeFrom(const std::string& name) {
    for (std::size_t i = 0; i < std::size(NAMES); ++i) {
        if (name == NAMES[i]) {
            return static_cast<Outcome>(i);
        }
    }
    return Outcome::Unknown;
}

JsonValue Strings(const std::vector<std::string>& items) {
    std::vector<JsonValue> out;
    for (const std::string& item : items) {
        out.push_back(JsonString(item));
    }
    return JsonArray(std::move(out));
}

std::vector<std::string> StringsOf(const JsonValue* value) {
    std::vector<std::string> out;
    if (value != nullptr) {
        for (const JsonValue& item : value->items) {
            out.push_back(item.string);
        }
    }
    return out;
}

JsonValue ToJson(const Record& record) {
    std::vector<JsonValue> games, moved;
    for (const AddedGame& game : record.games) {
        games.push_back(JsonObject({{"id", JsonString(game.id)}, {"name", JsonString(game.name)},
                                    {"system", JsonString(game.system)}}));
    }
    for (const auto& [from, to] : record.moved) {
        moved.push_back(JsonArray({JsonString(from), JsonString(to)}));
    }
    return JsonObject({{"id", JsonString(record.id)},
                       {"time", JsonNumber(static_cast<double>(record.time))},
                       {"outcome", JsonString(OutcomeName(record.outcome))},
                       {"download", JsonString(record.download)},
                       {"message", JsonString(record.message)},
                       {"system_name", JsonString(record.system_name)},
                       {"games", JsonArray(std::move(games))},
                       {"moved", JsonArray(std::move(moved))},
                       {"hacks", Strings(record.hacks)},
                       {"folders", Strings(record.folders)},
                       {"replaced", JsonBool(record.replaced)},
                       {"undone", JsonBool(record.undone)}});
}

Record FromJson(const JsonValue& value) {
    Record record;
    record.id = value.String("id");
    if (const JsonValue* time = value.Find("time")) {
        record.time = static_cast<std::int64_t>(time->number);
    }
    record.outcome = OutcomeFrom(value.String("outcome"));
    record.download = value.String("download");
    record.message = value.String("message");
    record.system_name = value.String("system_name");
    if (const JsonValue* games = value.Find("games")) {
        for (const JsonValue& game : games->items) {
            record.games.push_back({game.String("id"), game.String("name"), game.String("system")});
        }
    }
    if (const JsonValue* moved = value.Find("moved")) {
        for (const JsonValue& pair : moved->items) {
            if (pair.items.size() == 2) {
                record.moved.emplace_back(pair.items[0].string, pair.items[1].string);
            }
        }
    }
    record.hacks = StringsOf(value.Find("hacks"));
    record.folders = StringsOf(value.Find("folders"));
    const JsonValue* replaced = value.Find("replaced");
    const JsonValue* undone = value.Find("undone");
    record.replaced = replaced != nullptr && replaced->boolean;
    record.undone = undone != nullptr && undone->boolean;
    return record;
}

std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

const char* OutcomeName(Outcome outcome) {
    return NAMES[static_cast<std::size_t>(outcome)];
}

Setup::Setup(Library& library, fs::path downloads_dir, fs::path data_dir)
    : library_(library), downloads_(std::move(downloads_dir)), history_file_(data_dir / "library" / "downloads.json"),
      data_dir_(std::move(data_dir)) {
    std::error_code ec;
    fs::create_directories(downloads_, ec);
    const auto text = ReadTextFile(history_file_);
    const auto json = text ? ParseJson(*text) : std::nullopt;
    if (json && json->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : json->items) {
            history_.push_back(FromJson(item));
        }
    }
}

void Setup::Save() const {
    std::vector<JsonValue> items;
    for (const Record& record : history_) {
        items.push_back(ToJson(record));
    }
    std::error_code ec;
    fs::create_directories(history_file_.parent_path(), ec);
    WriteFileAtomic(history_file_, WriteJson(JsonArray(std::move(items))));
}

Outcomes Setup::Run(const Request& request, const std::function<bool()>& cancelled) {
    std::lock_guard run(running_);
    SetupWork work{*this, request, cancelled, library_.Consoles(), {}, {}};
    Record& record = work.out.record;
    record.time = Now();
    record.download = PathToUtf8(request.file.filename());
    {
        std::lock_guard lock(mutex_);
        const auto sequence = static_cast<unsigned long long>(history_.size());
        record.id = std::format("{}-{}", record.time, sequence);
        while (std::any_of(history_.begin(), history_.end(), [&](const Record& r) { return r.id == record.id; })) {
            record.id += "x";
        }
    }
    work.Run();
    std::lock_guard lock(mutex_);
    // A question or a wait about the same file is answered by this one.
    std::erase_if(history_, [&](const Record& old) {
        return (old.outcome == Outcome::NeedsBase || old.outcome == Outcome::Waiting) && old.download == record.download;
    });
    history_.insert(history_.begin(), record);
    if (history_.size() > KEPT) {
        history_.resize(KEPT);
    }
    Save();
    return std::move(work.out);
}

std::vector<Record> Setup::History() const {
    std::lock_guard lock(mutex_);
    return history_;
}

Status Setup::Undo(const std::string& record_id) {
    std::lock_guard run(running_);
    Record record;
    {
        std::lock_guard lock(mutex_);
        const auto found = std::find_if(history_.begin(), history_.end(), [&](const Record& r) { return r.id == record_id; });
        if (found == history_.end()) {
            return Status::Fail("There is no such download");
        }
        if (found->undone) {
            return Status::Fail("That was taken back already");
        }
        if (found->outcome != Outcome::Added && found->outcome != Outcome::HackAdded) {
            return Status::Fail("Nothing was set up for that download");
        }
        record = *found;
    }
    // Files go back first: a hack downloaded patched already is among them, and Remove then
    // only takes its entry and what lies beside it.
    for (const auto& [name, placed] : record.moved) {
        const fs::path file = PathFromUtf8(placed);
        if (IsRegularFile(file) && !MoveTo(file, FreeName(downloads_, PathFromUtf8(name).filename()))) {
            return Status::Fail("Could not move " + PathToUtf8(file.filename()) + " back to Downloads");
        }
    }
    for (const std::string& id : record.hacks) {
        if (Status removed = HacksOf(library_).Remove(id, {}); !removed && library_.Find(id)) {
            return removed;
        }
    }
    for (auto it = record.folders.rbegin(); it != record.folders.rend(); ++it) {
        std::error_code ec;
        if (fs::is_empty(PathFromUtf8(*it), ec) && !ec) {
            fs::remove(PathFromUtf8(*it), ec);
        }
    }
    if (!record.moved.empty()) {
        library_.Scan();
    }
    std::lock_guard lock(mutex_);
    for (Record& kept : history_) {
        if (kept.id == record_id) {
            kept.undone = true;
        }
    }
    Save();
    return {};
}

std::shared_ptr<Setup> SharedSetup(Library& library, const fs::path& data_dir) {
    static std::mutex mutex;
    static std::map<std::pair<Library*, std::string>, std::weak_ptr<Setup>> setups;
    std::lock_guard lock(mutex);
    std::weak_ptr<Setup>& slot = setups[{&library, PathToUtf8(data_dir)}];
    std::shared_ptr<Setup> setup = slot.lock();
    if (!setup) {
        setup = std::make_shared<Setup>(library, data_dir / "downloads", data_dir);
        slot = setup;
    }
    return setup;
}

} // namespace den::library::downloads
