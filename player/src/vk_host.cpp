// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The libretro Vulkan interface and the frame: the core's image is copied into a shared slot
// that GL then draws.

#include <algorithm>
#include <cstring>

#include "den/base/log.h"
#include "den/gfx/gl.h"
#include "vk_host.h"

namespace den::player {

namespace {

VulkanHost* Self(void* handle) {
    return static_cast<VulkanHost*>(handle);
}

} // namespace

void VulkanHost::FillInterface() {
    iface_ = retro_hw_render_interface_vulkan{};
    iface_.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    iface_.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    iface_.handle = this;
    iface_.instance = instance_;
    iface_.gpu = gpu_;
    iface_.device = device_;
    iface_.get_device_proc_addr = fn_.GetDeviceProcAddr;
    iface_.get_instance_proc_addr = fn_.GetInstanceProcAddr;
    iface_.queue = queue_;
    iface_.queue_index = queue_family_;
    iface_.set_image = SetImage;
    iface_.get_sync_index = GetSyncIndex;
    iface_.get_sync_index_mask = GetSyncIndexMask;
    iface_.set_command_buffers = SetCommandBuffers;
    iface_.wait_sync_index = WaitSyncIndex;
    iface_.lock_queue = LockQueue;
    iface_.unlock_queue = UnlockQueue;
    iface_.set_signal_semaphore = SetSignalSemaphore;
}

void VulkanHost::SetImage(void* handle, const retro_vulkan_image* image, std::uint32_t num_semaphores,
                          const VkSemaphore* semaphores, std::uint32_t src_queue_family) {
    VulkanHost* self = Self(handle);
    self->image_ = image;
    self->wait_semaphores_.assign(semaphores, semaphores + num_semaphores);
    self->image_family_ = src_queue_family;
}

std::uint32_t VulkanHost::GetSyncIndex(void* handle) {
    return static_cast<std::uint32_t>(Self(handle)->slot_);
}

std::uint32_t VulkanHost::GetSyncIndexMask(void*) {
    return (1u << SLOTS) - 1;
}

void VulkanHost::SetCommandBuffers(void* handle, std::uint32_t count, const VkCommandBuffer* buffers) {
    Self(handle)->core_commands_.assign(buffers, buffers + count);
}

void VulkanHost::WaitSyncIndex(void* handle) {
    VulkanHost* self = Self(handle);
    self->fn_.WaitForFences(self->device_, 1, &self->fences_[self->slot_], VK_TRUE, UINT64_MAX);
}

void VulkanHost::LockQueue(void* handle) {
    Self(handle)->queue_mutex_.lock();
}

void VulkanHost::UnlockQueue(void* handle) {
    Self(handle)->queue_mutex_.unlock();
}

void VulkanHost::SetSignalSemaphore(void* handle, VkSemaphore semaphore) {
    Self(handle)->signal_semaphore_ = semaphore;
}

bool VulkanHost::ResizeTarget(unsigned width, unsigned height) {
    if (!device_ && !MakeDevice()) {
        Shutdown();
        return false;
    }
    if (share_->Width() >= width && share_->Height() >= height) {
        return true;
    }
    width = std::max(width, share_->Width());
    height = std::max(height, share_->Height());
    {
        std::lock_guard lock(queue_mutex_);
        fn_.QueueWaitIdle(queue_);
    }
    if (!share_->Resize(width, height)) {
        if (share_->Kind() == gfx::VkShareKind::Copy) {
            log::Error("player", "no {}x{} picture for the Vulkan core", width, height);
            return false;
        }
        log::Warn("player", "the Vulkan picture cannot be shared ({}): it is copied", share_->Name());
        gfx::VkShareDevice share;
        share.fn = &fn_;
        share.instance = instance_;
        share.gpu = gpu_;
        share.device = device_;
        share.queue_family = queue_family_;
        // No extensions: only the copy is left.
        share_ = gfx::VkShare::Create(share, [](const char*) -> void* { return nullptr; }, gles_);
        if (!share_ || !share_->Resize(width, height)) {
            return false;
        }
    }
    log::Info("player", "Vulkan picture {}x{} via {}", width, height, share_->Name());
    shown_slot_ = -1;
    return true;
}

void VulkanHost::CopyFrame(VkCommandBuffer cmd, int slot, unsigned width, unsigned height) {
    const retro_vulkan_image& src = *image_;
    const VkImage image = src.create_info.image;
    const VkImageSubresourceRange& range = src.create_info.subresourceRange;
    const bool transfer = image_family_ != VK_QUEUE_FAMILY_IGNORED && image_family_ != queue_family_;
    const VkImageLayout read_layout =
        src.image_layout == VK_IMAGE_LAYOUT_GENERAL ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkImageMemoryBarrier acquire{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    acquire.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    acquire.oldLayout = src.image_layout;
    acquire.newLayout = read_layout;
    acquire.srcQueueFamilyIndex = transfer ? image_family_ : VK_QUEUE_FAMILY_IGNORED;
    acquire.dstQueueFamilyIndex = transfer ? queue_family_ : VK_QUEUE_FAMILY_IGNORED;
    acquire.image = image;
    acquire.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, range.baseMipLevel, 1, range.baseArrayLayer, 1};
    fn_.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &acquire);

