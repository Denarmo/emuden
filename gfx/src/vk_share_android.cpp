// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Android: each slot is an AHardwareBuffer, bound to a Vulkan image
// (VK_ANDROID_external_memory_android_hardware_buffer) and to a GLES texture through an
// EGLImage. GL waits for Vulkan through a sync fd turned into an EGL native fence.

#if defined(__ANDROID__)

#define VK_USE_PLATFORM_ANDROID_KHR

#include <array>
#include <cstring>
#include <type_traits>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/hardware_buffer.h>
#include <unistd.h>

#include "den/base/log.h"
#include "vk_share_common.h"

namespace den::gfx::vkshare {

namespace {

constexpr gl::Enum TEXTURE_BINDING_2D = 0x8069;

struct EglCalls {
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC GetNativeClientBuffer = nullptr;
    PFNEGLCREATEIMAGEKHRPROC CreateImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC DestroyImage = nullptr;
    PFNEGLCREATESYNCKHRPROC CreateSync = nullptr;
    PFNEGLDESTROYSYNCKHRPROC DestroySync = nullptr;
    PFNEGLWAITSYNCKHRPROC WaitSync = nullptr;
    void (*ImageTargetTexture2D)(gl::Enum, void*) = nullptr;

    bool Load() {
        const auto get = [](auto& f, const char* name) {
            f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(eglGetProcAddress(name));
            return f != nullptr;
        };
        bool ok = get(GetNativeClientBuffer, "eglGetNativeClientBufferANDROID");
        ok = get(CreateImage, "eglCreateImageKHR") && ok;
        ok = get(DestroyImage, "eglDestroyImageKHR") && ok;
        ok = get(ImageTargetTexture2D, "glEGLImageTargetTexture2DOES") && ok;
        // A GPU wait needs these three; without them the CPU waits for the fence.
        get(CreateSync, "eglCreateSyncKHR");
        get(DestroySync, "eglDestroySyncKHR");
        get(WaitSync, "eglWaitSyncKHR");
        return ok;
    }
};

class HardwareBufferShare final : public VkShare {
public:
    HardwareBufferShare(const VkShareDevice& d, const EglCalls& egl, bool sync_fd)
        : d_(d), egl_(egl), sync_fd_(sync_fd), display_(eglGetCurrentDisplay()) {
        d_.fn->GetPhysicalDeviceMemoryProperties(d_.gpu, &memory_);
        foreign_ = d_.Has("VK_EXT_queue_family_foreign") ? VK_QUEUE_FAMILY_FOREIGN_EXT
                                                          : VK_QUEUE_FAMILY_EXTERNAL;
    }
    ~HardwareBufferShare() override {
        Free();
    }

    VkShareKind Kind() const override {
        return VkShareKind::HardwareBuffer;
    }

    bool Resize(unsigned width, unsigned height) override {
        Free();
        for (Slot& slot : slots_) {
            if (!MakeSlot(slot, width, height)) {
                Free();
                return false;
            }
        }
        width_ = width;
        height_ = height;
        return true;
    }

    VkImage Image(int slot) const override {
        return slots_[slot].image;
    }

    void RecordBeginWrite(VkCommandBuffer cmd, int slot) override {
        RecordDiscardToTransferDst(*d_.fn, cmd, slots_[slot].image);
    }

    void RecordEndWrite(VkCommandBuffer cmd, int slot, unsigned, unsigned) override {
        Barrier b;
        b.from = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.to = VK_IMAGE_LAYOUT_GENERAL;
        b.src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        b.dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        b.src_family = d_.queue_family;
        b.dst_family = foreign_;
        RecordBarrier(*d_.fn, cmd, slots_[slot].image, b);
    }

    VkSemaphore SignalSemaphore(int slot) override {
        return sync_fd_ ? slots_[slot].semaphore : VK_NULL_HANDLE;
    }

    void Published(int slot, VkFence fence, unsigned, unsigned) override {
        if (sync_fd_ && GpuWait(slots_[slot].semaphore)) {
            return;
        }
        d_.fn->WaitForFences(d_.device, 1, &fence, VK_TRUE, UINT64_MAX);
    }

    Texture GlTexture(int slot) const override {
        return Texture{slots_[slot].texture, static_cast<int>(width_), static_cast<int>(height_)};
    }

private:
    struct Slot {
        AHardwareBuffer* buffer = nullptr;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        EGLImageKHR egl_image = EGL_NO_IMAGE_KHR;
        gl::UInt texture = 0;
    };

