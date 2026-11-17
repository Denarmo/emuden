// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.mods.apply, state and remove: a plugin's files copied into a game's folder. The record
// (<root>/<appid>/record.json) is written before the first copy and names, for each file,
// the SHA-256 the app wrote and the original's; the originals are in originals/ beside it.

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/sha256.h"
#include "feature/mods.h"

namespace den::plugins::feature::mods {

namespace {

struct RecordFile {
    std::string to, written;
    std::optional<std::string> original; // the replaced file's SHA-256; none: the file is new
    std::string keep;                    // where the original is also kept in the game's folder, or empty
};

// The file beside `to` with that name.
std::string Beside(const std::string& to, const std::string& name) {
    const std::size_t slash = to.rfind('/');
    return slash == std::string::npos ? name : to.substr(0, slash + 1) + name;
}

struct Record {
    std::string appid, name;
    std::int64_t time = 0;
    bool done = false; // every copy was made
    fs::path dir;      // the game's folder at the time
    std::vector<std::string> made; // folders the apply created, outermost first
    std::vector<RecordFile> files;
};

fs::path RecordPath(const fs::path& home) {
    return home / "record.json";
}

fs::path OriginalPath(const fs::path& home, std::size_t index) {
    return home / "originals" / std::to_string(index);
}

bool Write(const fs::path& home, const Record& record) {
    JsonValue files = JsonArray();
    for (const RecordFile& file : record.files) {
        files.items.push_back(JsonObject({{"to", JsonString(file.to)},
                                          {"written", JsonString(file.written)},
                                          {"original", file.original ? JsonString(*file.original) : JsonValue()},
                                          {"keep", file.keep.empty() ? JsonValue() : JsonString(file.keep)}}));
    }
    JsonValue made = JsonArray();
    for (const std::string& folder : record.made) {
        made.items.push_back(JsonString(folder));
    }
    const JsonValue root = JsonObject({{"version", JsonNumber(1)},
                                       {"appid", JsonString(record.appid)},
                                       {"name", JsonString(record.name)},
                                       {"time", JsonNumber(static_cast<double>(record.time))},
                                       {"done", JsonBool(record.done)},
                                       {"dir", JsonString(PathToUtf8(record.dir))},
                                       {"made", std::move(made)},
                                       {"files", std::move(files)}});
    return WriteFileAtomic(RecordPath(home), WriteJson(root));
}

std::optional<Record> Read(const fs::path& home) {
    const auto text = ReadTextFile(RecordPath(home));
    const auto root = text ? ParseJson(*text) : std::nullopt;
    const JsonValue* files = root ? root->Find("files") : nullptr;
    if (files == nullptr || files->kind != JsonValue::Kind::Array) {
        return std::nullopt;
    }
    Record record;
    record.appid = root->String("appid");
    record.name = root->String("name");
    record.dir = PathFromUtf8(root->String("dir"));
    if (const JsonValue* time = root->Find("time"); time != nullptr && time->kind == JsonValue::Kind::Number) {
        record.time = static_cast<std::int64_t>(time->number);
    }
    const JsonValue* done = root->Find("done");
    record.done = done != nullptr && done->kind == JsonValue::Kind::Bool && done->boolean;
    if (const JsonValue* made = root->Find("made"); made != nullptr) {
        for (const JsonValue& folder : made->items) {
            record.made.push_back(folder.string);
        }
    }
    for (const JsonValue& item : files->items) {
        RecordFile file{item.String("to"), item.String("written"), std::nullopt, item.String("keep")};
        if (const JsonValue* original = item.Find("original");
            original != nullptr && original->kind == JsonValue::Kind::String) {
            file.original = original->string;
        }
        record.files.push_back(std::move(file));
    }
    return record;
}

// The SHA-256 of what is at `path` now; nothing when no file is there.
std::optional<std::string> HashNow(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::symlink_status(path, ec)) ? Sha256File(path) : std::nullopt;
}

bool Copy(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

} // namespace

Result<std::vector<Planned>> Store::Plan(const std::string& appid, const fs::path& game_dir,
                                         const std::vector<Order>& files) const {
    if (!IsAppId(appid)) {
        return Status::Fail("that is not a Steam app id");
    }
    if (files.empty() || files.size() > MAX_FILES) {
        return Status::Fail("apply takes 1 to 64 files");
    }
    const std::optional<Record> earlier = Read(root_ / appid);
    std::vector<Planned> plan;
    std::uint64_t total = 0;
    for (const Order& order : files) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::symlink_status(order.from, ec))) {
            return Status::Fail("there is no file to copy to " + order.to);
        }
        Result<fs::path> target = Target(game_dir, order.to);
        if (!target) {
            return target.status();
        }
        // A file system that ignores case would take two of these for one file.
        const auto taken = [&](const std::string& path) {
            return std::any_of(plan.begin(), plan.end(), [&](const Planned& p) {
                return EqualsNoCase(p.to, path) || (!p.keep.empty() && EqualsNoCase(p.keep, path));
            });
        };
        if (taken(order.to)) {
            return Status::Fail(order.to + " is named twice");
        }
        Planned planned{order.to, fs::exists(fs::symlink_status(target.value(), ec)), fs::file_size(order.from, ec), {}};
        if (earlier) {
            // What the earlier apply added goes away before this one is made.
            const auto was = std::find_if(earlier->files.begin(), earlier->files.end(),
                                          [&](const RecordFile& f) { return f.to == order.to; });
            if (was != earlier->files.end() && !was->original && HashNow(target.value()) == was->written) {
                planned.replaces = false;
            }
        }
        if (!order.keep_as.empty()) {
            if (!IsPlainName(order.keep_as)) {
                return Status::Fail("keep_as is a plain file name");
            }
            if (!planned.replaces) {
                return Status::Fail(order.to + " is not in the game: there is nothing to keep as " + order.keep_as);
            }
            planned.keep = Beside(order.to, order.keep_as);
            const Result<fs::path> kept = Target(game_dir, planned.keep);
            if (!kept) {
                return kept.status();
            }
            if (taken(planned.keep) || EqualsNoCase(planned.keep, order.to)) {
                return Status::Fail(planned.keep + " is named twice");
            }
            // Only a copy an earlier apply made may be in its place.
            const std::optional<std::string> there = HashNow(kept.value());
            const bool ours = there && earlier &&
                              std::any_of(earlier->files.begin(), earlier->files.end(), [&](const RecordFile& f) {
                                  return f.keep == planned.keep && f.original == there;
                              });
            if (fs::exists(fs::symlink_status(kept.value(), ec)) && !ours) {
                return Status::Fail(planned.keep + " is already in the game");
            }
        }
        total += planned.size;
        plan.push_back(std::move(planned));
    }
    if (total > MAX_APPLY) {
        return Status::Fail("the files are more than 1 GiB");
    }
    return plan;
}

