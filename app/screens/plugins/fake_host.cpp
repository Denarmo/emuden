// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdlib>
#include <format>
#include <utility>
#include <functional>
#include <map>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/gfx/device.h"
#include "fake_host.h"

namespace den::app {

namespace {

using plugins::Ask;
using plugins::AskKind;
using plugins::Element;
using plugins::ElementType;
using plugins::FeaturePlugin;
using plugins::PageModel;
using plugins::UiEvent;

constexpr double LOAD_SECONDS = 0.7, IMAGE_SECONDS = 0.9, DOWNLOAD_SECONDS = 2.5;

Element* FindElement(std::vector<Element>& elements, const std::string& id) {
    for (Element& element : elements) {
        if (element.id == id) {
            return &element;
        }
        if (Element* inner = FindElement(element.items, id)) {
            return inner;
        }
    }
    return nullptr;
}

// "\"text\"" -> text; anything else as it is.
std::string Unquote(const std::string& json) {
    return json.size() >= 2 && json.front() == '"' && json.back() == '"' ? json.substr(1, json.size() - 2) : json;
}

// Small pictures for the sample tiles: a colour per index, darker towards the bottom.
void WriteSamplePictures(const std::filesystem::path& dir) {
    static const std::uint8_t COLOURS[sample::PICTURES][3] = {{196, 120, 64}, {72, 132, 110}, {96, 104, 168},
                                                              {168, 84, 108}, {60, 128, 160}, {150, 140, 70}};
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    constexpr int W = 96, H = 128;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(W) * H * 4);
    for (int index = 0; index < sample::PICTURES; ++index) {
        const std::filesystem::path file = dir / std::format("{}.png", index);
        if (std::filesystem::exists(file, ec)) {
            continue;
        }
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const bool band = ((x + y * 2) / 24) % 2 == 0;
                const float shade = (1.0f - 0.45f * static_cast<float>(y) / H) * (band ? 1.0f : 0.86f);
                std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * W + x) * 4];
                for (int c = 0; c < 3; ++c) {
                    p[c] = static_cast<std::uint8_t>(COLOURS[index][c] * shade);
                }
                p[3] = 255;
            }
        }
        gfx::SavePng(file, W, H, rgba.data());
    }
}

class SampleHost final : public plugins::FeatureHost {
public:
    SampleHost() : plugins_(sample::Plugins()), pictures_(Svc().data_dir / "cache" / "plugin-samples") {
        WriteSamplePictures(pictures_);
    }

    std::vector<FeaturePlugin> Plugins() const override {
        std::vector<FeaturePlugin> out = plugins_;
        for (FeaturePlugin& plugin : out) {
            plugin.running = running_.contains(plugin.id);
            if (plugin.installed) {
                plugin.icon = pictures_ / std::format("{}.png", plugin.id.size() % sample::PICTURES);
            }
        }
        std::stable_sort(out.begin(), out.end(),
                         [](const FeaturePlugin& a, const FeaturePlugin& b) { return a.installed > b.installed; });
        return out;
    }

    std::uint64_t Revision() const override {
        return revision_;
    }

    JobPtr Install(const std::string& id, bool consent) override {
        FeaturePlugin* plugin = Find(id);
        if (!plugin) {
            return Svc().jobs->Run("install plugin", [](Job&) { return Status::Fail("No such plugin"); });
        }
        if (plugin->needs_consent && !consent) {
            return Svc().jobs->Run("install plugin",
                                   [](Job&) { return Status::Fail("The plugin's permissions were not accepted"); });
        }
        return Svc().jobs->Run(
            "install plugin",
            [](Job& job) {
                job.SetMessage("Downloading");
                for (int step = 0; step <= 20 && !job.CancelRequested(); ++step) {
                    job.SetProgress(static_cast<float>(step) / 20.0f);
                    pal::SleepMs(60);
                }
                return Status{};
            },
            [this, id](Job& job) {
                FeaturePlugin* done = Find(id);
                if (done && job.State() == JobState::Done) {
                    done->installed = true;
                    done->update_available = false;
                    done->needs_consent = false;
                    done->permissions.insert(done->permissions.end(), done->new_permissions.begin(),
                                             done->new_permissions.end());
                    done->new_permissions.clear();
                }
                ++revision_;
            });
    }

    JobPtr Remove(const std::string& id, bool, bool) override {
        return Svc().jobs->Run(
            "remove plugin",
            [](Job&) {
                pal::SleepMs(300);
                return Status{};
            },
            [this, id](Job&) {
                if (FeaturePlugin* plugin = Find(id)) {
                    plugin->installed = false;
                    plugin->needs_consent = plugin->trust != FeaturePlugin::Trust::Official;
                    plugin->error.clear();
                }
                running_.erase(id);
                ++revision_;
            });
    }

