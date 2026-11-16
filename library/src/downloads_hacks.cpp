// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A downloaded patch: the game it is for (by the checksum it carries, else by name, else the
// user's choice), a patched copy of it in the library named after the hack. The same hack
// again (a newer version) replaces the copy and keeps its entry, so its saves stay.

#include <algorithm>
#include <format>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/library/hacks.h"
#include "downloads_work.h"

namespace den::library::downloads {

namespace fs = std::filesystem;

namespace {

std::string ConsoleName(const SystemTable& systems, const std::string& id) {
    const SystemDef* def = systems.Find(id);
    return def != nullptr ? def->name : id;
}

} // namespace

void SetupWork::SetUpPatch(const std::vector<fs::path>& patches) {
    Record& record = out.record;
    Library& library = setup.library_;
    const fs::path work = setup.downloads_ / ".unpacked-patch";
    std::optional<PatchFile> chosen;
    std::string first_error;
    for (const fs::path& patch : patches) {
        Result<PatchFile> opened = OpenPatchFile(library, patch, work);
        if (!opened) {
            first_error = first_error.empty() ? opened.error() : first_error;
            continue;
        }
        const bool has_base = opened.value().base.has_value();
        if (!chosen || (has_base && !chosen->base)) {
            chosen = opened.take();
        }
        if (has_base) {
            break;
        }
    }
    const auto cleanup = [&] {
        std::error_code ec;
        fs::remove_all(work, ec);
    };
    if (!chosen) {
        record.outcome = Outcome::Failed;
        record.message = record.download + " was not added: " +
                         (first_error.empty() ? "the patch cannot be read" : first_error) + ". It is in EmuDEN's Downloads folder";
        cleanup();
        return;
    }
    const std::string patch_name = PathToUtf8(chosen->patch.filename());
    std::string title = HackTitle(request.page_title, patch_name);
    if (title == "Hack" && patch_name != record.download) {
        title = HackTitle(request.page_title, record.download);
    }
    const std::string version = [&] {
        const std::string inner = HackVersion(request.page_title, patch_name);
        return !inner.empty() ? inner : HackVersion({}, record.download);
    }();

    std::optional<Entry> base;
    if (!request.base_id.empty()) {
        base = library.Find(request.base_id);
        if (!base) {
            record.outcome = Outcome::Failed;
            record.message = "That game is not in your library any more";
            cleanup();
            return;
        }
    } else if (chosen->base) {
        base = chosen->base->entry;
    } else if (chosen->info.source_crc32) {
        record.outcome = Outcome::MissingBase;
        record.message = title + " needs a game you don't have: " +
                         (chosen->missing.empty() ? "CRC32 " + patching::CrcText(*chosen->info.source_crc32) : chosen->missing) +
                         ". The patch stays in Downloads";
        cleanup();
        return;
    } else {
        // No checksum: the game whose name fits the hack's, when one fits better than the rest.
        std::vector<std::pair<int, Entry>> scored;
        const std::string text = title + " " + patch_name + " " + record.download + " " + request.page_title;
        for (const Entry& entry : chosen->choices) {
            if (entry.hack_of.empty()) {
                scored.emplace_back(NameScore(entry.name, text), entry);
            }
        }
        std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (!scored.empty() && scored.front().first > 0 && (scored.size() == 1 || scored[1].first < scored.front().first)) {
            base = scored.front().second;
        } else {
            for (auto& [score, entry] : scored) {
                out.choices.push_back(std::move(entry));
            }
            record.outcome = out.choices.empty() ? Outcome::MissingBase : Outcome::NeedsBase;
            record.message = out.choices.empty()
                                 ? title + ": none of your games can take this patch. It stays in Downloads"
                                 : "Which game is " + title + " for?";
            record.system_name = title; // shown with the question
            cleanup();
            return;
        }
    }

    Hacks& hacks = HacksOf(library);
    const std::vector<Hack> before = hacks.List();
    HackRequest order;
    order.base_id = base->id;
    order.patch = chosen->patch;
    order.title = title;
    order.version = version;
    order.plugin = request.plugin;
    order.source = request.host;
    Result<Entry> made = hacks.InstallHack(order, [this](float) { return !Cancelled(); });
    cleanup();
    if (!made) {
        record.outcome = Outcome::Failed;
        record.message = title + " was not added: " + made.error() + ". The patch is in EmuDEN's Downloads folder";
        return;
    }
    const Entry& hack = made.value();
    record.replaced = std::any_of(before.begin(), before.end(), [&](const Hack& old) { return old.id == hack.id; });
    record.outcome = Outcome::HackAdded;
    record.games.push_back({hack.id, hack.name, hack.system});
    record.hacks.push_back(hack.id);
    record.system_name = ConsoleName(systems, hack.system);
    record.message = record.replaced
                         ? std::format("{} has been updated{}", hack.name, version.empty() ? "" : " to " + version)
                         : std::format("{} has been added to the library, made from {}", hack.name, base->name);
    log::Info("downloads", "{}", record.message);
}

} // namespace den::library::downloads
