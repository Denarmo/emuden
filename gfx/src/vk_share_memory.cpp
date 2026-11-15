// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Windows and Linux: Vulkan memory exported as an NT handle or an fd, imported by GL with
// GL_EXT_memory_object, and a semaphore GL waits for (GL_EXT_semaphore).

#if !defined(__ANDROID__)

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <array>
#include <type_traits>

#include "den/base/log.h"
#include "vk_share_common.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace den::gfx::vkshare {

namespace {

constexpr gl::Enum DEDICATED_MEMORY_OBJECT = 0x9581, LAYOUT_SHADER_READ_ONLY = 0x9591,
                   TEXTURE_BINDING_2D = 0x8069;
#if defined(_WIN32)
constexpr gl::Enum HANDLE_TYPE = 0x9587; // GL_HANDLE_TYPE_OPAQUE_WIN32_EXT
constexpr auto VK_MEMORY_HANDLE = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
constexpr auto VK_SEMAPHORE_HANDLE = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
constexpr gl::Enum HANDLE_TYPE = 0x9586; // GL_HANDLE_TYPE_OPAQUE_FD_EXT
constexpr auto VK_MEMORY_HANDLE = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
constexpr auto VK_SEMAPHORE_HANDLE = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

struct GlCalls {
    void (*CreateMemoryObjects)(gl::Sizei, gl::UInt*);
    void (*DeleteMemoryObjects)(gl::Sizei, const gl::UInt*);
    void (*MemoryObjectParameteriv)(gl::UInt, gl::Enum, const gl::Int*);
    void (*TexStorageMem2D)(gl::Enum, gl::Sizei, gl::Enum, gl::Sizei, gl::Sizei, gl::UInt, std::uint64_t);
    void (*GenSemaphores)(gl::Sizei, gl::UInt*);
    void (*DeleteSemaphores)(gl::Sizei, const gl::UInt*);
    void (*WaitSemaphore)(gl::UInt, gl::UInt, const gl::UInt*, gl::UInt, const gl::UInt*, const gl::Enum*);
#if defined(_WIN32)
    void (*ImportMemory)(gl::UInt, std::uint64_t, gl::Enum, void*);
    void (*ImportSemaphore)(gl::UInt, gl::Enum, void*);
#else
    void (*ImportMemory)(gl::UInt, std::uint64_t, gl::Enum, gl::Int);
    void (*ImportSemaphore)(gl::UInt, gl::Enum, gl::Int);
#endif

    bool Load(gl::ProcLoader loader) {
        const auto get = [&](auto& f, const char* name) {
            f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(loader(name));
            return f != nullptr;
        };
        bool ok = get(CreateMemoryObjects, "glCreateMemoryObjectsEXT");
        ok = get(DeleteMemoryObjects, "glDeleteMemoryObjectsEXT") && ok;
        ok = get(MemoryObjectParameteriv, "glMemoryObjectParameterivEXT") && ok;
        ok = get(TexStorageMem2D, "glTexStorageMem2DEXT") && ok;
        ok = get(GenSemaphores, "glGenSemaphoresEXT") && ok;
        ok = get(DeleteSemaphores, "glDeleteSemaphoresEXT") && ok;
        ok = get(WaitSemaphore, "glWaitSemaphoreEXT") && ok;
#if defined(_WIN32)
        ok = get(ImportMemory, "glImportMemoryWin32HandleEXT") && ok;
        ok = get(ImportSemaphore, "glImportSemaphoreWin32HandleEXT") && ok;
#else
        ok = get(ImportMemory, "glImportMemoryFdEXT") && ok;
        ok = get(ImportSemaphore, "glImportSemaphoreFdEXT") && ok;
#endif
        return ok;
    }
};

class MemoryShare final : public VkShare {
public:
    MemoryShare(const VkShareDevice& d, const GlCalls& gl) : d_(d), gl_(gl) {
        d_.fn->GetPhysicalDeviceMemoryProperties(d_.gpu, &memory_);
    }
    ~MemoryShare() override {
        Free();
    }

    VkShareKind Kind() const override {
        return VkShareKind::MemoryObject;
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
        b.to = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        b.dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        b.src_family = d_.queue_family;
        b.dst_family = VK_QUEUE_FAMILY_EXTERNAL;
        RecordBarrier(*d_.fn, cmd, slots_[slot].image, b);
    }

