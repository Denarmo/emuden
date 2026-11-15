// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Vulkan device of a core that draws with Vulkan, the libretro interface to it, and each
// frame's copy of the core's image into a picture GL can draw (docs/vulkan.md).

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/hw_render.h"
#include "den/gfx/vk_share.h"
#include "libretro_vulkan.h"

namespace den::player {

class VulkanHost {
public:
    explicit VulkanHost(bool gles) : gles_(gles) {}
    ~VulkanHost();
    VulkanHost(const VulkanHost&) = delete;
    VulkanHost& operator=(const VulkanHost&) = delete;

    /// Whether this device has Vulkan at all (the loader and a GPU). Cached after the first call.
    bool Available();
    bool Accept(const HwRenderRequest& request);
    bool SetNegotiation(const retro_hw_render_context_negotiation_interface_vulkan* negotiation);
    /// Makes the device on the first call; then gives the picture at least this size.
    bool ResizeTarget(unsigned width, unsigned height);
    const retro_hw_render_interface_vulkan* Interface() const {
        return device_ ? &iface_ : nullptr;
    }
    void Present(unsigned width, unsigned height);
    bool ReadPicture(unsigned* width, unsigned* height, std::vector<std::uint8_t>* rgba);
    void Shutdown();

    gfx::Texture Target() const;
    unsigned PictureWidth() const {
        return picture_width_;
    }
    unsigned PictureHeight() const {
        return picture_height_;
    }
    /// How the picture reaches GL ("shared memory", "hardware buffer", "copy"), empty before.
    std::string ShareName() const {
        return share_ ? share_->Name() : "";
    }

private:
    bool LoadLoader();
    bool MakeInstance();
    VkPhysicalDevice PickGpu();
    bool MakeDevice();
    bool MakeFrameObjects();
    void FillInterface();
    std::vector<const char*> SharingExtensions(VkPhysicalDevice gpu) const;
    void CopyFrame(VkCommandBuffer cmd, int slot, unsigned width, unsigned height);

    // Called by the core through retro_hw_render_interface_vulkan and the negotiation wrappers.
    static void SetImage(void* handle, const retro_vulkan_image* image, std::uint32_t num_semaphores,
                         const VkSemaphore* semaphores, std::uint32_t src_queue_family);
    static std::uint32_t GetSyncIndex(void* handle);
    static std::uint32_t GetSyncIndexMask(void* handle);
    static void SetCommandBuffers(void* handle, std::uint32_t count, const VkCommandBuffer* buffers);
    static void WaitSyncIndex(void* handle);
    static void LockQueue(void* handle);
    static void UnlockQueue(void* handle);
    static void SetSignalSemaphore(void* handle, VkSemaphore semaphore);
    static VkInstance CreateInstanceWrapper(void* opaque, const VkInstanceCreateInfo* info);
    static VkDevice CreateDeviceWrapper(VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* info);

    static constexpr int SLOTS = gfx::VkShare::SLOTS;

    bool gles_;
    int available_ = -1;
    bool loader_loaded_ = false;
    HwRenderRequest request_;
    const retro_hw_render_context_negotiation_interface_vulkan* negotiation_ = nullptr;
    bool core_made_device_ = false;

    vk::Functions fn_;
    VkApplicationInfo app_{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    std::vector<std::string> device_extensions_;
    retro_hw_render_interface_vulkan iface_{};
    std::mutex queue_mutex_;

    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, SLOTS> commands_{};
    std::array<VkFence, SLOTS> fences_{};
    std::unique_ptr<gfx::VkShare> share_;
    int slot_ = 0;       // the core's sync index, and the slot the next frame goes to
    int shown_slot_ = -1; // the slot of the picture GL shows

    // What the core handed for the next frame.
    const retro_vulkan_image* image_ = nullptr;
    std::vector<VkSemaphore> wait_semaphores_;
    std::uint32_t image_family_ = VK_QUEUE_FAMILY_IGNORED;
    std::vector<VkCommandBuffer> core_commands_;
    VkSemaphore signal_semaphore_ = VK_NULL_HANDLE;

    unsigned picture_width_ = 0, picture_height_ = 0;
};

} // namespace den::player
