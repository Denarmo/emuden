// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A running plugin: the top page of its stack, drawn from the UI model. It starts the plugin
// when shown and closes it when the page goes; Back pops the plugin's own pages first. Also
// does for the plugin what only a screen can: the file browser, questions, messages, opening
// a game in the library, the in-app browser. Route "plugin.view", param id.

#include <format>
#include <utility>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/platform/pal.h"
#include "feature_api.h"
#include "model_render.h"
#include "../web/web_browser.h"

namespace den::app {

namespace {

using plugins::Ask;
using plugins::AskKind;
using plugins::Element;
using plugins::PageModel;

constexpr int GRACE_FRAMES = 3;  // frames an unanswered question may be gone before it counts as "no"
constexpr float NEAR_END = 140;  // this close to the end of a grid, more of it is asked for

class PluginView;
// The view whose plugin has the browser page open above it. A covered page gets no Update,
// so the frame serves that plugin's asks meanwhile (WhileBrowsing).
PluginView* g_browsing = nullptr;

class PluginView final : public ui::Page {
public:
    ~PluginView() override {
        plugins::FeatureHost* host = FeatureHostSvc();
        if (g_browsing == this) {
            g_browsing = nullptr;
        }
        if (host && web_ask_ != 0) {
            host->Answer(web_ask_, false);
        }
        if (host && opened_) {
            if (pick_ask_ != 0) {
                host->Answer(pick_ask_, false);
            }
            if (confirm_ask_ != 0) {
                host->Answer(confirm_ask_, false);
            }
            host->Close(id_);
        }
    }

    void Build() override {
        Services& svc = Svc();
        plugins::FeatureHost* host = FeatureHostSvc();
        id_ = Param("id");
        nodes_ = {};
        built_ = true;
        width_ = svc.canvas->Width();
        plugins::FeaturePlugin plugin;
        const bool known = host && FindFeaturePlugin(id_, plugin);
        host_revision_ = host ? host->Revision() : 0;
        if (known && !opened_ && start_error_.empty()) {
            const Status started = host->Open(id_);
            opened_ = started.ok;
            start_error_ = started.ok ? std::string() : started.error;
        }
        error_ = !known ? std::string("This plugin is not installed.")
                        : (!start_error_.empty() ? start_error_ : plugin.error);
        if (!error_.empty()) {
            BuildError(known ? plugin.name : std::string("Plugin"), known);
            return;
        }
        const std::vector<PageModel> pages = host->Pages(id_, revision_);
        depth_ = pages.size();
        if (pages.empty()) {
            PageModel waiting;
            waiting.busy = true;
            shape_.clear();
            nodes_ = BuildModel(BuildPlainFrame(*this, plugin.name), waiting, Sink());
            return;
        }
        const PageModel& top = pages.back();
        shape_ = ModelShape(top);
        ui::Node* content = BuildPlainFrame(*this, top.title.empty() ? plugin.name : top.title);
        content->gap = 12;
        nodes_ = BuildModel(content, top, Sink());
        if (nodes_.list) {
            nodes_.list->pad_bottom = 64; // the last row clears the hint bar
            if (keep_scroll_ > 0) {
                nodes_.list->scroller.Jump(keep_scroll_);
            }
        }
        if (nodes_.more_action.empty()) {
            more_sent_.clear();
        }
        keep_scroll_ = 0;
    }

    void Update(ui::Context&) override {
        plugins::FeatureHost* host = FeatureHostSvc();
        if (!host) {
            return;
        }
        RestoreFocus();
        ServeAsks(*host);
        WatchQuestions(*host);
        if (Svc().canvas->Width() != width_) {
            Refresh(true);
            return;
        }
        plugins::FeaturePlugin plugin;
        const bool known = FindFeaturePlugin(id_, plugin);
        if (!error_.empty() || !known || !plugin.error.empty()) {
            // The error page changes only with the plugin's state.
            if (host->Revision() != host_revision_) {
                Refresh(false);
            }
            return;
        }
        std::uint64_t revision = 0;
        const std::vector<PageModel> pages = host->Pages(id_, revision);
        if (revision != revision_) {
            revision_ = revision;
            if (pages.empty() || pages.size() != depth_) {
                MoveBetweenPages(pages.size());
                Refresh(false);
            } else if (ModelShape(pages.back()) != shape_) {
                Refresh(true);
            } else {
                // Only amounts and pictures moved: the nodes stay, so a finger on the page keeps
                // its hold.
                PatchProgress(nodes_, pages.back());
                PatchImages(nodes_, Sink());
            }
        }
        AskForMore(*host);
    }

