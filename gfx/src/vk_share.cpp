// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Picking the kind of sharing, the helpers the kinds use, and the copy through the CPU.

#include <algorithm>
#include <array>
#include <cstring>

#include "den/base/log.h"
#include "vk_share_common.h"

namespace den::gfx {

namespace vkshare {

void RecordBarrier(const vk::Functions& fn, VkCommandBuffer cmd, VkImage image, const Barrier& b) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = b.src_access;
    barrier.dstAccessMask = b.dst_access;
    barrier.oldLayout = b.from;
    barrier.newLayout = b.to;
    barrier.srcQueueFamilyIndex = b.src_family;
    barrier.dstQueueFamilyIndex = b.dst_family;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    fn.CmdPipelineBarrier(cmd, b.src_stage, b.dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void RecordDiscardToTransferDst(const vk::Functions& fn, VkCommandBuffer cmd, VkImage image) {
    Barrier b;
    b.to = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    b.dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    RecordBarrier(fn, cmd, image, b);
}

VkImage CreateImage(const VkShareDevice& d, unsigned width, unsigned height,
                    VkImageUsageFlags usage, const void* next) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.pNext = next;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = FORMAT;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    if (d.fn->CreateImage(d.device, &info, nullptr, &image) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return image;
}

bool HasGlExtension(const char* name) {
    gl::Int count = 0;
    gl::GetIntegerv(gl::NUM_EXTENSIONS, &count);
    for (gl::Int i = 0; i < count; ++i) {
        const auto* ext = reinterpret_cast<const char*>(gl::GetStringi(gl::EXTENSIONS, static_cast<gl::UInt>(i)));
        if (ext && std::strcmp(ext, name) == 0) {
            return true;
        }
    }
    return false;
}

void SetOpaqueTextureParams() {
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, TEXTURE_SWIZZLE_A, gl::ONE);
}

namespace {

constexpr gl::Enum TEXTURE_BINDING_2D = 0x8069;

// Vulkan draws into a device image, copies it into a buffer the CPU can read, and GL uploads
// that once the fence says the copy is done.
class CopyShare final : public VkShare {
public:
    explicit CopyShare(const VkShareDevice& d) : d_(d) {
        d_.fn->GetPhysicalDeviceMemoryProperties(d_.gpu, &memory_);
    }
    ~CopyShare() override {
        Free();
    }

    VkShareKind Kind() const override {
        return VkShareKind::Copy;
    }

    bool Resize(unsigned width, unsigned height) override {
        Free();
        const vk::Functions& fn = *d_.fn;
        for (Slot& slot : slots_) {
            slot.image = vkshare::CreateImage(d_, width, height,
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, nullptr);
            if (!slot.image) {
                return false;
            }
            VkMemoryRequirements req{};
            fn.GetImageMemoryRequirements(d_.device, slot.image, &req);
            if (!Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &slot.image_memory) ||
                fn.BindImageMemory(d_.device, slot.image, slot.image_memory, 0) != VK_SUCCESS) {
                return false;
            }
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = static_cast<VkDeviceSize>(width) * height * 4;
            info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (fn.CreateBuffer(d_.device, &info, nullptr, &slot.buffer) != VK_SUCCESS) {
                return false;
            }
            fn.GetBufferMemoryRequirements(d_.device, slot.buffer, &req);
            if (!Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                          &slot.buffer_memory) &&
                !Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &slot.buffer_memory)) {
                return false;
            }
            if (fn.BindBufferMemory(d_.device, slot.buffer, slot.buffer_memory, 0) != VK_SUCCESS ||
                fn.MapMemory(d_.device, slot.buffer_memory, 0, VK_WHOLE_SIZE, 0, &slot.mapped) != VK_SUCCESS) {
                return false;
            }
            gl::Int old = 0;
            gl::GetIntegerv(TEXTURE_BINDING_2D, &old);
            gl::GenTextures(1, &slot.texture);
            gl::BindTexture(gl::TEXTURE_2D, slot.texture);
            gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, static_cast<gl::Sizei>(width),
                           static_cast<gl::Sizei>(height), 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
            vkshare::SetOpaqueTextureParams();
            gl::BindTexture(gl::TEXTURE_2D, static_cast<gl::UInt>(old));
        }
        width_ = width;
        height_ = height;
        return true;
    }

    VkImage Image(int slot) const override {
        return slots_[slot].image;
    }

    void RecordBeginWrite(VkCommandBuffer cmd, int slot) override {
        vkshare::RecordDiscardToTransferDst(*d_.fn, cmd, slots_[slot].image);
    }

