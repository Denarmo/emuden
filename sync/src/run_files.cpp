// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A run's transfers: uploads, downloads that replace a file atomically (or apply a settings
// file), and files changed on both sides, which are compared by content before anything is
// called a conflict.

#include <system_error>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/log.h"
#include "den/base/sha256.h"
#include "run.h"

namespace den::sync {

namespace fs = std::filesystem;

namespace {

std::string TextHash(const std::string& text) {
    return Sha256Hex(text.data(), text.size());
}

} // namespace

void Run::Error(const std::string& message) {
    log::Warn("sync", "{}", message);
    Change([&](SyncState& s) {
        if (s.errors.size() < 20) {
            s.errors.push_back(message);
        }
    });
}

void Run::Current(const std::string& path, std::uint64_t size) {
    Change([&](SyncState& s) {
        s.current = path;
        s.fraction = -1;
        s.size = size;
        if (transfer_ != nullptr) {
            transfer_->current = path;
            transfer_->fraction = -1;
            transfer_->size = size;
        }
    });
}

void Run::Fraction(double done, double total) {
    Change([&](SyncState& s) {
        s.fraction = total > 0 ? static_cast<float>(done / total) : done <= 1 ? static_cast<float>(done) : -1;
        if (total > 1) {
            s.size = static_cast<std::uint64_t>(total);
        }
        if (transfer_ != nullptr) {
            transfer_->fraction = s.fraction;
            transfer_->size = s.size;
        }
    });
}

std::string Run::GameOf(const std::string& path) const {
    if (const auto item = local_.find(path); item != local_.end()) {
        return item->second.game_id;
    }
    const auto target = TargetOf(path, games_, engine_.config_.data_dir, games_folder_);
    return target ? target->game_id : std::string();
}

fs::path Run::Temp(const std::string& path) {
    return temp_dir_ / (std::to_string(++temp_) + "-" + SafeFileName(path.substr(path.rfind('/') + 1)));
}

const RemoteFile* Run::RemoteOf(const std::string& path) const {
    const auto found = remote_->find(path);
    return found != remote_->end() ? &found->second : nullptr;
}

void Run::Agree(const std::string& path, const std::string& hash, const std::string& rev, std::uint64_t size) {
    journal_->entries[path] = {hash, rev, size, engine_.Now()};
}

fs::path Run::Fetch(const std::string& path) {
    const fs::path temp = Temp(path);
    const Status got = provider_.Get(path, temp, [this](double done, double total) { Fraction(done, total); });
    if (!got.ok) {
        std::error_code ec;
        fs::remove(temp, ec);
        Error(path + ": " + got.error);
        return {};
    }
    return temp;
}

void Run::Do(const Action& action) {
    const std::string& path = action.path;
    const LocalItem* local = local_.contains(path) ? &local_.at(path) : nullptr;
    const RemoteFile* remote = RemoteOf(path);
    switch (action.op) {
    case Op::Upload:
        Upload(*local, remote ? (journal_->entries.contains(path) ? journal_->entries[path].rev : remote->rev)
                              : std::string());
        break;
    case Op::Download:
    case Op::Compare:
    case Op::Conflict: {
        const fs::path temp = Fetch(path);
        if (temp.empty()) {
            break;
        }
        if (action.op == Op::Download) {
            Apply(path, temp, *remote);
        } else {
            const std::string hash = Sha256File(temp).value_or("");
            const auto agreed = journal_->entries.find(path);
            const JournalEntry* journal = agreed != journal_->entries.end() ? &agreed->second : nullptr;
            if (local && hash == local->hash) {
                Agree(path, hash, remote->rev, local->size);
            } else if (CategoryOf(path) == CONFIG) {
                // Settings are merged, never a conflict copy. Changed on both sides: key by
                // key against the version both last agreed on (what changed here wins).
                // Met for the first time: the remote values are applied. Then what this
                // device has besides goes up.
                if (action.op == Op::Conflict && local) {
                    const std::string merged = MergeSettings(ReadTextFile(BaseOf(path)).value_or(""), local->text,
                                                             ReadTextFile(temp).value_or(""));
                    if (!merged.empty()) {
                        WriteFileAtomic(temp, merged);
                    }
                }
                if (Apply(path, temp, *remote)) {
                    for (LocalItem& item : config_->Export(games_)) {
                        if (item.path == path && TextHash(item.text) != hash) {
                            item.hash = TextHash(item.text);
                            item.size = item.text.size();
                            Upload(item, remote->rev);
                        }
                    }
                }
            } else if (local && AfterFetch(local->hash, hash, journal) == Op::Upload) {
                // Only its rev moved online: this device's change goes up, no conflict.
                Upload(*local, remote->rev);
            } else if (local) {
                Resolve(*local, *remote, temp);
            }
        }
        std::error_code ec;
        fs::remove(temp, ec);
        break;
    }
    case Op::Adopt:
        Agree(path, local->hash, remote->rev, local->size);
        break;
    case Op::DeleteRemote:
        if (const Status deleted = provider_.Delete(path); !deleted.ok) {
            Error(path + ": " + deleted.error);
            break;
        }
        journal_->entries.erase(path);
        journal_->deleted.erase(path);
        break;
    case Op::Forget:
        journal_->entries.erase(path);
        journal_->deleted.erase(path);
        break;
    }
}

bool Run::Upload(const LocalItem& item, const std::string& rev) {
    fs::path file = item.file;
    if (file.empty()) {
        file = Temp(item.path);
        WriteFileAtomic(file, item.text);
    }
    Result<std::string> put = provider_.Put(item.path, file, rev, item.modified,
                                            [this](double done, double total) { Fraction(done, total); });
    if (item.file.empty()) {
        std::error_code ec;
        fs::remove(file, ec);
    }
    if (!put && put.error() == "cancelled" && (Cancelled() || engine_.Stopping())) {
        return false; // it goes on in the next run
    }
    if (!put) {
        Error(item.path + ": " + (put.error() == "conflict" ? "changed on another device; it is sorted out "
                                                              "on the next sync"
                                                            : put.error()));
        return false;
    }
    Agree(item.path, item.hash, put.value(), item.size);
    if (CategoryOf(item.path) == CONFIG) {
        KeepBase(item.path, item.text);
    }
    Change([](SyncState& s) { ++s.uploaded; });
    return true;
}

// The last version of a settings file both sides agreed on, for merging.
fs::path Run::BaseOf(const std::string& path) const {
    return sync_dir_ / "base" / PathFromUtf8(path);
}

void Run::KeepBase(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(BaseOf(path).parent_path(), ec);
    WriteFileAtomic(BaseOf(path), text);
}

bool Run::Guarded(const std::function<void()>& write) {
    if (gate_ == nullptr) {
        write();
        return true;
    }
    return gate_->Write(write);
}

// A downloaded file takes the place of the local one (or is applied, for settings).
bool Run::Apply(const std::string& path, const fs::path& temp, const RemoteFile& remote) {
    const std::string hash = Sha256File(temp).value_or("");
    if (CategoryOf(path) == CONFIG) {
        const std::string text = ReadTextFile(temp).value_or("");
        if (const Status applied = config_->Import(path, text, games_); !applied.ok) {
            Error(path + ": " + applied.error);
            return false;
        }
        // What both sides agree on is what this device now exports for it.
        std::string agreed = TextHash(text);
        for (const LocalItem& item : config_->Export(games_)) {
            if (item.path == path) {
                agreed = TextHash(item.text);
            }
        }
        Agree(path, agreed, remote.rev, remote.size);
        KeepBase(path, text);
    } else {
        const auto target = local_.contains(path) ? std::optional<LocalTarget>(LocalTarget{local_.at(path).file, {}})
                                                  : TargetOf(path, games_, engine_.config_.data_dir, games_folder_);
        if (!target || target->file.empty()) {
            return false; // this device does not have the game
        }
        std::error_code ec;
        const bool written = Guarded([&] {
            fs::create_directories(target->file.parent_path(), ec);
            fs::path part = target->file;
            part += ".sync-part";
            fs::copy_file(temp, part, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                SetFileTime(part, remote.modified);
                fs::rename(part, target->file, ec);
            }
            if (ec) {
                fs::remove(part, ec);
            }
        });
        if (!written) {
            return false; // the game starts: it is left for the next sync
        }
        if (ec) {
            Error(path + ": could not be written");
            return false;
        }
        Agree(path, hash, remote.rev, remote.size);
    }
    Change([](SyncState& s) { ++s.downloaded; });
    return true;
}

// Both sides changed: the newer file wins, the other one is kept as a conflict copy.
void Run::Resolve(const LocalItem& local, const RemoteFile& remote, const fs::path& temp) {
    std::error_code ec;
    if (local.modified >= remote.modified) {
        const fs::path loser = Temp(local.path);
        fs::copy_file(temp, loser, fs::copy_options::overwrite_existing, ec);
        engine_.KeepLoser(local.path, loser, engine_.OtherDevice(), local.game_id);
        Upload(local, remote.rev);
    } else {
        const fs::path loser = Temp(local.path);
        if (local.file.empty()) {
            WriteFileAtomic(loser, local.text);
        } else {
            fs::copy_file(local.file, loser, fs::copy_options::overwrite_existing, ec);
        }
        engine_.KeepLoser(local.path, loser, engine_.config_.device, local.game_id);
        Apply(local.path, temp, remote);
    }
    Change([](SyncState& s) { ++s.conflicts; });
}

} // namespace den::sync