    // Exporting the semaphore's payload as a sync fd also unsignals it for the next frame.
    bool GpuWait(VkSemaphore semaphore) {
        VkSemaphoreGetFdInfoKHR info{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
        info.semaphore = semaphore;
        info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        int fd = -1;
        if (reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(d_.fn->GetSemaphoreFdKHR)(d_.device, &info, &fd) !=
            VK_SUCCESS) {
            return false;
        }
        if (fd < 0) {
            return true; // already signalled
        }
        const EGLint attributes[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd, EGL_NONE};
        EGLSyncKHR sync = egl_.CreateSync(display_, EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
        if (sync == EGL_NO_SYNC_KHR) {
            close(fd);
            return false;
        }
        egl_.WaitSync(display_, sync, 0); // EGL owns the fd now
        egl_.DestroySync(display_, sync);
        return true;
    }

    bool MakeSlot(Slot& s, unsigned width, unsigned height) {
        const vk::Functions& fn = *d_.fn;
        AHardwareBuffer_Desc desc{};
        desc.width = width;
        desc.height = height;
        desc.layers = 1;
        desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
        if (AHardwareBuffer_allocate(&desc, &s.buffer) != 0) {
            log::Warn("gfx", "no {}x{} hardware buffer", width, height);
            return false;
        }
        VkAndroidHardwareBufferFormatPropertiesANDROID format{
            VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
        VkAndroidHardwareBufferPropertiesANDROID props{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
        props.pNext = &format;
        if (reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
                fn.GetAndroidHardwareBufferPropertiesANDROID)(d_.device, s.buffer, &props) != VK_SUCCESS ||
            format.format != FORMAT) {
            return false;
        }
        VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
        s.image = CreateImage(d_, width, height, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              &external);
        if (!s.image) {
            return false;
        }
        int type = vk::FindMemoryType(memory_, props.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type < 0) {
            type = vk::FindMemoryType(memory_, props.memoryTypeBits, 0);
        }
        VkImportAndroidHardwareBufferInfoANDROID import{VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
        import.buffer = s.buffer;
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.pNext = &import;
        dedicated.image = s.image;
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.pNext = &dedicated;
        info.allocationSize = props.allocationSize;
        info.memoryTypeIndex = static_cast<std::uint32_t>(type);
        if (type < 0 || fn.AllocateMemory(d_.device, &info, nullptr, &s.memory) != VK_SUCCESS ||
            fn.BindImageMemory(d_.device, s.image, s.memory, 0) != VK_SUCCESS) {
            return false;
        }
        if (sync_fd_) {
            VkExportSemaphoreCreateInfo exported{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
            exported.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            semaphore.pNext = &exported;
            if (fn.CreateSemaphore(d_.device, &semaphore, nullptr, &s.semaphore) != VK_SUCCESS) {
                return false;
            }
        }

        EGLClientBuffer client = egl_.GetNativeClientBuffer(s.buffer);
        const EGLint attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        s.egl_image = egl_.CreateImage(display_, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client, attributes);
        if (s.egl_image == EGL_NO_IMAGE_KHR) {
            log::Warn("gfx", "no EGLImage for the hardware buffer (0x{:x})", eglGetError());
            return false;
        }
        while (gl::GetError() != 0) {
            // earlier errors are not ours
        }
        gl::Int old = 0;
        gl::GetIntegerv(TEXTURE_BINDING_2D, &old);
        gl::GenTextures(1, &s.texture);
        gl::BindTexture(gl::TEXTURE_2D, s.texture);
        egl_.ImageTargetTexture2D(gl::TEXTURE_2D, s.egl_image);
        SetOpaqueTextureParams();
        gl::BindTexture(gl::TEXTURE_2D, static_cast<gl::UInt>(old));
        return gl::GetError() == 0;
    }

    void Free() {
        const vk::Functions& fn = *d_.fn;
        for (Slot& s : slots_) {
            if (s.texture) {
                gl::DeleteTextures(1, &s.texture);
            }
            if (s.egl_image != EGL_NO_IMAGE_KHR) {
                egl_.DestroyImage(display_, s.egl_image);
            }
            if (s.semaphore) {
                fn.DestroySemaphore(d_.device, s.semaphore, nullptr);
            }
            if (s.image) {
                fn.DestroyImage(d_.device, s.image, nullptr);
            }
            if (s.memory) {
                fn.FreeMemory(d_.device, s.memory, nullptr);
            }
            if (s.buffer) {
                AHardwareBuffer_release(s.buffer);
            }
            s = Slot{};
        }
        width_ = height_ = 0;
    }

    VkShareDevice d_;
    EglCalls egl_;
    bool sync_fd_;
    EGLDisplay display_;
    std::uint32_t foreign_ = VK_QUEUE_FAMILY_EXTERNAL;
    VkPhysicalDeviceMemoryProperties memory_{};
    std::array<Slot, SLOTS> slots_{};
};

// Whether a semaphore can be exported as a sync fd, which EGL can wait for.
bool CanExportSyncFd(const VkShareDevice& d) {
    if (!d.Has("VK_KHR_external_semaphore_fd") || !d.fn->GetSemaphoreFdKHR ||
        !d.fn->GetPhysicalDeviceExternalSemaphoreProperties) {
        return false;
    }
    VkPhysicalDeviceExternalSemaphoreInfo info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};
    info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkExternalSemaphoreProperties props{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
    d.fn->GetPhysicalDeviceExternalSemaphoreProperties(d.gpu, &info, &props);
    return (props.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) != 0;
}

} // namespace

std::unique_ptr<VkShare> MakeHardwareBuffer(const VkShareDevice& d, gl::ProcLoader) {
    if (!d.Has("VK_ANDROID_external_memory_android_hardware_buffer") ||
        !d.fn->GetAndroidHardwareBufferPropertiesANDROID) {
        return nullptr;
    }
    EglCalls egl;
    if (eglGetCurrentDisplay() == EGL_NO_DISPLAY || !egl.Load()) {
        return nullptr;
    }
    const char* egl_extensions = eglQueryString(eglGetCurrentDisplay(), EGL_EXTENSIONS);
    const bool native_fence = egl_extensions && std::strstr(egl_extensions, "EGL_ANDROID_native_fence_sync") &&
                              std::strstr(egl_extensions, "EGL_KHR_wait_sync");
    const bool sync_fd = native_fence && CanExportSyncFd(d) && egl.CreateSync && egl.WaitSync && egl.DestroySync;
    if (!sync_fd) {
        log::Info("gfx", "no sync fd for the Vulkan picture: the CPU waits for each frame");
    }
    return std::make_unique<HardwareBufferShare>(d, egl, sync_fd);
}

} // namespace den::gfx::vkshare

#endif