    bool OnNav(input::Nav nav) override {
        if (nav != input::Nav::Back) {
            return false;
        }
        plugins::FeatureHost* host = FeatureHostSvc();
        if (host && error_.empty() && depth_ > 1) {
            host->Back(id_);
        } else {
            GetStage().Pop();
        }
        return true;
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Select"}, {input::Nav::Back, depth_ > 1 ? "Back" : "Close"}};
    }

    // Every frame while the browser page is above this one: messages about files that were
    // not taken, and the plugin closing the page.
    void WhileBrowsing() {
        plugins::FeatureHost* host = FeatureHostSvc();
        if (host && web_ask_ != 0 && GetStage().Top() != this) {
            web_gone_ = 0;
            ServeAsks(*host);
        }
    }

private:
    ModelSink Sink() {
        ModelSink sink;
        sink.width = (width_ > 0 ? width_ : 1280.0f) - Svc().theme->margin * 2;
        sink.send = [id = id_](plugins::UiEvent event) {
            if (plugins::FeatureHost* host = FeatureHostSvc()) {
                host->Send(id, std::move(event));
            }
        };
        sink.image = [id = id_](const std::string& image) {
            plugins::FeatureHost* host = FeatureHostSvc();
            return host && !image.empty() ? PathToUtf8(host->ImagePath(id, image)) : std::string();
        };
        return sink;
    }

    void BuildError(const std::string& name, bool known) {
        ui::Node* content = BuildPlainFrame(*this, name);
        ui::Node* box = content->Add(ui::MakeBox(ui::Axis::Column, 14));
        box->grow = 1;
        box->align = ui::Align::Center;
        box->justify = ui::Align::Center;
        box->Emplace<ui::Label>(known ? "The plugin stopped" : "Plugin not found", Svc().theme->text_heading,
                                gfx::FONT_BOLD);
        const float room = (width_ > 0 ? width_ : 1280.0f) - Svc().theme->margin * 2;
        auto* message = box->Emplace<Paragraph>(error_, std::min(room, 640.0f));
        message->boxed = true;
        ui::Node* buttons = box->Add(ui::MakeBox(ui::Axis::Row, 12));
        if (known) {
            auto* retry = buttons->Emplace<ui::Button>("Try again", [this] {
                start_error_.clear();
                if (plugins::FeatureHost* host = FeatureHostSvc()) {
                    if (opened_) {
                        host->Reload(id_);
                    }
                }
                Rebuild();
            });
            retry->id = "plugin.retry";
            retry->width = 220;
        }
        auto* close = buttons->Emplace<ui::Button>("Close", [this] { GetStage().Pop(); });
        close->id = "plugin.close";
        close->width = 220;
    }

    void Refresh(bool keep_scroll) {
        keep_scroll_ = keep_scroll && nodes_.list ? nodes_.list->scroller.Destination() : 0;
        Rebuild();
    }

    // A deeper page remembers what was selected below it; coming back selects it again.
    void MoveBetweenPages(std::size_t depth) {
        if (depth > depth_) {
            focus_stack_.resize(depth_ > 0 ? depth_ : 1);
            if (depth_ > 0) {
                focus_stack_[depth_ - 1] = {Focus() ? Focus()->id : std::string(),
                                            nodes_.list ? nodes_.list->scroller.Destination() : 0.0f};
            }
        } else if (depth > 0 && depth <= focus_stack_.size()) {
            restore_ = focus_stack_[depth - 1];
            restore_pending_ = true;
            built_ = false;
            focus_stack_.resize(depth - 1);
        }
    }

    void RestoreFocus() {
        if (!restore_pending_ || !built_) {
            return;
        }
        restore_pending_ = false;
        if (nodes_.list && restore_.scroll > 0) {
            nodes_.list->scroller.Jump(restore_.scroll);
        }
        if (ui::Node* node = restore_.focus.empty() ? nullptr : Root().FindById(restore_.focus)) {
            SetFocus(node);
        }
    }

