// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The newer release (route "update"): its notes as plain text and Download and install.

#include <format>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/model_widgets.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "update_state.h"

namespace den::app {

namespace {

class UpdatePage final : public ui::Page {
public:
    void Build() override {
        const UpdateState& state = Updates();
        const ui::Theme& theme = *Svc().theme;
        if (!state.release) {
            ui::Node* content = BuildPlainFrame(*this, "Updates");
            content->Emplace<ui::Label>("No release is known yet. Check for updates in Settings.")->tone = ui::Tone::Dim;
            return;
        }
        const update::Release& release = *state.release;
        ui::Node* content =
            BuildPlainFrame(*this, !release.name.empty() ? release.name : TrF("EmuDEN {}", release.version));
        ui::Node* list = content->Add(ui::MakeList(12));
        list->id = "update.list";
        list->grow = 1;
        list->pad_bottom = 24;

        std::string about = TrF("You have {}.", UpdatesOffered() ? state.running : "a development build");
        if (!release.published.empty()) {
            about += std::format(" Released {}.", update::DateText(release.published));
        }
        list->Emplace<ui::Label>(about)->tone = ui::Tone::Dim;

        AddRows(list, UpdateInstallRows());

        const float width = std::max(200.0f, PageWidth() - theme.margin * 2 - 16);
        const std::string notes = update::PlainNotes(release.notes);
        // Long notes get stops on their paragraphs, so the pad can scroll through them.
        const bool stops = notes.size() > 400;
        int index = 0;
        for (std::size_t start = 0; start < notes.size();) {
            std::size_t end = notes.find("\n\n", start);
            end = end == std::string::npos ? notes.size() : end;
            auto* paragraph = list->Emplace<Paragraph>(notes.substr(start, end - start), width);
            paragraph->id = std::format("update.notes{}", index++);
            paragraph->focusable = stops;
            paragraph->label = "Release notes";
            start = end + 2;
        }
        if (notes.empty()) {
            list->Emplace<ui::Label>("This release has no notes.")->tone = ui::Tone::Dim;
        }
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Choose"}, {input::Nav::Back, "Back"}};
    }
};

} // namespace

DEN_REGISTER_SCREEN("update", UpdatePage);

} // namespace den::app
