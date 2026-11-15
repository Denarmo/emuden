// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game_faces.h"

#include <format>
#include <functional>
#include <optional>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/gfx/device.h"
#include "den/gfx/images.h"
#include "face_images.h"

namespace den::app {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t MAX_QUEUED = 8; // older requests are dropped and asked again when seen
constexpr int MAX_SIDE = 1024;
constexpr int CLEAN_VERSION = 1; // bump when CleanFace changes, so pictures are cleaned again

// The cleaned copy of a picture, made once under the cache (never beside the original, which
// may sit in a read-only games folder). `image` gets the cleaned pixels when asked for. ""
// when the picture cannot be read.
std::string Cleaned(const std::string& path, FaceKind kind, gfx::ImageData* image = nullptr) {
    if (path.empty()) {
        return {};
    }
    const fs::path source = PathFromUtf8(path);
    std::error_code ec;
    const auto size = fs::file_size(source, ec);
    if (ec) {
        return {};
    }
    const std::size_t hash = std::hash<std::string>{}(std::format("{}|{}|{}|{}", path, size, static_cast<int>(kind),
                                                                  CLEAN_VERSION));
    const fs::path out = Svc().data_dir / "cache" / "faces" / std::format("{:016x}.png", hash);
    if (IsRegularFile(out)) {
        if (image != nullptr) {
            auto loaded = gfx::LoadImageFile(out);
            if (!loaded) {
                return {};
            }
            *image = std::move(*loaded);
        }
        return PathToUtf8(out);
    }
    auto loaded = gfx::LoadImageFile(source);
    if (!loaded) {
        return {};
    }
    gfx::ImageData& im = *loaded;
    gfx::ShrinkImage(im, MAX_SIDE);
    if (!CleanFace(im, kind)) {
        return {};
    }
    fs::create_directories(out.parent_path(), ec);
    if (!gfx::SavePng(out, im.width, im.height, im.rgba.data()).ok) {
        return {};
    }
    if (image != nullptr) {
        *image = std::move(im);
    }
    return PathToUtf8(out);
}

LoadedFaces Load(const std::string& id, Job& job) {
    LoadedFaces out;
    library::Library& library = *Svc().library;
    const auto stopping = [&job] { return job.CancelRequested(); };
    out.faces = library.Faces(id, stopping);
    library::GameFaces& f = out.faces;
    f.back = Cleaned(f.back, FaceKind::Back);
    f.spine = Cleaned(f.spine, FaceKind::Spine);
    f.cart_back = Cleaned(f.cart_back, FaceKind::Cart);
    f.disc = Cleaned(f.disc, FaceKind::Disc);
    if (gfx::ImageData cart; !f.cart.empty()) {
        f.cart = Cleaned(f.cart, FaceKind::Cart, &cart);
        if (!f.cart.empty()) {
            const CartReading reading = ReadCart(cart);
            out.plastic = reading.plastic;
            out.cart_aspect = reading.aspect;
            out.cart_label = reading.label;
        }
    }
    if (const std::optional<library::Entry> entry = library.Find(id); entry && !entry->placeholder_cover) {
        gfx::ImageData cover;
        out.cover = Cleaned(entry->cover, FaceKind::Cover, &cover);
        if (!out.cover.empty()) {
            out.cover_aspect = static_cast<float>(cover.width) / static_cast<float>(cover.height);
            double sum[3] = {0, 0, 0};
            const std::size_t pixels = cover.rgba.size() / 4;
            for (std::size_t i = 0; i < pixels; ++i) {
                for (int c = 0; c < 3; ++c) {
                    sum[c] += cover.rgba[i * 4 + c];
                }
            }
            if (pixels > 0) {
                const auto mean = [&](int c) { return static_cast<std::uint8_t>(sum[c] / static_cast<double>(pixels)); };
                out.cover_color = gfx::Color{mean(0), mean(1), mean(2), 255};
            }
        }
    }
    return out;
}

} // namespace

FaceLoader::~FaceLoader() {
    *alive_ = false;
    if (job_) {
        job_->Cancel();
    }
}

const LoadedFaces* FaceLoader::Get(const std::string& id) {
    const auto found = slots_.find(id);
    if (found != slots_.end()) {
        return found->second->done ? &found->second->result : nullptr;
    }
    slots_[id] = std::make_shared<Slot>();
    queue_.push_back(id);
    while (queue_.size() > MAX_QUEUED) {
        slots_.erase(queue_.front());
        queue_.pop_front();
    }
    Pump();
    return nullptr;
}

const LoadedFaces* FaceLoader::Peek(const std::string& id) const {
    const auto found = slots_.find(id);
    return found != slots_.end() && found->second->done ? &found->second->result : nullptr;
}

void FaceLoader::Pump() {
    if ((job_ && !job_->Finished()) || queue_.empty()) {
        return;
    }
    const std::string id = queue_.front();
    queue_.pop_front();
    std::shared_ptr<Slot> slot = slots_[id];
    job_ = Svc().jobs->Run(
        "Box faces",
        [slot, id](Job& job) {
            slot->result = Load(id, job);
            return Status{};
        },
        [this, slot, alive = alive_](Job&) {
            slot->done = true;
            if (*alive) {
                Pump();
            }
        });
}

} // namespace den::app