    VkSemaphore SignalSemaphore(int slot) override {
        return slots_[slot].semaphore;
    }

    void Published(int slot, VkFence, unsigned, unsigned) override {
        // A GPU wait: GL work issued after this runs once Vulkan has written the slot.
        const Slot& s = slots_[slot];
        const gl::Enum layout = LAYOUT_SHADER_READ_ONLY;
        gl_.WaitSemaphore(s.gl_semaphore, 0, nullptr, 1, &s.texture, &layout);
    }

    Texture GlTexture(int slot) const override {
        return Texture{slots_[slot].texture, static_cast<int>(width_), static_cast<int>(height_)};
    }

private:
    struct Slot {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        gl::UInt memory_object = 0, texture = 0, gl_semaphore = 0;
#if defined(_WIN32)
        HANDLE memory_handle = nullptr, semaphore_handle = nullptr;
#endif
    };

    bool MakeSlot(Slot& s, unsigned width, unsigned height) {
        const vk::Functions& fn = *d_.fn;
        VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        external.handleTypes = VK_MEMORY_HANDLE;
        s.image = CreateImage(d_, width, height,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                              &external);
        if (!s.image) {
            return false;
        }
        VkMemoryRequirements req{};
        fn.GetImageMemoryRequirements(d_.device, s.image, &req);
        const int type = vk::FindMemoryType(memory_, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type < 0) {
            return false;
        }
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.image = s.image;
        VkExportMemoryAllocateInfo exported{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
        exported.pNext = &dedicated;
        exported.handleTypes = VK_MEMORY_HANDLE;
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.pNext = &exported;
        info.allocationSize = req.size;
        info.memoryTypeIndex = static_cast<std::uint32_t>(type);
        if (fn.AllocateMemory(d_.device, &info, nullptr, &s.memory) != VK_SUCCESS ||
            fn.BindImageMemory(d_.device, s.image, s.memory, 0) != VK_SUCCESS) {
            return false;
        }
        VkExportSemaphoreCreateInfo semaphore_export{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
        semaphore_export.handleTypes = VK_SEMAPHORE_HANDLE;
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        semaphore_info.pNext = &semaphore_export;
        if (fn.CreateSemaphore(d_.device, &semaphore_info, nullptr, &s.semaphore) != VK_SUCCESS) {
            return false;
        }

        // GL keeps its own reference to what it imports.
        while (gl::GetError() != 0) {
            // earlier errors are not ours
        }
        const auto gl_ok = [](const char* step) {
            const gl::Enum error = gl::GetError();
            if (error != 0) {
                log::Warn("gfx", "GL could not share Vulkan memory: {} (0x{:x})", step, error);
            }
            return error == 0;
        };
        gl_.CreateMemoryObjects(1, &s.memory_object);
        const gl::Int dedicated_flag = 1;
        gl_.MemoryObjectParameteriv(s.memory_object, DEDICATED_MEMORY_OBJECT, &dedicated_flag);
        gl_.GenSemaphores(1, &s.gl_semaphore);
#if defined(_WIN32)
        VkMemoryGetWin32HandleInfoKHR get{VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
        get.memory = s.memory;
        get.handleType = VK_MEMORY_HANDLE;
        HANDLE handle = nullptr;
        if (reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(fn.GetMemoryWin32HandleKHR)(d_.device, &get, &handle) !=
            VK_SUCCESS) {
            return false;
        }
        // GL may import on a thread of its own: the handles stay open as long as the slot.
        s.memory_handle = handle;
        gl_.ImportMemory(s.memory_object, req.size, HANDLE_TYPE, handle);
        if (!gl_ok("memory import")) {
            return false;
        }
        VkSemaphoreGetWin32HandleInfoKHR get_semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR};
        get_semaphore.semaphore = s.semaphore;
        get_semaphore.handleType = VK_SEMAPHORE_HANDLE;
        HANDLE semaphore_handle = nullptr;
        if (reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(fn.GetSemaphoreWin32HandleKHR)(
                d_.device, &get_semaphore, &semaphore_handle) != VK_SUCCESS) {
            return false;
        }
        s.semaphore_handle = semaphore_handle;
        gl_.ImportSemaphore(s.gl_semaphore, HANDLE_TYPE, semaphore_handle);
        if (!gl_ok("semaphore import")) {
            return false;
        }
#else
        VkMemoryGetFdInfoKHR get{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
        get.memory = s.memory;
        get.handleType = VK_MEMORY_HANDLE;
        int fd = -1;
        if (reinterpret_cast<PFN_vkGetMemoryFdKHR>(fn.GetMemoryFdKHR)(d_.device, &get, &fd) != VK_SUCCESS) {
            return false;
        }
        gl_.ImportMemory(s.memory_object, req.size, HANDLE_TYPE, fd); // GL owns the fd now
        if (!gl_ok("memory import")) {
            return false;
        }
        VkSemaphoreGetFdInfoKHR get_semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
        get_semaphore.semaphore = s.semaphore;
        get_semaphore.handleType = VK_SEMAPHORE_HANDLE;
        int semaphore_fd = -1;
        if (reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(fn.GetSemaphoreFdKHR)(d_.device, &get_semaphore,
                                                                             &semaphore_fd) != VK_SUCCESS) {
            return false;
        }
        gl_.ImportSemaphore(s.gl_semaphore, HANDLE_TYPE, semaphore_fd);
        if (!gl_ok("semaphore import")) {
            return false;
        }
#endif
        gl::Int old = 0;
        gl::GetIntegerv(TEXTURE_BINDING_2D, &old);
        gl::GenTextures(1, &s.texture);
        gl::BindTexture(gl::TEXTURE_2D, s.texture);
        gl_.TexStorageMem2D(gl::TEXTURE_2D, 1, gl::RGBA8, static_cast<gl::Sizei>(width),
                            static_cast<gl::Sizei>(height), s.memory_object, 0);
        const bool stored = gl_ok("texture storage");
        SetOpaqueTextureParams();
        gl::BindTexture(gl::TEXTURE_2D, static_cast<gl::UInt>(old));
        return stored && gl_ok("texture parameters");
        return true;
    }

    void Free() {
        const vk::Functions& fn = *d_.fn;
        for (Slot& s : slots_) {
            if (s.texture) {
                gl::DeleteTextures(1, &s.texture);
            }
            if (s.memory_object) {
                gl_.DeleteMemoryObjects(1, &s.memory_object);
            }
            if (s.gl_semaphore) {
                gl_.DeleteSemaphores(1, &s.gl_semaphore);
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
#if defined(_WIN32)
            if (s.memory_handle) {
                CloseHandle(s.memory_handle);
            }
            if (s.semaphore_handle) {
                CloseHandle(s.semaphore_handle);
            }
#endif
            s = Slot{};
        }
        width_ = height_ = 0;
    }

    VkShareDevice d_;
    GlCalls gl_;
    VkPhysicalDeviceMemoryProperties memory_{};
    std::array<Slot, SLOTS> slots_{};
};

} // namespace

std::unique_ptr<VkShare> MakeMemoryObject(const VkShareDevice& d, gl::ProcLoader loader, bool gles) {
#if defined(_WIN32)
    const char* const vk_memory = "VK_KHR_external_memory_win32";
    const char* const vk_semaphore = "VK_KHR_external_semaphore_win32";
    const char* const gl_memory = "GL_EXT_memory_object_win32";
    const char* const gl_semaphore = "GL_EXT_semaphore_win32";
    const bool have_calls = d.fn->GetMemoryWin32HandleKHR && d.fn->GetSemaphoreWin32HandleKHR;
#else
    const char* const vk_memory = "VK_KHR_external_memory_fd";
    const char* const vk_semaphore = "VK_KHR_external_semaphore_fd";
    const char* const gl_memory = "GL_EXT_memory_object_fd";
    const char* const gl_semaphore = "GL_EXT_semaphore_fd";
    const bool have_calls = d.fn->GetMemoryFdKHR && d.fn->GetSemaphoreFdKHR;
#endif
    (void)gles;
    if (!d.Has(vk_memory) || !d.Has(vk_semaphore) || !have_calls) {
        return nullptr;
    }
    if (!HasGlExtension("GL_EXT_memory_object") || !HasGlExtension("GL_EXT_semaphore") ||
        !HasGlExtension(gl_memory) || !HasGlExtension(gl_semaphore)) {
        log::Info("gfx", "GL has no {} / {}: the Vulkan picture is copied", gl_memory, gl_semaphore);
        return nullptr;
    }
    GlCalls calls{};
    if (!calls.Load(loader)) {
        return nullptr;
    }
    return std::make_unique<MemoryShare>(d, calls);
}

} // namespace den::gfx::vkshare

#endif
