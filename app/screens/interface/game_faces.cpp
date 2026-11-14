// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game_faces.h"

#include <algorithm>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/gfx/images.h"

namespace den::app {

namespace {

constexpr std::size_t MAX_QUEUED = 8; // older requests are dropped and asked again when seen

// Where the cartridge is in its photograph (the opaque part), and the colour of its plastic:
// the mean of the opaque pixels in a band along that part's edge, where no label is.
void ReadPhoto(const std::string& path, LoadedFaces& out) {
    auto image = gfx::LoadImageFile(PathFromUtf8(path));
    if (!image) {
        return;
    }
    gfx::ImageData& im = *image;
    gfx::ShrinkImage(im, 256);
    const auto alpha = [&](int x, int y) { return im.rgba[(static_cast<std::size_t>(y) * im.width + x) * 4 + 3]; };
    int x0 = im.width, y0 = im.height, x1 = -1, y1 = -1;
    for (int y = 0; y < im.height; ++y) {
        for (int x = 0; x < im.width; ++x) {
            if (alpha(x, y) > 128) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    if (x1 - x0 < 8 || y1 - y0 < 8) {
        return;
    }
    const int band = std::max(2, std::min(x1 - x0, y1 - y0) / 12);
    double sum[3] = {0, 0, 0};
    int count = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const bool edge = x - x0 < band || x1 - x < band || y1 - y < band;
            if (edge && alpha(x, y) > 200) {
                const std::uint8_t* p = &im.rgba[(static_cast<std::size_t>(y) * im.width + x) * 4];
                for (int c = 0; c < 3; ++c) {
                    sum[c] += p[c];
                }
                ++count;
            }
        }
    }
    if (count == 0) {
        return;
    }
    // The edge is in shadow on most photographs: a little lighter is closer to the plastic.
    const auto channel = [&](int c) { return static_cast<std::uint8_t>(std::min(255.0, sum[c] / count * 1.12)); };
    out.plastic = gfx::Color{channel(0), channel(1), channel(2), 255};
    const float w = static_cast<float>(im.width), h = static_cast<float>(im.height);
    out.cart_uv = gfx::Rect{x0 / w, y0 / h, (x1 + 1 - x0) / w, (y1 + 1 - y0) / h};
    out.cart_aspect = static_cast<float>(x1 + 1 - x0) / static_cast<float>(y1 + 1 - y0);
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
            slot->result.faces = Svc().library->Faces(id, [&job] { return job.CancelRequested(); });
            if (!slot->result.faces.cart.empty()) {
                ReadPhoto(slot->result.faces.cart, slot->result);
            }
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
