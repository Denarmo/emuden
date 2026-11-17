// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// "Patch a game", without any plugin: the user chooses a patch file (IPS, UPS, BPS, APS, PPF,
// xdelta, or a zip holding one), the library finds the game it is for, and a patched copy is
// added to the library. Route "patch_game" (param `file`); the row is in Settings > ROM hacks.

#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/library/hacks.h"

namespace den::app {

namespace {

// Shared between the page and its jobs, which may outlive it.
struct Work {
    std::mutex mutex;
    int stamp = 0; // moves on every change
    bool opened = false;
    Status open_status;
    library::PatchFile file;
    bool installing = false;
    Status install_status;
    std::string installed_id;
};

void ChoosePatch() {
    PickOptions options;
    options.title = "Choose a patch file";
    options.files = true;
    options.extensions = {"ips", "ups", "bps", "aps", "ppf", "xdelta", "xdelta3", "vcdiff", "zip", "7z"};
    Pick(std::move(options), [](const std::string& path) { Svc().stage->Push("patch_game", {{"file", path}}); });
}

// A message on as many lines as it needs: labels are one line each.
void AddText(ui::Node* list, const std::string& text, ui::Tone tone = ui::Tone::Text) {
    constexpr std::size_t WIDTH = 76;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = std::min(text.size(), at + WIDTH);
        if (end < text.size()) {
            const std::size_t space = text.rfind(' ', end);
            end = space != std::string::npos && space > at ? space : end;
        }
        list->Emplace<ui::Label>(Trim(text.substr(at, end - at)))->tone = tone;
        at = end;
    }
}

class PatchGameScreen final : public ui::Page {
public:
    void Build() override {
        if (!work_) {
            Open();
        }
        ui::Node* content = BuildPlainFrame(*this, "Patch a game");
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "patch.list";
        list->pad_bottom = 24;
        std::lock_guard lock(work_->mutex);
        seen_ = work_->stamp;
        const std::string name = PathToUtf8(PathFromUtf8(Param("file")).filename());
        if (!work_->opened) {
            list->Emplace<ui::Label>(TrF("Reading {}", name));
            return;
        }
        if (!work_->open_status) {
            AddText(list, work_->open_status.error);
            AddBack(list);
            return;
        }
        const library::PatchFile& file = work_->file;
        std::vector<ui::RowSpec> rows;
        rows.push_back(ValueRow("patch.file", file.title, [format = file.info.format_name] { return format; }));
        if (work_->installing) {
            rows.push_back(ValueRow("patch.progress", "Patching", [job = job_] { return JobText(job, "Done"); }));
            AddRows(list, std::move(rows));
            return;
        }
        if (!work_->install_status) {
            AddText(list, work_->install_status.error);
        }
        if (file.base) {
            rows.push_back(ValueRow("patch.base", "Game", [game = file.base->entry.name] { return game; }));
            rows.push_back(ActionRow("patch.install", "Install", [this, id = file.base->entry.id] { Install(id); },
                                     "A patched copy is added to your library. Your game is not changed."));
            AddRows(list, std::move(rows));
            return;
        }
        if (!file.missing.empty()) {
            AddRows(list, std::move(rows));
            AddText(list, file.missing);
            AddBack(list);
            return;
        }
        AddRows(list, std::move(rows));
        AddText(list, "This patch does not say which game it is for. Choose the game:", ui::Tone::Dim);
        std::vector<ui::RowSpec> games;
        int index = 0;
        for (const library::Entry& game : file.choices) {
            games.push_back(ValueRow("patch.choice." + std::to_string(index++), game.name,
                                     [system = game.system] { return system; }, [this, id = game.id] { Install(id); }));
        }
        AddRows(list, std::move(games));
    }

    void Update(ui::Context&) override {
        std::string installed;
        bool changed = false;
        {
            std::lock_guard lock(work_->mutex);
            changed = work_->stamp != seen_;
            installed = std::exchange(work_->installed_id, {});
        }
        if (!installed.empty()) {
            Svc().stage->Toast("Installed. It is in your library.");
            Svc().stage->Replace("details", {{"id", installed}});
        } else if (changed) {
            Rebuild();
        }
    }

private:
    static void AddBack(ui::Node* list) {
        auto* back = list->Emplace<ui::Button>("Back", [] { Svc().stage->Pop(); });
        back->id = "patch.back";
        back->width = 200;
    }

    void Open() {
        work_ = std::make_shared<Work>();
        const std::filesystem::path file = PathFromUtf8(Param("file"));
        const std::filesystem::path unpack = Svc().data_dir / "cache" / "patching" / "inbox";
        Svc().jobs->Run("Reading the patch", [work = work_, file, unpack, library = Svc().library](Job& job) {
            auto opened = library::OpenPatchFile(*library, file, unpack, &job);
            std::lock_guard lock(work->mutex);
            work->opened = true;
            work->open_status = opened.status();
            if (opened) {
                work->open_status = {};
                work->file = opened.take();
            }
            ++work->stamp;
            return Status{};
        });
    }

    void Install(const std::string& base_id) {
        library::HackRequest request;
        {
            std::lock_guard lock(work_->mutex);
            if (work_->installing) {
                return;
            }
            work_->installing = true;
            work_->install_status = {};
            ++work_->stamp;
            request.patch = work_->file.patch;
            request.title = work_->file.title;
        }
        request.base_id = base_id;
        job_ = Svc().jobs->Run("Patching", [work = work_, request, library = Svc().library](Job& job) {
            auto installed = library::HacksOf(*library).InstallHack(request, [&job](float fraction) {
                job.SetProgress(fraction);
                return !job.CancelRequested();
            });
            std::lock_guard lock(work->mutex);
            work->installing = false;
            if (installed) {
                work->installed_id = installed.value().id;
            } else {
                work->install_status = installed.status();
            }
            ++work->stamp;
            return Status{};
        });
    }

    std::shared_ptr<Work> work_;
    JobPtr job_;
    int seen_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("patch_game", PatchGameScreen);
DEN_REGISTER_SETTINGS(32, "ROM hacks", [] {
    return std::vector<ui::RowSpec>{ActionRow("patch.choose", "Patch a game", ChoosePatch,
                                              "Choose a patch file (IPS, UPS, BPS, APS, PPF, xdelta or a zip) for a game you own")};
});

} // namespace den::app