    share_->RecordBeginWrite(cmd, slot);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, range.baseMipLevel, range.baseArrayLayer, 1};
    blit.srcOffsets[1] = {static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = blit.srcOffsets[1];
    fn_.CmdBlitImage(cmd, image, read_layout, share_->Image(slot), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_NEAREST);
    share_->RecordEndWrite(cmd, slot, width, height);

    // Back to the core as it was: its layout and its queue family.
    VkImageMemoryBarrier release = acquire;
    release.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    release.dstAccessMask = 0;
    release.oldLayout = read_layout;
    release.newLayout = src.image_layout;
    release.srcQueueFamilyIndex = acquire.dstQueueFamilyIndex;
    release.dstQueueFamilyIndex = acquire.srcQueueFamilyIndex;
    fn_.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &release);
}

void VulkanHost::Present(unsigned width, unsigned height) {
    if (!device_ || !image_ || width == 0 || height == 0) {
        return;
    }
    if ((width > share_->Width() || height > share_->Height()) && !ResizeTarget(width, height)) {
        return;
    }
    const int slot = slot_;
    VkCommandBuffer cmd = commands_[slot];
    fn_.WaitForFences(device_, 1, &fences_[slot], VK_TRUE, UINT64_MAX);
    fn_.ResetFences(device_, 1, &fences_[slot]);
    fn_.ResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    fn_.BeginCommandBuffer(cmd, &begin);
    CopyFrame(cmd, slot, width, height);
    fn_.EndCommandBuffer(cmd);

    // The core's own command buffers go first; with them its semaphores are not waited for.
    std::vector<VkCommandBuffer> buffers = core_commands_;
    buffers.push_back(cmd);
    std::vector<VkPipelineStageFlags> stages;
    if (core_commands_.empty()) {
        stages.assign(wait_semaphores_.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    } else {
        wait_semaphores_.clear();
    }
    std::vector<VkSemaphore> signals;
    if (VkSemaphore s = share_->SignalSemaphore(slot)) {
        signals.push_back(s);
    }
    if (signal_semaphore_) {
        signals.push_back(signal_semaphore_);
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = static_cast<std::uint32_t>(wait_semaphores_.size());
    submit.pWaitSemaphores = wait_semaphores_.data();
    submit.pWaitDstStageMask = stages.data();
    submit.commandBufferCount = static_cast<std::uint32_t>(buffers.size());
    submit.pCommandBuffers = buffers.data();
    submit.signalSemaphoreCount = static_cast<std::uint32_t>(signals.size());
    submit.pSignalSemaphores = signals.data();
    VkResult result;
    {
        std::lock_guard lock(queue_mutex_);
        result = fn_.QueueSubmit(queue_, 1, &submit, fences_[slot]);
    }
    // Semaphores and command buffers are used once; the image stays for duplicated frames.
    wait_semaphores_.clear();
    core_commands_.clear();
    signal_semaphore_ = VK_NULL_HANDLE;
    if (result != VK_SUCCESS) {
        log::Error("player", "the Vulkan frame was not submitted ({})", static_cast<int>(result));
        return;
    }
    share_->Published(slot, fences_[slot], width, height);
    shown_slot_ = slot;
    picture_width_ = width;
    picture_height_ = height;
    // The next sync index; waiting for its last frame keeps the GPU at most two frames ahead.
    slot_ = (slot + 1) % SLOTS;
    fn_.WaitForFences(device_, 1, &fences_[slot_], VK_TRUE, UINT64_MAX);
    image_ = nullptr;
}

gfx::Texture VulkanHost::Target() const {
    if (!share_ || shown_slot_ < 0) {
        return gfx::Texture{};
    }
    return share_->GlTexture(shown_slot_);
}

bool VulkanHost::ReadPicture(unsigned* width, unsigned* height, std::vector<std::uint8_t>* rgba) {
    const gfx::Texture texture = Target();
    if (!texture || picture_width_ == 0 || picture_height_ == 0) {
        return false;
    }
    gl::Int old_fbo = 0;
    gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &old_fbo);
    gl::UInt fbo = 0;
    gl::GenFramebuffers(1, &fbo);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, texture.id, 0);
    const bool complete = gl::CheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    if (complete) {
        rgba->resize(static_cast<std::size_t>(picture_width_) * picture_height_ * 4);
        gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
        // Row 0 of the texture is the top of the picture, as Vulkan draws.
        gl::ReadPixels(0, 0, static_cast<gl::Sizei>(picture_width_), static_cast<gl::Sizei>(picture_height_),
                       gl::RGBA, gl::UNSIGNED_BYTE, rgba->data());
        for (std::size_t i = 3; i < rgba->size(); i += 4) {
            (*rgba)[i] = 255;
        }
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, static_cast<gl::UInt>(old_fbo));
    gl::DeleteFramebuffers(1, &fbo);
    if (!complete) {
        return false;
    }
    *width = picture_width_;
    *height = picture_height_;
    return true;
}

} // namespace den::player