    Status Open(const std::string& id) override {
        FeaturePlugin* plugin = Find(id);
        if (!plugin || !plugin->installed) {
            return Status::Fail("The plugin is not installed");
        }
        plugin->error.clear();
        Running& run = running_[id];
        if (!run.pages.empty()) {
            return {};
        }
        if (id == sample::ROMHACKS) {
            run.pages.push_back(sample::RomHacksHome());
        } else if (id == sample::CATALOGUE) {
            run.pages.push_back(sample::CataloguePage(run.query, false));
            LoadCatalogue(id);
        } else {
            run.pages.push_back(sample::GalleryPage());
        }
        ++run.revision;
        ++revision_;
        return {};
    }

    void Close(const std::string& id) override {
        if (running_.erase(id) > 0) {
            ++revision_;
        }
    }

    void Reload(const std::string& id) override {
        Close(id);
        Open(id);
    }

    std::vector<PageModel> Pages(const std::string& id, std::uint64_t& revision) const override {
        const auto found = running_.find(id);
        if (found == running_.end()) {
            revision = 0;
            return {};
        }
        revision = found->second.revision + image_revision_;
        return found->second.pages;
    }

    void Send(const std::string& id, UiEvent event) override {
        const auto found = running_.find(id);
        if (found == running_.end() || found->second.pages.empty()) {
            return;
        }
        Running& run = found->second;
        const std::string& action = event.action;
        if (action == "search" || action == "retry" || action == "filter" || action == "more") {
            if (action == "search") {
                run.query.text = event.value;
                run.query.shown = sample::CATALOGUE_PAGE;
            } else if (action == "filter") {
                run.query.chips.clear();
                for (std::size_t start = 0; start < event.value.size();) {
                    const std::size_t comma = std::min(event.value.find(',', start), event.value.size());
                    run.query.chips.push_back(event.value.substr(start, comma - start));
                    start = comma + 1;
                }
                run.query.shown = sample::CATALOGUE_PAGE;
            } else if (action == "more") {
                run.query.shown += sample::CATALOGUE_PAGE;
            } else if (ToLower(run.query.text) == "error") {
                run.query.text.clear(); // Retry works the second time
            }
            if (action != "more") {
                run.pages.front() = sample::CataloguePage(run.query, false);
            }
            LoadCatalogue(id);
        } else if (action == "detail") {
            run.pages.push_back(sample::CatalogueDetail(std::atoi(event.arg_json.c_str())));
        } else if (action == "bases") {
            run.pages.push_back(sample::RomHacksBases());
        } else if (action == "install") {
            run.pages.push_back(sample::RomHacksInstall(Unquote(event.arg_json)));
        } else if (action == "import") {
            Ask ask = NewAsk(id, AskKind::PickFile);
            ask.title = "Choose a patch";
            ask.extensions = {"bps", "xdelta", "ips", "ups"};
            run.asks.push_back(std::move(ask));
        } else if (action == "open_site") {
            Ask ask = NewAsk(id, AskKind::OpenUrl);
            ask.value = "https://www.hackdex.app/discover";
            run.asks.push_back(std::move(ask));
        } else if (action == "open_game") {
            const std::vector<library::Entry> games = Svc().library->Entries();
            if (games.empty()) {
                Toast(run, id, "There is no game in the library to open");
            } else {
                Ask ask = NewAsk(id, AskKind::OpenGame);
                ask.value = games.front().id;
                run.asks.push_back(std::move(ask));
            }
        } else if (action == "confirm") {
            Ask ask = NewAsk(id, AskKind::Confirm);
            ask.title = "A question from the plugin";
            ask.message = "The script waits for the answer.";
            ask.yes = "Yes";
            run.asks.push_back(std::move(ask));
        } else if (action == "toast") {
            Toast(run, id, event.arg_json.empty() ? "Pressed" : Unquote(event.arg_json));
        } else if (action == "typed") {
            Toast(run, id, std::format("The plugin got: {}", event.value));
        } else if (action == "download") {
            StartDownload(run, id);
        } else if (action == "fail") {
            if (FeaturePlugin* plugin = Find(id)) {
                plugin->error = "main.lua:42: attempt to index a nil value (sample failure)";
                ++revision_;
            }
        }
        ++run.revision;
    }

    void Back(const std::string& id) override {
        const auto found = running_.find(id);
        if (found != running_.end() && found->second.pages.size() > 1) {
            found->second.pages.pop_back();
            ++found->second.revision;
        }
    }

    std::vector<Ask> TakeAsks(const std::string& id) override {
        const auto found = running_.find(id);
        if (found == running_.end()) {
            return {};
        }
        return std::exchange(found->second.asks, {});
    }

    void Answer(std::uint64_t ask_id, bool accepted, const std::string& value) override {
        const auto pending = pending_.find(ask_id);
        if (pending == pending_.end()) {
            return;
        }
        const auto [plugin, kind] = pending->second;
        pending_.erase(pending);
        const auto found = running_.find(plugin);
        if (found == running_.end()) {
            return;
        }
        Running& run = found->second;
        if (kind == AskKind::PickFile && accepted) {
            run.pages.push_back(sample::RomHacksInstall(value));
            ++run.revision;
        } else if (kind == AskKind::Confirm) {
            Toast(run, plugin, accepted ? "The plugin got: yes" : "The plugin got: no");
        }
    }

