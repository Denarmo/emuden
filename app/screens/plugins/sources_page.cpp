// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin sources: the settings section that lists them, adds one (its key's fingerprint is
// confirmed unless the user pinned it) and checks them, and the page of one source (route
// "plugin.source", param address) that switches it off or removes it.

#include <chrono>
#include <format>
#include <memory>

#include "../home/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "model_widgets.h"

namespace den::app {

namespace {

using plugins::SourceInfo;
using plugins::SourceProbe;

JobPtr g_probe, g_check, g_remove;
std::shared_ptr<SourceProbe> g_probed;
std::string g_removing;

std::optional<SourceInfo> FindSource(const std::string& address) {
    for (SourceInfo& info : Svc().plugins->Sources()) {
        if (info.address == address) {
            return info;
        }
    }
    return std::nullopt;
}

std::string CheckedText(std::int64_t checked) {
    if (checked <= 0) {
        return Tr("sources.never");
    }
    const std::chrono::sys_days day{std::chrono::days(checked / 86400)};
    const std::chrono::year_month_day date{day};
    const std::int64_t minutes = checked % 86400 / 60;
    return TrF("sources.checked", std::format("{}-{:02}-{:02} {:02}:{:02} UTC", static_cast<int>(date.year()),
                                              static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                                              minutes / 60, minutes % 60));
}

std::string StatusText(const SourceInfo& info) {
    if (!info.error.empty()) {
        return Tr("sources.problem");
    }
    if (!info.enabled) {
        return Tr("sources.off");
    }
    return info.official ? Tr("sources.always_on") : TrF("sources.offers", info.offered);
}

void CheckAll() {
    if (!(g_check && !g_check->Finished())) {
        g_check = Svc().plugins->Refresh();
        RebuildTop();
    }
}

void Add(const SourceProbe& probe) {
    if (const Status added = Svc().plugins->AddSource(probe); !added.ok) {
        Svc().stage->Toast(added.error, 5.0f);
        return;
    }
    Svc().stage->Toast(TrF("sources.added", probe.label));
    CheckAll();
}

void AskForSource() {
    if (g_probe && !g_probe->Finished()) {
        return;
    }
    ui::TextRequest request;
    request.title = Tr("sources.add_title");
    request.placeholder = Tr("sources.add_placeholder");
    request.max_length = 200;
    Svc().stage->AskText(std::move(request), [](const std::string& text) {
        g_probed = std::make_shared<SourceProbe>();
        g_probe = Svc().plugins->ProbeSource(text, g_probed);
        RebuildTop();
    });
}

// The probe found the source's key and its signed list: the user compares the fingerprint
// before anything of it is used, unless they pinned it already.
void Probed() {
    const JobPtr job = std::move(g_probe);
    const std::shared_ptr<SourceProbe> probe = std::move(g_probed);
    if (job->State() != JobState::Done) {
        if (job->State() == JobState::Failed) {
            Svc().stage->Toast(job->Error(), 6.0f);
        }
        return;
    }
    if (probe->pinned) {
        Add(*probe);
        return;
    }
    Svc().stage->Confirm(TrF("sources.confirm_title", probe->label),
                         TrF("sources.confirm_message", probe->fingerprint, probe->ns), Tr("sources.confirm_add"),
                         [probe] { Add(*probe); });
}

void EveryFrame() {
    if (g_probe && g_probe->Finished()) {
        Probed();
        RebuildTop();
    }
    if (g_check && g_check->Finished()) {
        const JobPtr job = std::move(g_check);
        if (job->State() == JobState::Failed) {
            Svc().stage->Toast(job->Error(), 6.0f);
        } else if (job->State() == JobState::Done) {
            Svc().stage->Toast(Tr("sources.checked_all")); // a source's own problem is on its row
        }
        RebuildTop();
    }
    if (g_remove && g_remove->Finished()) {
        const JobPtr job = std::move(g_remove);
        Svc().stage->Toast(job->State() == JobState::Done ? TrF("sources.removed", g_removing) : job->Error(), 5.0f);
        RebuildTop();
    }
}

std::vector<ui::RowSpec> SourceRows() {
    std::vector<ui::RowSpec> rows;
    ui::RowSpec add = ValueRow(
        "sources.add", Tr("sources.add"), [] { return JobText(g_probe, ""); }, AskForSource);
    add.description = Tr("sources.add_help");
    rows.push_back(std::move(add));
    ui::RowSpec check = ValueRow(
        "sources.check", Tr("sources.check"), [] { return JobText(g_check, ""); }, CheckAll);
    check.description = Tr("sources.check_help");
    rows.push_back(std::move(check));
    int index = 0;
    for (const SourceInfo& info : Svc().plugins->Sources()) {
        ui::RowSpec row = ValueRow(
            std::format("sources.source.{}", index++), info.label, [text = StatusText(info)] { return text; },
            [address = info.address] { Svc().stage->Push("plugin.source", {{"address", address}}); });
        row.description = info.official ? Tr("sources.official_help")
                                         : TrF("sources.key", info.fingerprint) + "  ·  " + CheckedText(info.checked);
        rows.push_back(std::move(row));
    }
    return rows;
}

class SourceScreen final : public ui::Page {
public:
    void Build() override {
        revision_ = Svc().plugins->Revision();
        const std::string address = Param("address");
        const std::optional<SourceInfo> info = FindSource(address);
        ui::Node* content = BuildPlainFrame(*this, info ? info->label : Tr("sources.title"));
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "source.list";
        list->pad_bottom = 24;
        if (!info) {
            list->Emplace<ui::Label>(Tr("sources.gone"));
            return;
        }
        const float width = std::min((Svc().canvas->Width() > 0 ? Svc().canvas->Width() : 1280.0f) -
                                         Svc().theme->margin * 2 - 12,
                                     860.0f);
        auto* trust = list->Emplace<Paragraph>(
            info->official ? Tr("sources.official_help") : TrF("source.from_added", info->label), width);
        trust->boxed = !info->official;
        if (!info->error.empty()) {
            auto* error = list->Emplace<Paragraph>(info->error, width);
            error->font = gfx::FONT_BOLD;
        }

        std::vector<ui::RowSpec> rows;
        const auto fact = [&](const std::string& id, const std::string& label, std::string value) {
            rows.push_back(ValueRow("source." + id, label, [value = std::move(value)] { return value; }));
        };
        fact("address", Tr("sources.address"), address);
        fact("key", Tr("sources.fingerprint"), info->fingerprint);
        fact("checked", Tr("sources.last_check"), CheckedText(info->checked));
        fact("offered", Tr("sources.offered"), std::to_string(info->offered));
        if (!info->official) {
            fact("installed", Tr("sources.installed"), std::to_string(info->installed));
            rows.push_back(ToggleRow(
                "source.enabled", Tr("sources.enabled"),
                [address] {
                    const auto found = FindSource(address);
                    return found && found->enabled;
                },
                [address](bool on) {
                    if (const Status status = Svc().plugins->SetSourceEnabled(address, on); !status.ok) {
                        Svc().stage->Toast(status.error, 4.0f);
                    }
                    RebuildTop();
                }));
            rows.back().description = Tr("sources.enabled_help");
            rows.push_back(ActionRow(
                "source.remove", Tr("sources.remove"),
                [address, label = info->label] {
                    Svc().stage->Confirm(TrF("sources.remove_title", label), Tr("sources.remove_message"),
                                         Tr("sources.remove"), [address, label] {
                                             g_removing = label;
                                             g_remove = Svc().plugins->RemoveSource(address);
                                             Svc().stage->Pop();
                                         });
                },
                Tr("sources.remove_help")));
        }
        AddRows(list, std::move(rows));

        if (!info->notes.empty()) {
            auto heading = ui::MakeHeading(Tr("sources.notes"));
            heading->size = 20;
            heading->pad_top = 10;
            list->Add(std::move(heading));
            int index = 0;
            for (const std::string& note : info->notes) {
                auto* text = list->Emplace<Paragraph>("\xE2\x80\xA2  " + note, width);
                text->id = std::format("source.note.{}", index++);
                text->focusable = true;
                text->tone = ui::Tone::Dim;
            }
        }
    }

    void Update(ui::Context&) override {
        if (Svc().plugins->Revision() != revision_) {
            Rebuild();
        }
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, Tr("hint.select")}, {input::Nav::Back, Tr("common.back")}};
    }

    bool OnNav(input::Nav nav) override {
        if (nav != input::Nav::Back) {
            return false;
        }
        GetStage().Pop();
        return true;
    }

private:
    std::uint64_t revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("plugin.source", SourceScreen);
DEN_REGISTER_SETTINGS(52, "Plugin sources", SourceRows);
DEN_REGISTER_FRAME(EveryFrame);

} // namespace den::app
