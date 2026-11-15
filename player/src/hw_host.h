// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The hardware rendering a session gives its core: an OpenGL context (HwContext) or a Vulkan
// device (VulkanHost), whichever the core asks for and the renderer choice allows.

#pragma once

#include <memory>
#include <string>

#include "hw_context.h"
#include "vk_host.h"

namespace den::player {

/// The player's renderer setting for a core or a game.
enum class RendererChoice { Auto, OpenGL, Vulkan };

RendererChoice ParseRendererChoice(const std::string& text);

class HwHost final : public HwRenderHost {
public:
    explicit HwHost(bool gles) : gl_(gles), vulkan_(gles) {}

    void SetChoice(RendererChoice choice) {
        choice_ = choice;
    }

    HwRenderRequest::Api PreferredApi() const override;
    bool Accept(const HwRenderRequest& request) override;
    bool MakeCurrent() override;
    void Release() override;
    void* GetProcAddress(const char* name) override;
    std::uintptr_t CurrentFramebuffer() override;
    bool ResizeTarget(unsigned width, unsigned height) override;
    void Present(unsigned width, unsigned height) override;
    bool ReadPicture(unsigned* width, unsigned* height, std::vector<std::uint8_t>* rgba) override;
    void Shutdown() override;
    bool SetVulkanNegotiation(const void* negotiation) override;
    const void* VulkanInterface() override;

    gfx::Texture Target() const {
        return vulkan_active_ ? vulkan_.Target() : gl_.Target();
    }
    unsigned PictureWidth() const {
        return vulkan_active_ ? vulkan_.PictureWidth() : gl_.PictureWidth();
    }
    unsigned PictureHeight() const {
        return vulkan_active_ ? vulkan_.PictureHeight() : gl_.PictureHeight();
    }
    bool BottomLeftOrigin() const {
        return !vulkan_active_ && gl_.BottomLeftOrigin();
    }
    /// What draws the core's picture, for the menu: "OpenGL", "Vulkan (shared memory)", ...
    std::string InUse() const;
    bool VulkanRefused() const {
        return vulkan_refused_;
    }

private:
    RendererChoice choice_ = RendererChoice::Auto;
    HwContext gl_;
    mutable VulkanHost vulkan_;
    bool vulkan_active_ = false;
    bool gl_active_ = false;
    bool vulkan_refused_ = false; // the core asked for Vulkan and did not get it
};

} // namespace den::player
