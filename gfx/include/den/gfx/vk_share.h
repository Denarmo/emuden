// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Images that Vulkan writes and OpenGL samples: how a Vulkan core's picture reaches the GL
// canvas. There are a few slots, so Vulkan can write one while GL still reads another. Each
// platform shares them its own way (docs/vulkan.md); a copy through the CPU works everywhere.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "den/gfx/gl.h"
#include "den/gfx/types.h"
#include "den/gfx/vk.h"

namespace den::gfx {

struct VkShareDevice {
    const vk::Functions* fn = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    /// The device extensions that are on.
    std::vector<std::string> extensions;

    bool Has(const char* extension) const;
};

enum class VkShareKind { HardwareBuffer, MemoryObject, Copy };

class VkShare {
public:
    static constexpr int SLOTS = 3;

    /// Device extensions the sharing of this platform uses when the device has them.
    static std::vector<const char*> WantedExtensions();
    /// The GPU the current GL context draws with (GL_EXT_memory_object), false if GL can't say.
    static bool GlDeviceUuid(gl::ProcLoader loader, std::uint8_t uuid[VK_UUID_SIZE]);
    /// The best kind of sharing this device and the current GL context allow; the copy when
    /// nothing else does. Null when not even that works. Call with the GL context current.
    static std::unique_ptr<VkShare> Create(const VkShareDevice& device, gl::ProcLoader loader,
                                           bool gles);

    virtual ~VkShare() = default;
    virtual VkShareKind Kind() const = 0;
    const char* Name() const;

    /// Gives every slot an image of this size (contents undefined). The GL context is current
    /// and the device does not use the old images any more.
    virtual bool Resize(unsigned width, unsigned height) = 0;
    unsigned Width() const {
        return width_;
    }
    unsigned Height() const {
        return height_;
    }

    virtual VkImage Image(int slot) const = 0;
    /// Recorded before writing the slot's image: it is then in TRANSFER_DST_OPTIMAL.
    virtual void RecordBeginWrite(VkCommandBuffer cmd, int slot) = 0;
    /// Recorded after writing the top left width x height of the image.
    virtual void RecordEndWrite(VkCommandBuffer cmd, int slot, unsigned width, unsigned height) = 0;
    /// A semaphore the submit that wrote the slot signals, or null.
    virtual VkSemaphore SignalSemaphore(int slot) = 0;
    /// After that submit, on the GL thread: GL commands from now on see the picture. `fence`
    /// is the submit's.
    virtual void Published(int slot, VkFence fence, unsigned width, unsigned height) = 0;
    /// The slot's texture in the GL context, row 0 at the top of the picture.
    virtual Texture GlTexture(int slot) const = 0;

protected:
    unsigned width_ = 0, height_ = 0;
};

} // namespace den::gfx
