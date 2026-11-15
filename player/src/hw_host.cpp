// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hw_host.h"

#include "den/base/log.h"

namespace den::player {

RendererChoice ParseRendererChoice(const std::string& text) {
    if (text == "OpenGL") {
        return RendererChoice::OpenGL;
    }
    if (text == "Vulkan") {
        return RendererChoice::Vulkan;
    }
    return RendererChoice::Auto;
}

HwRenderRequest::Api HwHost::PreferredApi() const {
    // Auto keeps what cores were always told; they ask for Vulkan themselves if they want it.
    if (choice_ == RendererChoice::Vulkan && vulkan_.Available()) {
        return HwRenderRequest::Api::Vulkan;
    }
    return gl_.PreferredApi();
}

bool HwHost::Accept(const HwRenderRequest& request) {
    Shutdown();
    if (request.api == HwRenderRequest::Api::Vulkan) {
        if (choice_ == RendererChoice::OpenGL || !vulkan_.Accept(request)) {
            log::Info("player", "the core's Vulkan request is refused{}",
                      choice_ == RendererChoice::OpenGL ? " (OpenGL is chosen)" : "");
            vulkan_refused_ = choice_ != RendererChoice::OpenGL;
            return false;
        }
        vulkan_active_ = true;
        return true;
    }
    if (!gl_.Accept(request)) {
        return false;
    }
    gl_active_ = true;
    return true;
}

bool HwHost::MakeCurrent() {
    return vulkan_active_ || (gl_active_ && gl_.MakeCurrent());
}

void HwHost::Release() {
    if (gl_active_) {
        gl_.Release();
    }
}

void* HwHost::GetProcAddress(const char* name) {
    return gl_active_ ? gl_.GetProcAddress(name) : nullptr;
}

std::uintptr_t HwHost::CurrentFramebuffer() {
    return gl_active_ ? gl_.CurrentFramebuffer() : 0;
}

bool HwHost::ResizeTarget(unsigned width, unsigned height) {
    if (vulkan_active_) {
        return vulkan_.ResizeTarget(width, height);
    }
    return gl_active_ && gl_.ResizeTarget(width, height);
}

void HwHost::Present(unsigned width, unsigned height) {
    if (vulkan_active_) {
        vulkan_.Present(width, height);
    } else if (gl_active_) {
        gl_.Present(width, height);
    }
}

bool HwHost::ReadPicture(unsigned* width, unsigned* height, std::vector<std::uint8_t>* rgba) {
    if (vulkan_active_) {
        return vulkan_.ReadPicture(width, height, rgba);
    }
    return gl_active_ && gl_.ReadPicture(width, height, rgba);
}

void HwHost::Shutdown() {
    if (vulkan_active_) {
        vulkan_.Shutdown();
    }
    if (gl_active_) {
        gl_.Shutdown();
    }
    vulkan_active_ = gl_active_ = false;
}

bool HwHost::SetVulkanNegotiation(const void* negotiation) {
    return vulkan_active_ &&
           vulkan_.SetNegotiation(static_cast<const retro_hw_render_context_negotiation_interface_vulkan*>(negotiation));
}

const void* HwHost::VulkanInterface() {
    return vulkan_active_ ? vulkan_.Interface() : nullptr;
}

std::string HwHost::InUse() const {
    if (vulkan_active_) {
        const std::string share = vulkan_.ShareName();
        return share.empty() ? "Vulkan" : "Vulkan (" + share + ")";
    }
    return gl_active_ ? "OpenGL" : "";
}

} // namespace den::player