Status Store::Apply(const std::string& appid, const std::string& name, const fs::path& game_dir,
                    const std::vector<Order>& files, std::int64_t time, const std::function<bool()>& cut) {
    if (!IsAppId(appid)) {
        return Status::Fail("that is not a Steam app id");
    }
    const fs::path home = root_ / appid;
    if (Read(home)) {
        if (const Result<std::vector<std::string>> undone = Remove(appid); !undone) {
            return Status::Fail("the earlier setup could not be undone: " + undone.error());
        }
    }
    const Result<std::vector<Planned>> plan = Plan(appid, game_dir, files);
    if (!plan) {
        return plan.status();
    }
    std::error_code ec;
    Record record;
    record.appid = appid;
    record.name = name;
    record.time = time;
    record.dir = fs::canonical(game_dir, ec);
    std::vector<fs::path> targets;
    for (const Order& order : files) {
        Result<fs::path> target = Target(game_dir, order.to);
        const auto written = target ? Sha256File(order.from) : std::nullopt;
        if (!written) {
            return Status::Fail(order.to + " could not be read");
        }
        std::optional<std::string> original = HashNow(target.value());
        if (!original && fs::exists(fs::symlink_status(target.value(), ec))) {
            return Status::Fail(order.to + " of the game could not be read");
        }
        record.files.push_back({order.to, *written, std::move(original),
                                order.keep_as.empty() ? std::string() : Beside(order.to, order.keep_as)});
        targets.push_back(target.take());
        // The folders on the way that are not there yet.
        std::string folder;
        for (std::size_t slash = order.to.find('/'); slash != std::string::npos; slash = order.to.find('/', slash + 1)) {
            folder = order.to.substr(0, slash);
            const bool listed = std::find(record.made.begin(), record.made.end(), folder) != record.made.end();
            if (!listed && !fs::exists(record.dir / PathFromUtf8(folder), ec)) {
                record.made.push_back(folder);
            }
        }
    }
    fs::remove_all(home, ec);
    fs::create_directories(home / "originals", ec);
    if (ec || !Write(home, record)) {
        fs::remove_all(home, ec);
        return Status::Fail("the record of the setup could not be written");
    }
    const auto failed = [&](const std::string& what) {
        const Result<std::vector<std::string>> undone = Remove(appid);
        return Status::Fail(what + (undone ? "; nothing was changed" : "; undoing it failed: " + undone.error()));
    };
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (cut && cut()) {
            return Status::Fail("cancelled");
        }
        if (record.files[i].original && !Copy(targets[i], OriginalPath(home, i))) {
            return failed("the original of " + files[i].to + " could not be kept");
        }
        if (!record.files[i].keep.empty() && !Copy(targets[i], record.dir / PathFromUtf8(record.files[i].keep))) {
            return failed(record.files[i].keep + " could not be written");
        }
        fs::create_directories(targets[i].parent_path(), ec);
        if (!Copy(files[i].from, targets[i])) {
            return failed(files[i].to + " could not be written (is the game running?)");
        }
    }
    record.done = true;
    if (!Write(home, record)) {
        return failed("the record of the setup could not be written");
    }
    return {};
}

