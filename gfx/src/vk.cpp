// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

#include "den/gfx/vk.h"

namespace den::vk {

bool Functions::LoadGlobal(PFN_vkGetInstanceProcAddr get_instance_proc_addr) {
    GetInstanceProcAddr = get_instance_proc_addr;
    if (!GetInstanceProcAddr) {
        return false;
    }
    CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
        GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    EnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    // Null on a Vulkan 1.0 loader.
    EnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    return CreateInstance && EnumerateInstanceExtensionProperties;
}

bool Functions::LoadInstance(VkInstance instance) {
    bool ok = true;
#define DEN_VK_LOAD(name)                                                                         \
    name = reinterpret_cast<PFN_vk##name>(GetInstanceProcAddr(instance, "vk" #name));              \
    ok = ok && name != nullptr;
    DEN_VK_INSTANCE_FUNCTIONS(DEN_VK_LOAD)
#undef DEN_VK_LOAD
    if (!GetPhysicalDeviceProperties2) {
        GetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            GetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
    }
    return ok || (GetDeviceProcAddr && CreateDevice && EnumeratePhysicalDevices);
}

bool Functions::LoadDevice(VkDevice device) {
    bool ok = true;
#define DEN_VK_LOAD(name)                                                                         \
    name = reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(device, "vk" #name));                  \
    ok = ok && name != nullptr;
    DEN_VK_DEVICE_FUNCTIONS(DEN_VK_LOAD)
#undef DEN_VK_LOAD
#define DEN_VK_LOAD_OPTIONAL(name) name = GetDeviceProcAddr(device, "vk" #name);
    DEN_VK_OPTIONAL_DEVICE_FUNCTIONS(DEN_VK_LOAD_OPTIONAL)
#undef DEN_VK_LOAD_OPTIONAL
    return ok;
}

int FindMemoryType(const VkPhysicalDeviceMemoryProperties& memory, std::uint32_t bits,
                   VkMemoryPropertyFlags flags) {
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace den::vk