    void RecordEndWrite(VkCommandBuffer cmd, int slot, unsigned width, unsigned height) override {
        const vk::Functions& fn = *d_.fn;
        const Slot& s = slots_[slot];
        vkshare::Barrier b;
        b.from = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.to = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dst_access = VK_ACCESS_TRANSFER_READ_BIT;
        b.src_stage = b.dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        vkshare::RecordBarrier(fn, cmd, s.image, b);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        fn.CmdCopyImageToBuffer(cmd, s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.buffer, 1, &region);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = s.buffer;
        host.size = VK_WHOLE_SIZE;
        fn.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                              nullptr, 1, &host, 0, nullptr);
    }

    VkSemaphore SignalSemaphore(int) override {
        return VK_NULL_HANDLE;
    }

    void Published(int slot, VkFence fence, unsigned width, unsigned height) override {
        const vk::Functions& fn = *d_.fn;
        Slot& s = slots_[slot];
        fn.WaitForFences(d_.device, 1, &fence, VK_TRUE, UINT64_MAX);
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = s.buffer_memory;
        range.size = VK_WHOLE_SIZE;
        fn.InvalidateMappedMemoryRanges(d_.device, 1, &range);
        gl::Int old = 0;
        gl::GetIntegerv(TEXTURE_BINDING_2D, &old);
        gl::BindTexture(gl::TEXTURE_2D, s.texture);
        gl::PixelStorei(gl::UNPACK_ALIGNMENT, 4);
        gl::PixelStorei(gl::UNPACK_ROW_LENGTH, 0);
        gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, static_cast<gl::Sizei>(width),
                          static_cast<gl::Sizei>(height), gl::RGBA, gl::UNSIGNED_BYTE, s.mapped);
        gl::BindTexture(gl::TEXTURE_2D, static_cast<gl::UInt>(old));
    }

    Texture GlTexture(int slot) const override {
        return Texture{slots_[slot].texture, static_cast<int>(width_), static_cast<int>(height_)};
    }

private:
    struct Slot {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory image_memory = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        gl::UInt texture = 0;
    };

    bool Allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags flags, VkDeviceMemory* out) {
        const int type = vk::FindMemoryType(memory_, req.memoryTypeBits, flags);
        if (type < 0) {
            return false;
        }
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = req.size;
        info.memoryTypeIndex = static_cast<std::uint32_t>(type);
        return d_.fn->AllocateMemory(d_.device, &info, nullptr, out) == VK_SUCCESS;
    }

    void Free() {
        const vk::Functions& fn = *d_.fn;
        for (Slot& s : slots_) {
            if (s.mapped) {
                fn.UnmapMemory(d_.device, s.buffer_memory);
            }
            if (s.buffer) {
                fn.DestroyBuffer(d_.device, s.buffer, nullptr);
            }
            if (s.buffer_memory) {
                fn.FreeMemory(d_.device, s.buffer_memory, nullptr);
            }
            if (s.image) {
                fn.DestroyImage(d_.device, s.image, nullptr);
            }
            if (s.image_memory) {
                fn.FreeMemory(d_.device, s.image_memory, nullptr);
            }
            if (s.texture) {
                gl::DeleteTextures(1, &s.texture);
            }
            s = Slot{};
        }
        width_ = height_ = 0;
    }

    VkShareDevice d_;
    VkPhysicalDeviceMemoryProperties memory_{};
    std::array<Slot, SLOTS> slots_{};
};

} // namespace

std::unique_ptr<VkShare> MakeCopy(const VkShareDevice& d) {
    return std::make_unique<CopyShare>(d);
}

} // namespace vkshare

bool VkShareDevice::Has(const char* extension) const {
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

const char* VkShare::Name() const {
    switch (Kind()) {
    case VkShareKind::HardwareBuffer:
        return "hardware buffer";
    case VkShareKind::MemoryObject:
        return "shared memory";
    case VkShareKind::Copy:
        break;
    }
    return "copy";
}

std::vector<const char*> VkShare::WantedExtensions() {
#if defined(__ANDROID__)
    return {"VK_KHR_external_memory", "VK_ANDROID_external_memory_android_hardware_buffer",
            "VK_EXT_queue_family_foreign", "VK_KHR_external_semaphore",
            "VK_KHR_external_semaphore_fd", "VK_KHR_sampler_ycbcr_conversion",
            "VK_KHR_dedicated_allocation", "VK_KHR_get_memory_requirements2",
            "VK_KHR_maintenance1", "VK_KHR_bind_memory2"};
#elif defined(_WIN32)
    return {"VK_KHR_external_memory", "VK_KHR_external_memory_win32", "VK_KHR_external_semaphore",
            "VK_KHR_external_semaphore_win32", "VK_KHR_dedicated_allocation",
            "VK_KHR_get_memory_requirements2"};
#else
    return {"VK_KHR_external_memory", "VK_KHR_external_memory_fd", "VK_KHR_external_semaphore",
            "VK_KHR_external_semaphore_fd", "VK_KHR_dedicated_allocation",
            "VK_KHR_get_memory_requirements2"};
#endif
}

bool VkShare::GlDeviceUuid(gl::ProcLoader loader, std::uint8_t uuid[VK_UUID_SIZE]) {
    constexpr gl::Enum DEVICE_UUID = 0x9597;
    if (!vkshare::HasGlExtension("GL_EXT_memory_object")) {
        return false;
    }
    using GetUnsignedBytei = void (*)(gl::Enum, gl::UInt, std::uint8_t*);
    auto get = reinterpret_cast<GetUnsignedBytei>(loader("glGetUnsignedBytei_vEXT"));
    if (!get) {
        return false;
    }
    std::memset(uuid, 0, VK_UUID_SIZE);
    while (gl::GetError() != 0) {
    }
    get(DEVICE_UUID, 0, uuid);
    if (gl::GetError() != 0) {
        return false;
    }
    return std::any_of(uuid, uuid + VK_UUID_SIZE, [](std::uint8_t b) { return b != 0; });
}

std::unique_ptr<VkShare> VkShare::Create(const VkShareDevice& device, gl::ProcLoader loader, bool gles) {
    std::unique_ptr<VkShare> share;
#if defined(__ANDROID__)
    share = vkshare::MakeHardwareBuffer(device, loader);
#else
    share = vkshare::MakeMemoryObject(device, loader, gles);
#endif
    (void)gles;
    if (!share) {
        share = vkshare::MakeCopy(device);
    }
    return share;
}

} // namespace den::gfx