    void ServeAsks(plugins::FeatureHost& host) {
        for (Ask& ask : host.TakeAsks(id_)) {
            switch (ask.kind) {
            case AskKind::Toast:
                GetStage().Toast(ask.message.empty() ? ask.title : ask.message, 4.0f);
                break;
            case AskKind::Confirm:
                confirm_ask_ = ask.id;
                confirm_gone_ = 0;
                GetStage().Confirm(ask.title, ask.message, ask.yes.empty() ? "Yes" : ask.yes, [this, id = ask.id] {
                    confirm_ask_ = 0;
                    if (plugins::FeatureHost* h = FeatureHostSvc()) {
                        h->Answer(id, true);
                    }
                });
                break;
            case AskKind::PickFile: {
                PickOptions options;
                options.title = ask.title.empty() ? "Choose a file" : ask.title;
                options.files = true;
                options.extensions = ask.extensions;
                pick_ask_ = ask.id;
                pick_gone_ = 0;
                Pick(std::move(options), [this, id = ask.id](const std::string& path) {
                    pick_ask_ = 0;
                    if (plugins::FeatureHost* h = FeatureHostSvc()) {
                        h->Answer(id, true, path);
                    }
                });
                break;
            }
            case AskKind::OpenGame:
                if (Svc().library->Find(ask.value)) {
                    GetStage().Push("details", {{"id", ask.value}});
                } else {
                    GetStage().Toast("That game is not in your library any more.");
                }
                break;
            case AskKind::OpenUrl:
                // An address of a host the plugin declared, after something the user pressed.
                if (!pal::OpenUrl(ask.value).ok) {
                    GetStage().Toast(std::format("Open in your browser: {}", ask.value), 6.0f);
                }
                break;
            case AskKind::OpenWeb:
                OpenWeb(host, ask);
                break;
            case AskKind::CloseWeb:
                if (web_ask_ != 0 && GetStage().Top() != this) {
                    GetStage().Pop();
                }
                break;
            case AskKind::Pop:
                GetStage().Pop();
                break;
            }
        }
    }

    // The browser page (route "web") for the plugin: every file the user downloads there
    // goes to the host, which checks it and hands it to the script.
    void OpenWeb(plugins::FeatureHost& host, const Ask& ask) {
        if (web_ask_ != 0) {
            host.Answer(ask.id, false); // one page at a time
            return;
        }
        web::OpenRequest request;
        request.url = ask.value;
        request.title = ask.title;
        request.allowed_hosts = ask.hosts;
        request.opener.extensions = ask.extensions;
        request.opener.on_download = [id = ask.id](const web::Download& download) {
            if (plugins::FeatureHost* h = FeatureHostSvc()) {
                h->Answer(id, true,
                          plugins::WebFileAnswer({PathToUtf8(download.file), download.name, download.host}));
            }
        };
        web_ask_ = ask.id;
        web_gone_ = 0;
        g_browsing = this;
        web::Open(std::move(request));
    }

    // A question or a file browser the user backed out of has no callback: the plugin gets
    // "no" once this page is in front again without an answer. The same tells it that the
    // browser page was closed.
    void WatchQuestions(plugins::FeatureHost& host) {
        if (web_ask_ != 0 && ++web_gone_ >= GRACE_FRAMES) {
            host.Answer(std::exchange(web_ask_, 0), false);
            if (g_browsing == this) {
                g_browsing = nullptr;
            }
        }
        if (confirm_ask_ != 0 && !GetStage().DialogOpen() && ++confirm_gone_ >= GRACE_FRAMES) {
            host.Answer(std::exchange(confirm_ask_, 0), false);
        }
        if (pick_ask_ != 0 && ++pick_gone_ >= GRACE_FRAMES) {
            host.Answer(std::exchange(pick_ask_, 0), false);
        }
    }

    // The end of a grid that has more: once per state of the model.
    void AskForMore(plugins::FeatureHost& host) {
        const ui::Node* list = nodes_.list;
        if (nodes_.more_action.empty() || !list || list->content_extent <= 0 || more_sent_ == shape_) {
            return;
        }
        if (list->scroller.Destination() + list->rect.h >= list->content_extent - NEAR_END) {
            more_sent_ = shape_;
            host.Send(id_, plugins::UiEvent{nodes_.more_action, {}, {}});
        }
    }

    struct Place {
        std::string focus;
        float scroll = 0;
    };

    std::string id_, error_, start_error_, shape_, more_sent_;
    ModelNodes nodes_;
    std::vector<Place> focus_stack_;
    Place restore_;
    std::size_t depth_ = 0;
    std::uint64_t revision_ = 0, host_revision_ = 0, pick_ask_ = 0, confirm_ask_ = 0, web_ask_ = 0;
    int pick_gone_ = 0, confirm_gone_ = 0, web_gone_ = 0;
    float width_ = 0, keep_scroll_ = 0;
    bool opened_ = false, built_ = false, restore_pending_ = false;
};

} // namespace

DEN_REGISTER_SCREEN("plugin.view", PluginView);
DEN_REGISTER_FRAME([] {
    if (g_browsing != nullptr) {
        g_browsing->WhileBrowsing();
    }
});

} // namespace den::app