    std::filesystem::path ImagePath(const std::string&, const std::string& image) override {
        if (image.starts_with("plugin:")) {
            return pictures_ / image.substr(7);
        }
        if (image.starts_with("game:")) {
            const auto game = Svc().library->Find(image.substr(5));
            return game ? PathFromUtf8(game->cover) : std::filesystem::path{};
        }
        const std::size_t slash = image.rfind('/');
        if (!image.starts_with("https://") || slash == std::string::npos) {
            return {};
        }
        // A remote picture arrives a moment after it was first asked for.
        const auto [found, fresh] = images_.try_emplace(image, pal::NowSeconds() + IMAGE_SECONDS);
        return !fresh && found->second <= 0 ? pictures_ / image.substr(slash + 1) : std::filesystem::path{};
    }

    std::string RouteFile(const std::filesystem::path& file) override {
        const std::string extension = ToLower(PathToUtf8(file.extension()));
        return extension == ".bps" || extension == ".xdelta" || extension == ".ips" || extension == ".ups"
                   ? std::string(sample::ROMHACKS)
                   : std::string();
    }

    void Pump() override {
        const double now = pal::NowSeconds();
        for (auto& [image, due] : images_) {
            if (due > 0 && due <= now) {
                due = 0;
                ++image_revision_;
            }
        }
        std::vector<Task> due;
        std::erase_if(tasks_, [&](Task& task) {
            if (task.due > now) {
                return false;
            }
            due.push_back(std::move(task));
            return true;
        });
        for (Task& task : due) {
            if (const auto found = running_.find(task.plugin); found != running_.end()) {
                task.run(found->second);
                ++found->second.revision;
            }
        }
        for (auto& [id, run] : running_) {
            if (run.download_start > 0) {
                AdvanceDownload(run, id, now);
            }
        }
    }

private:
    struct Running {
        std::vector<PageModel> pages;
        std::vector<Ask> asks;
        std::uint64_t revision = 0;
        sample::CatalogueQuery query;
        double download_start = 0;
    };
    struct Task {
        double due = 0;
        std::string plugin;
        std::function<void(Running&)> run;
    };

    FeaturePlugin* Find(const std::string& id) {
        const auto found = std::ranges::find(plugins_, id, &FeaturePlugin::id);
        return found == plugins_.end() ? nullptr : &*found;
    }

    Ask NewAsk(const std::string& plugin, AskKind kind) {
        Ask ask;
        ask.id = ++next_ask_;
        ask.kind = kind;
        if (kind == AskKind::PickFile || kind == AskKind::Confirm) {
            pending_[ask.id] = {plugin, kind};
        }
        return ask;
    }

    void Toast(Running& run, const std::string& plugin, std::string text) {
        Ask ask = NewAsk(plugin, AskKind::Toast);
        ask.message = std::move(text);
        run.asks.push_back(std::move(ask));
    }

    void LoadCatalogue(const std::string& id) {
        tasks_.push_back({pal::NowSeconds() + LOAD_SECONDS, id, [](Running& run) {
                              run.pages.front() = sample::CataloguePage(run.query, true);
                          }});
    }

    void StartDownload(Running& run, const std::string&) {
        PageModel& page = run.pages.back();
        if (run.download_start > 0 || FindElement(page.body, "download")) {
            return;
        }
        if (Element* button = FindElement(page.body, "get")) {
            button->enabled = false;
            button->label = "Installing";
        }
        Element progress;
        progress.type = ElementType::Progress;
        progress.id = "download";
        progress.label = "Downloading the patch";
        progress.fraction = 0;
        page.body.insert(page.body.begin(), std::move(progress)); // where it is seen at once
        run.download_start = pal::NowSeconds();
    }

    void AdvanceDownload(Running& run, const std::string& id, double now) {
        const float fraction = static_cast<float>((now - run.download_start) / DOWNLOAD_SECONDS);
        // The page that shows the download may be under a newer one, or gone.
        for (PageModel& page : run.pages) {
            Element* progress = FindElement(page.body, "download");
            if (!progress) {
                continue;
            }
            if (fraction < 1.0f) {
                progress->fraction = fraction;
                progress->label = fraction < 0.7f ? "Downloading the patch" : "Patching your copy";
            } else {
                std::erase_if(page.body, [](const Element& element) { return element.id == "download"; });
                if (Element* button = FindElement(page.body, "get")) {
                    button->label = "Installed";
                }
            }
        }
        if (fraction >= 1.0f) {
            run.download_start = 0;
            Toast(run, id, "Installed. It is in your library (sample: nothing was written).");
        }
        ++run.revision;
    }

    std::vector<FeaturePlugin> plugins_;
    std::filesystem::path pictures_;
    std::map<std::string, Running> running_;
    std::map<std::string, double> images_; // url -> when it is there; 0 = there
    std::map<std::uint64_t, std::pair<std::string, AskKind>> pending_;
    std::vector<Task> tasks_;
    std::uint64_t revision_ = 1, image_revision_ = 0, next_ask_ = 0;
};

} // namespace

std::unique_ptr<plugins::FeatureHost> CreateSampleFeatureHost() {
    return std::make_unique<SampleHost>();
}

} // namespace den::app