std::optional<Applied> Store::State(const std::string& appid) const {
    const std::optional<Record> record = IsAppId(appid) ? Read(root_ / appid) : std::nullopt;
    if (!record) {
        return std::nullopt;
    }
    Applied applied{record->name, record->time, record->done, {}};
    for (const RecordFile& file : record->files) {
        const Result<fs::path> target = Target(record->dir, file.to);
        applied.files.push_back({file.to, target && HashNow(target.value()) == file.written});
    }
    return applied;
}

std::optional<std::string> Store::Name(const std::string& appid) const {
    const std::optional<Record> record = IsAppId(appid) ? Read(root_ / appid) : std::nullopt;
    return record ? std::optional<std::string>(record->name) : std::nullopt;
}

Result<std::vector<std::string>> Store::Remove(const std::string& appid) {
    std::vector<std::string> left;
    if (!IsAppId(appid)) {
        return Status::Fail("that is not a Steam app id");
    }
    const fs::path home = root_ / appid;
    const std::optional<Record> record = Read(home);
    std::error_code ec;
    if (!record) {
        return left;
    }
    const bool gone = !fs::is_directory(record->dir, ec); // the game was uninstalled or moved
    std::string failure;
    for (std::size_t i = 0; i < record->files.size() && !gone; ++i) {
        const RecordFile& file = record->files[i];
        // The copy kept beside it goes when it is still the one the app made.
        if (const Result<fs::path> kept = Target(record->dir, file.keep); !file.keep.empty() && kept) {
            const std::optional<std::string> there = HashNow(kept.value());
            if (there && (there == file.original || !record->done)) {
                fs::remove(kept.value(), ec);
                if (ec) {
                    failure = file.keep + " could not be deleted (is the game running?)";
                }
            } else if (there) {
                left.push_back(file.keep);
            }
        }
        const Result<fs::path> target = Target(record->dir, file.to);
        if (!target) {
            left.push_back(file.to); // a link or a folder is in its place now
            continue;
        }
        const std::optional<std::string> now = HashNow(target.value());
        if (now == file.original) {
            continue; // never copied, or already put back
        }
        const bool ours = now && *now == file.written;
        // Once the copies were all made, a file that is no longer ours is the game's (an
        // update); in an apply cut short it may be half a copy.
        if (!ours && record->done) {
            left.push_back(file.to);
            continue;
        }
        if (!file.original) {
            fs::remove(target.value(), ec);
            if (ec) {
                failure = file.to + " could not be deleted (is the game running?)";
            }
        } else if (Sha256File(OriginalPath(home, i)) != file.original) {
            failure = "the kept original of " + file.to + " is damaged";
        } else if (!Copy(OriginalPath(home, i), target.value())) {
            failure = file.to + " could not be put back (is the game running?)";
        }
    }
    if (!failure.empty()) {
        return Status::Fail(failure);
    }
    for (auto folder = record->made.rbegin(); folder != record->made.rend() && !gone; ++folder) {
        if (IsInnerPath(*folder)) {
            fs::remove(record->dir / PathFromUtf8(*folder), ec); // only when empty
        }
    }
    fs::remove_all(home, ec);
    return left;
}

std::vector<std::string> Store::Games() const {
    std::vector<std::string> out;
    for (const fs::path& home : ListDirectory(root_)) {
        const std::string appid = PathToUtf8(home.filename());
        if (IsAppId(appid) && IsRegularFile(RecordPath(home))) {
            out.push_back(appid);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

Status Store::RemoveAll() {
    Status result;
    for (const std::string& appid : Games()) {
        const Result<std::vector<std::string>> removed = Remove(appid);
        if (!removed) {
            result.notes.push_back("Steam app " + appid + ": " + removed.error());
            continue;
        }
        for (const std::string& file : removed.value()) {
            result.notes.push_back("Steam app " + appid + ": " + file + " was changed since and left alone");
        }
    }
    for (const std::string& layer : Layers()) {
        if (const Status removed = RemoveLayer(layer); !removed.ok) {
            result.notes.push_back(layer + ": " + removed.error);
        }
    }
    return result;
}

} // namespace den::plugins::feature::mods
