// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The in-app file and folder browser: places, then folders, navigable with a pad.

#include <algorithm>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"

namespace den::app {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t MAX_ENTRIES = 600;

struct Request {
    PickOptions options;
    std::function<void(const std::string&)> on_pick;
};
Request g_request;

bool Hidden(const std::string& name) {
    return name.empty() || name[0] == '.' || name[0] == '$';
}

class PickerPage final : public ui::Page {
public:
    PickerPage() : request_(g_request) {
        std::error_code ec;
        if (!request_.options.start.empty() && fs::is_directory(PathFromUtf8(request_.options.start), ec)) {
            current_ = PathFromUtf8(request_.options.start);
        }
    }

    void Build() override {
        ui::Node* content = BuildPlainFrame(*this, request_.options.title);
        auto* where = content->Emplace<ui::Label>(current_.empty() ? "Places" : PathToUtf8(current_));
        where->tone = ui::Tone::Dim;
        ui::Node* list = content->Add(ui::MakeList(6));
        list->id = "picker.list";
        list->pad_bottom = 24;

        if (current_.empty()) {
            for (const fs::path& place : pal::StorageRoots()) {
                AddFolder(list, place, PathToUtf8(place));
            }
            return;
        }
        if (!request_.options.files) {
            auto* use = list->Emplace<ui::Button>("Use this folder");
            use->id = "picker.use";
            use->centered = false;
            use->selected = true;
            use->on_activate = [this] { Finish(current_); };
        }
        auto* up = list->Emplace<ui::Button>("..  (up)");
        up->id = "picker.up";
        up->centered = false;
        up->on_activate = [this] { Up(); };

        std::vector<fs::path> folders, files;
        for (const fs::path& entry : ListDirectory(current_)) {
            if (Hidden(PathToUtf8(entry.filename()))) {
                continue;
            }
            if (IsDirectory(entry)) {
                folders.push_back(entry);
            } else if (request_.options.files && Wanted(entry)) {
                files.push_back(entry);
            }
        }
        const auto by_name = [](const fs::path& a, const fs::path& b) {
            return ToLower(PathToUtf8(a.filename())) < ToLower(PathToUtf8(b.filename()));
        };
        std::sort(folders.begin(), folders.end(), by_name);
        std::sort(files.begin(), files.end(), by_name);
        std::size_t shown = 0;
        for (const fs::path& folder : folders) {
            if (++shown > MAX_ENTRIES) {
                break;
            }
            AddFolder(list, folder, PathToUtf8(folder.filename()));
        }
        for (const fs::path& file : files) {
            if (++shown > MAX_ENTRIES) {
                break;
            }
            auto* button = list->Emplace<ui::Button>(PathToUtf8(file.filename()));
            button->id = "picker.file." + PathToUtf8(file.filename());
            button->centered = false;
            button->on_activate = [this, file] { Finish(file); };
        }
    }

    bool OnNav(input::Nav nav) override {
        if (nav == input::Nav::Back && !current_.empty()) {
            Up();
            return true;
        }
        return false;
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Open"}, {input::Nav::Back, current_.empty() ? "Cancel" : "Up"}};
    }

private:
    bool Wanted(const fs::path& file) const {
        const auto& extensions = request_.options.extensions;
        if (extensions.empty()) {
            return true;
        }
        std::string extension = ToLower(PathToUtf8(file.extension()));
        if (!extension.empty()) {
            extension.erase(0, 1);
        }
        return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
    }

    void AddFolder(ui::Node* list, const fs::path& folder, const std::string& name) {
        auto* button = list->Emplace<ui::Button>(name);
        button->id = "picker.dir." + name;
        button->centered = false;
        button->on_activate = [this, folder] {
            current_ = folder;
            Rebuild(false);
        };
    }

    void Up() {
        const fs::path parent = current_.parent_path();
        current_ = parent == current_ ? fs::path() : parent;
        Rebuild(false);
    }

    void Finish(const fs::path& chosen) {
        // The callback runs after the browser has left, so it may open pages itself.
        const auto on_pick = request_.on_pick;
        const std::string path = PathToUtf8(chosen);
        GetStage().Pop();
        if (on_pick) {
            on_pick(path);
        }
    }

    Request request_;
    fs::path current_;
};

} // namespace

void Pick(PickOptions options, std::function<void(const std::string&)> on_pick) {
    g_request = Request{std::move(options), std::move(on_pick)};
    Svc().stage->Push("picker");
}

void ChooseEnginePath(const std::string& system) {
    PickOptions options;
    options.title = "Choose the engine program";
    options.files = true;
    const std::string key = engine::PathKey(system);
    const std::string current = Svc().settings->GetString(key);
    if (!current.empty()) {
        const fs::path path = PathFromUtf8(current);
        options.start = PathToUtf8(IsDirectory(path) ? path : path.parent_path());
    }
    Pick(std::move(options), [key](const std::string& path) {
        Svc().settings->SetString(key, path);
        Svc().stage->Toast("Engine: " + path);
        RebuildTop();
    });
}

DEN_REGISTER_SCREEN("picker", PickerPage);

} // namespace den::app
