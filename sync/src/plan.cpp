// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The journal and the plan: what changed on which side since both last agreed.

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/hash.h"
#include "den/sync/plan.h"

namespace den::sync {

namespace fs = std::filesystem;

Journal::Journal(fs::path file) : file_(std::move(file)) {
    const auto text = ReadTextFile(file_);
    const auto root = text ? ParseJson(*text) : std::nullopt;
    if (!root || root->kind != JsonValue::Kind::Object) {
        return;
    }
    if (const JsonValue* files = root->Find("files"); files && files->kind == JsonValue::Kind::Object) {
        for (const auto& [path, value] : files->members) {
            JournalEntry entry;
            entry.hash = value.String("hash");
            entry.rev = value.String("rev");
            if (const JsonValue* size = value.Find("size")) {
                entry.size = static_cast<std::uint64_t>(size->number);
            }
            if (const JsonValue* synced = value.Find("synced")) {
                entry.synced = static_cast<std::int64_t>(synced->number);
            }
            entries[path] = std::move(entry);
        }
    }
    if (const JsonValue* list = root->Find("deleted"); list && list->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : list->items) {
            deleted.insert(item.string);
        }
    }
}

bool Journal::Save() const {
    JsonValue files = JsonObject();
    for (const auto& [path, entry] : entries) {
        files.members.emplace_back(path, JsonObject({
                                             {"hash", JsonString(entry.hash)},
                                             {"rev", JsonString(entry.rev)},
                                             {"size", JsonNumber(static_cast<double>(entry.size))},
                                             {"synced", JsonNumber(static_cast<double>(entry.synced))},
                                         }));
    }
    JsonValue list = JsonArray();
    for (const std::string& path : deleted) {
        list.items.push_back(JsonString(path));
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    return WriteFileAtomic(file_, WriteJson(JsonObject({{"version", JsonNumber(1)},
                                                        {"files", std::move(files)},
                                                        {"deleted", std::move(list)}})));
}

std::string GameHash(std::uint64_t size) {
    return "size:" + std::to_string(size);
}

namespace {

// Games are a library that is uploaded once, not synced: a game the remote has (or that came
// from it, or went up before) never goes up again, and nothing deletes one on either side.
void PlanGame(const PlanRequest& request, const std::string& path, const LocalItem* local,
              const RemoteFile* remote, const JournalEntry* journal, bool deleted, std::vector<Action>& out) {
    if (local && remote) {
        if (!journal && local->size == remote->size) {
            out.push_back({Op::Adopt, path});
        }
    } else if (local) {
        if (!journal && request.upload_games) {
            out.push_back({Op::Upload, path});
        }
    } else if (journal || deleted) {
        out.push_back({Op::Forget, path}); // gone here: the remote copy stays
    }
}

// Play time files first: they are small and name the other devices for conflict copies.
int Rank(const std::string& path) {
    const std::string category = CategoryOf(path);
    if (path.starts_with("config/playtime/")) {
        return -1;
    }
    if (category == SAVES) {
        return 0;
    }
    if (category == STATES) {
        return 1;
    }
    if (category == CONFIG) {
        return 2;
    }
    return category == GAMES ? 4 : 3;
}

} // namespace

std::vector<Action> MakePlan(const PlanRequest& request) {
    std::set<std::string> paths;
    for (const auto& [path, item] : *request.local) {
        paths.insert(path);
    }
    for (const auto& [path, file] : *request.remote) {
        paths.insert(path);
    }
    for (const auto& [path, entry] : request.journal->entries) {
        paths.insert(path);
    }
    paths.insert(request.journal->deleted.begin(), request.journal->deleted.end());

    std::vector<Action> out;
    for (const std::string& path : paths) {
        const std::string category = CategoryOf(path);
        if (!request.categories.contains(category) || !IsPortablePath(path)) {
            continue;
        }
        const auto l = request.local->find(path);
        const auto r = request.remote->find(path);
        const auto j = request.journal->entries.find(path);
        const LocalItem* local = l != request.local->end() ? &l->second : nullptr;
        const RemoteFile* remote = r != request.remote->end() ? &r->second : nullptr;
        const JournalEntry* journal = j != request.journal->entries.end() ? &j->second : nullptr;
        const bool deleted = request.journal->deleted.contains(path);
        if (category == GAMES) {
            PlanGame(request, path, local, remote, journal, deleted, out);
            continue;
        }
        if (local && remote) {
            if (!journal) {
                out.push_back({Op::Compare, path});
                continue;
            }
            const bool local_changed = local->hash != journal->hash;
            const bool remote_changed = remote->rev != journal->rev;
            if (local_changed && remote_changed) {
                out.push_back({Op::Conflict, path});
            } else if (local_changed) {
                out.push_back({Op::Upload, path});
            } else if (remote_changed) {
                out.push_back({Op::Download, path});
            }
        } else if (local) {
            // New here, or missing remotely although it was synced: it goes up (again).
            out.push_back({Op::Upload, path});
        } else if (remote) {
            if (deleted && journal && remote->rev == journal->rev) {
                out.push_back({Op::DeleteRemote, path});
            } else {
                // New there, or gone here without the user deleting it in EmuDEN.
                out.push_back({Op::Download, path});
            }
        } else if (journal || deleted) {
            out.push_back({Op::Forget, path});
        }
    }
    // Saves first, games last: a game upload can take hours.
    std::stable_sort(out.begin(), out.end(), [](const Action& a, const Action& b) {
        return Rank(a.path) < Rank(b.path);
    });
    return out;
}

void AddClearedGameLayers(Catalog& local, const Journal& journal, const GameIndex& games) {
    constexpr std::string_view PREFIX = "config/games/";
    const std::string empty = WriteJson(JsonObject());
    for (const auto& [path, entry] : journal.entries) {
        if (!path.starts_with(PREFIX) || !path.ends_with(".json") || local.contains(path)) {
            continue;
        }
        const std::string name = path.substr(path.rfind('/') + 1);
        const std::string system = SystemOf(path);
        if (games.Never(std::string(CONFIG) + "/" + system) ||
            games.Find(system, name.substr(0, name.size() - 5)) == nullptr) {
            continue;
        }
        LocalItem item;
        item.path = path;
        item.text = empty;
        item.size = empty.size();
        item.hash = Sha256Hex(empty.data(), empty.size());
        local[path] = std::move(item);
    }
}

Op AfterFetch(const std::string& local_hash, const std::string& remote_hash, const JournalEntry* journal) {
    if (!remote_hash.empty() && remote_hash == local_hash) {
        return Op::Adopt;
    }
    if (journal != nullptr && !remote_hash.empty() && remote_hash == journal->hash) {
        return Op::Upload;
    }
    return Op::Conflict;
}

} // namespace den::sync
