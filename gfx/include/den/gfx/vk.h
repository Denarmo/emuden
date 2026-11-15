// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Vulkan calls the app makes, loaded at run time from vkGetInstanceProcAddr, so no build
// links the Vulkan loader. Calls of one platform only are kept as untyped pointers here and
// cast where that platform's code uses them.

#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

namespace den::vk {

// clang-format off
#define DEN_VK_INSTANCE_FUNCTIONS(X) \
    X(DestroyInstance) \
    X(EnumeratePhysicalDevices) \
    X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceProperties2) \
    X(GetPhysicalDeviceFeatures) \
    X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceMemoryProperties) \
    X(GetPhysicalDeviceFormatProperties) \
    X(GetPhysicalDeviceImageFormatProperties2) \
    X(GetPhysicalDeviceExternalSemaphoreProperties) \
    X(EnumerateDeviceExtensionProperties) \
    X(CreateDevice) \
    X(GetDeviceProcAddr)

#define DEN_VK_DEVICE_FUNCTIONS(X) \
    X(DestroyDevice) \
    X(GetDeviceQueue) \
    X(DeviceWaitIdle) \
    X(QueueWaitIdle) \
    X(QueueSubmit) \
    X(CreateCommandPool) \
    X(DestroyCommandPool) \
    X(AllocateCommandBuffers) \
    X(ResetCommandBuffer) \
    X(BeginCommandBuffer) \
    X(EndCommandBuffer) \
    X(CmdPipelineBarrier) \
    X(CmdBlitImage) \
    X(CmdCopyImageToBuffer) \
    X(CmdClearColorImage) \
    X(CreateFence) \
    X(DestroyFence) \
    X(WaitForFences) \
    X(ResetFences) \
    X(GetFenceStatus) \
    X(CreateSemaphore) \
    X(DestroySemaphore) \
    X(CreateImage) \
    X(DestroyImage) \
    X(GetImageMemoryRequirements) \
    X(BindImageMemory) \
    X(CreateBuffer) \
    X(DestroyBuffer) \
    X(GetBufferMemoryRequirements) \
    X(BindBufferMemory) \
    X(AllocateMemory) \
    X(FreeMemory) \
    X(MapMemory) \
    X(UnmapMemory) \
    X(InvalidateMappedMemoryRanges)

// Device calls only some platforms have; null when the extension is not there.
#define DEN_VK_OPTIONAL_DEVICE_FUNCTIONS(X) \
    X(GetMemoryWin32HandleKHR) \
    X(GetSemaphoreWin32HandleKHR) \
    X(GetMemoryFdKHR) \
    X(GetSemaphoreFdKHR) \
    X(GetAndroidHardwareBufferPropertiesANDROID)
// clang-format on

struct Functions {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance CreateInstance = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties = nullptr;
    PFN_vkEnumerateInstanceVersion EnumerateInstanceVersion = nullptr;
#define DEN_VK_MEMBER(name) PFN_vk##name name = nullptr;
    DEN_VK_INSTANCE_FUNCTIONS(DEN_VK_MEMBER)
    DEN_VK_DEVICE_FUNCTIONS(DEN_VK_MEMBER)
#undef DEN_VK_MEMBER
#define DEN_VK_OPTIONAL(name) PFN_vkVoidFunction name = nullptr;
    DEN_VK_OPTIONAL_DEVICE_FUNCTIONS(DEN_VK_OPTIONAL)
#undef DEN_VK_OPTIONAL

    /// The calls made before there is an instance. False when the loader lacks one.
    bool LoadGlobal(PFN_vkGetInstanceProcAddr get_instance_proc_addr);
    bool LoadInstance(VkInstance instance);
    bool LoadDevice(VkDevice device);
};

/// The index of a memory type in `bits` with `flags`, or -1.
int FindMemoryType(const VkPhysicalDeviceMemoryProperties& memory, std::uint32_t bits,
                   VkMemoryPropertyFlags flags);

} // namespace den::vk
