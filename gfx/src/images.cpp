// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "den/base/files.h"
#include "den/gfx/images.h"

namespace den::gfx {

Result<ImageData> DecodeImage(const void* data, std::size_t size) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size),
                                            &w, &h, &channels, 4);
    if (!pixels) {
        return Status::Fail("not an image this build can read");
    }
    ImageData image;
    image.width = w;
    image.height = h;
    image.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return image;
}

Result<ImageData> LoadImageFile(const std::filesystem::path& file) {
    const auto bytes = ReadBinaryFile(file);
    if (!bytes) {
        return Status::Fail("cannot read " + PathToUtf8(file));
    }
    return DecodeImage(bytes->data(), bytes->size());
}

void ShrinkImage(ImageData& image, int max_side) {
    while (max_side > 0 && (image.width > max_side || image.height > max_side) && image.width > 1 &&
           image.height > 1) {
        const int w = image.width / 2, h = image.height / 2;
        std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* a = image.rgba.data() + static_cast<std::size_t>(y) * 2 * image.width * 4;
            const std::uint8_t* b = a + static_cast<std::size_t>(image.width) * 4;
            std::uint8_t* o = out.data() + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 4; ++c) {
                    o[x * 4 + c] = static_cast<std::uint8_t>(
                        (a[x * 8 + c] + a[x * 8 + 4 + c] + b[x * 8 + c] + b[x * 8 + 4 + c] + 2) / 4);
                }
            }
        }
        image.width = w;
        image.height = h;
        image.rgba = std::move(out);
    }
}

Status SavePng(const std::filesystem::path& file, int width, int height, const std::uint8_t* rgba) {
    int size = 0;
    unsigned char* png = stbi_write_png_to_mem(rgba, width * 4, width, height, 4, &size);
    if (!png) {
        return Status::Fail("could not encode the image");
    }
    const bool ok = WriteFileAtomic(file, std::string_view(reinterpret_cast<const char*>(png),
                                                           static_cast<std::size_t>(size)));
    STBIW_FREE(png);
    return ok ? Status{} : Status::Fail("could not write " + PathToUtf8(file));
}

struct TextureCache::Impl {
    struct Entry {
        State state = State::Unknown;
        Texture texture;
        std::uint64_t used = 0;
    };
    // Shared with decode jobs, which may outlive the cache.
    struct Inbox {
        std::mutex mutex;
        std::deque<std::pair<std::string, Result<ImageData>>> done;
    };
    Device* device;
    Jobs* jobs;
    std::unordered_map<std::string, Entry> entries;
    std::shared_ptr<Inbox> inbox = std::make_shared<Inbox>();
    std::uint64_t frame = 0;
    std::size_t capacity = 400;

    void Upload(Entry& entry, const ImageData& image) {
        device->DestroyTexture(entry.texture);
        entry.texture = device->CreateTexture(image.width, image.height, PixelFormat::Rgba8,
                                              image.rgba.data());
        entry.state = State::Ready;
        entry.used = frame;
    }
};

TextureCache::TextureCache(Device& device, Jobs& jobs) : impl_(std::make_unique<Impl>()) {
    impl_->device = &device;
    impl_->jobs = &jobs;
}

TextureCache::~TextureCache() {
    for (auto& [key, entry] : impl_->entries) {
        impl_->device->DestroyTexture(entry.texture);
    }
}

Texture TextureCache::Get(const std::string& path, int max_side) {
    if (path.empty()) {
        return {};
    }
    Impl::Entry& entry = impl_->entries[path];
    entry.used = impl_->frame;
    if (entry.state == State::Unknown) {
        entry.state = State::Loading;
        impl_->jobs->Run("decode image", [inbox = impl_->inbox, path, max_side](Job&) {
            Result<ImageData> image = LoadImageFile(PathFromUtf8(path));
            if (image) {
                ShrinkImage(*image, max_side);
            }
            std::lock_guard lock(inbox->mutex);
            inbox->done.emplace_back(path, std::move(image));
            return Status{};
        });
    }
    return entry.texture;
}

TextureCache::State TextureCache::StateOf(const std::string& path) const {
    const auto it = impl_->entries.find(path);
    return it == impl_->entries.end() ? State::Unknown : it->second.state;
}

void TextureCache::Put(const std::string& key, const ImageData& image) {
    impl_->Upload(impl_->entries[key], image);
}

void TextureCache::Forget(const std::string& path) {
    const auto it = impl_->entries.find(path);
    if (it != impl_->entries.end()) {
        impl_->device->DestroyTexture(it->second.texture);
        impl_->entries.erase(it);
    }
}

void TextureCache::SetCapacity(std::size_t textures) {
    impl_->capacity = textures;
}

void TextureCache::Pump() {
    ++impl_->frame;
    for (int budget = 4; budget > 0; --budget) {
        std::pair<std::string, Result<ImageData>> item{"", Status::Fail("")};
        {
            std::lock_guard lock(impl_->inbox->mutex);
            if (impl_->inbox->done.empty()) {
                break;
            }
            item = std::move(impl_->inbox->done.front());
            impl_->inbox->done.pop_front();
        }
        const auto it = impl_->entries.find(item.first);
        if (it == impl_->entries.end()) {
            continue;
        }
        if (item.second) {
            impl_->Upload(it->second, *item.second);
        } else {
            it->second.state = State::Failed;
        }
    }
    if (impl_->entries.size() > impl_->capacity) {
        // Drop the ready textures that were not asked for in the last ten seconds or so.
        for (auto it = impl_->entries.begin(); it != impl_->entries.end();) {
            if (it->second.state == State::Ready && impl_->frame - it->second.used > 600) {
                impl_->device->DestroyTexture(it->second.texture);
                it = impl_->entries.erase(it);
            } else {
                ++it;
            }
        }
    }
}

} // namespace den::gfx
