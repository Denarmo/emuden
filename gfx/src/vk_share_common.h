// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the kinds of VkShare have in common.

#pragma once

#include "den/gfx/vk_share.h"

namespace den::gfx::vkshare {

constexpr VkFormat FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
constexpr gl::Enum TEXTURE_SWIZZLE_A = 0x8E45;

struct Barrier {
    VkImageLayout from = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout to = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags src_access = 0;
    VkAccessFlags dst_access = 0;
    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    std::uint32_t src_family = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t dst_family = VK_QUEUE_FAMILY_IGNORED;
};

void RecordBarrier(const vk::Functions& fn, VkCommandBuffer cmd, VkImage image, const Barrier& b);

/// UNDEFINED -> TRANSFER_DST_OPTIMAL, the start of every write.
void RecordDiscardToTransferDst(const vk::Functions& fn, VkCommandBuffer cmd, VkImage image);

/// A 2D colour image of FORMAT with optimal tiling.
VkImage CreateImage(const VkShareDevice& d, unsigned width, unsigned height,
                    VkImageUsageFlags usage, const void* next);

/// Whether the current GL context names this extension.
bool HasGlExtension(const char* name);

/// A plain texture with the picture's alpha read as one: what a core leaves in alpha must not
/// make the picture transparent.
void SetOpaqueTextureParams();

std::unique_ptr<VkShare> MakeCopy(const VkShareDevice& d);
std::unique_ptr<VkShare> MakeMemoryObject(const VkShareDevice& d, gl::ProcLoader loader, bool gles);
std::unique_ptr<VkShare> MakeHardwareBuffer(const VkShareDevice& d, gl::ProcLoader loader);

} // namespace den::gfx::vkshare
