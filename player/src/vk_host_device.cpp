// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Making the Vulkan instance and device of a core, by the host or by the core itself through
// the negotiation interface.

#include <algorithm>
#include <cstring>

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

#include "den/base/log.h"
#include "vk_host.h"

namespace den::player {

namespace {

constexpr std::uint32_t MIN_API = VK_MAKE_API_VERSION(0, 1, 1, 0);

void* GlProc(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

std::vector<std::string> DeviceExtensionNames(const vk::Functions& fn, VkPhysicalDevice gpu) {
    std::uint32_t count = 0;
    fn.EnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> props(count);
    fn.EnumerateDeviceExtensionProperties(gpu, nullptr, &count, props.data());
    std::vector<std::string> out;
    for (const VkExtensionProperties& p : props) {
        out.emplace_back(p.extensionName);
    }
    return out;
}

int GraphicsQueueFamily(const vk::Functions& fn, VkPhysicalDevice gpu) {
    std::uint32_t count = 0;
    fn.GetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    fn.GetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
    for (std::uint32_t i = 0; i < count; ++i) {
        const VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((families[i].queueFlags & wanted) == wanted) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

bool VulkanHost::LoadLoader() {
    if (loader_loaded_) {
        return true;
    }
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        log::Info("player", "no Vulkan loader: {}", SDL_GetError());
        return false;
    }
    loader_loaded_ = true;
    const auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    return fn_.LoadGlobal(gipa);
}

bool VulkanHost::Available() {
    if (available_ < 0) {
        // A throwaway instance: is there a GPU at all?
        available_ = 0;
        if (LoadLoader() && MakeInstance()) {
            std::uint32_t count = 0;
            fn_.EnumeratePhysicalDevices(instance_, &count, nullptr);
            available_ = count > 0 ? 1 : 0;
            fn_.DestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
        log::Info("player", "Vulkan {}", available_ ? "is available" : "is not available");
    }
    return available_ == 1;
}

bool VulkanHost::Accept(const HwRenderRequest& request) {
    Shutdown();
    if (!Available()) {
        return false;
    }
    request_ = request;
    negotiation_ = nullptr;
    return true;
}

bool VulkanHost::SetNegotiation(const retro_hw_render_context_negotiation_interface_vulkan* negotiation) {
    if (device_) {
        return false; // too late: the device exists
    }
    negotiation_ = negotiation;
    return true;
}

VkInstance VulkanHost::CreateInstanceWrapper(void* opaque, const VkInstanceCreateInfo* info) {
    auto* self = static_cast<VulkanHost*>(opaque);
    VkInstanceCreateInfo copy = *info;
    VkApplicationInfo app = info->pApplicationInfo ? *info->pApplicationInfo : self->app_;
    app.apiVersion = std::max(app.apiVersion, MIN_API);
    copy.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (self->fn_.CreateInstance(&copy, nullptr, &instance) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return instance;
}

bool VulkanHost::MakeInstance() {
    std::uint32_t loader_version = VK_API_VERSION_1_0;
    if (fn_.EnumerateInstanceVersion) {
        fn_.EnumerateInstanceVersion(&loader_version);
    }
    if (loader_version < MIN_API) {
        log::Info("player", "the Vulkan loader is older than 1.1");
        return false;
    }
    app_ = VkApplicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_.pApplicationName = "EmuDEN";
    app_.pEngineName = "EmuDEN";
    app_.apiVersion = MIN_API;
    if (negotiation_ && negotiation_->get_application_info) {
        if (const VkApplicationInfo* wanted = negotiation_->get_application_info()) {
            app_ = *wanted;
        }
    }
    app_.apiVersion = std::min(std::max(app_.apiVersion, MIN_API), loader_version);

    if (negotiation_ && negotiation_->interface_version >= 2 && negotiation_->create_instance) {
        instance_ = negotiation_->create_instance(fn_.GetInstanceProcAddr, &app_, CreateInstanceWrapper, this);
    } else {
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app_;
        if (fn_.CreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) {
            instance_ = VK_NULL_HANDLE;
        }
    }
    if (!instance_ || !fn_.LoadInstance(instance_)) {
        log::Warn("player", "no Vulkan instance");
        return false;
    }
    return true;
}

// The GPU GL draws with, so the picture can be shared; else the first discrete one.
VkPhysicalDevice VulkanHost::PickGpu() {
    std::uint32_t count = 0;
    fn_.EnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    fn_.EnumeratePhysicalDevices(instance_, &count, gpus.data());
    if (gpus.empty()) {
        return VK_NULL_HANDLE;
    }
    std::uint8_t gl_uuid[VK_UUID_SIZE]{};
    const bool have_uuid = gfx::VkShare::GlDeviceUuid(GlProc, gl_uuid);
    if (!have_uuid) {
        log::Info("player", "GL does not name its GPU; Vulkan takes the first fitting one");
    }
    VkPhysicalDevice best = VK_NULL_HANDLE;
    int best_score = -1;
    for (VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props.pNext = &id;
        fn_.GetPhysicalDeviceProperties2(gpu, &props);
        int score = 0;
        if (have_uuid && std::memcmp(id.deviceUUID, gl_uuid, VK_UUID_SIZE) == 0) {
            score = 10;
        } else if (props.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score = 3;
        } else if (props.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score = 2;
        } else if (props.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
            score = 1;
        }
        if (GraphicsQueueFamily(fn_, gpu) >= 0 && score > best_score) {
            best = gpu;
            best_score = score;
        }
    }
    return best;
}

std::vector<const char*> VulkanHost::SharingExtensions(VkPhysicalDevice gpu) const {
    const std::vector<std::string> have = DeviceExtensionNames(fn_, gpu);
    std::vector<const char*> out;
    for (const char* name : gfx::VkShare::WantedExtensions()) {
        if (std::find(have.begin(), have.end(), name) != have.end()) {
            out.push_back(name);
        }
    }
    return out;
}

VkDevice VulkanHost::CreateDeviceWrapper(VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* info) {
    auto* self = static_cast<VulkanHost*>(opaque);
    std::vector<const char*> names(info->ppEnabledExtensionNames,
                                   info->ppEnabledExtensionNames + info->enabledExtensionCount);
    for (const char* name : self->SharingExtensions(gpu)) {
        if (std::none_of(names.begin(), names.end(), [&](const char* n) { return std::strcmp(n, name) == 0; })) {
            names.push_back(name);
        }
    }
    VkDeviceCreateInfo copy = *info;
    copy.enabledExtensionCount = static_cast<std::uint32_t>(names.size());
    copy.ppEnabledExtensionNames = names.data();
    VkDevice device = VK_NULL_HANDLE;
    if (self->fn_.CreateDevice(gpu, &copy, nullptr, &device) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    self->device_extensions_.assign(names.begin(), names.end());
    return device;
}

bool VulkanHost::MakeDevice() {
    if (!LoadLoader() || !MakeInstance()) {
        return false;
    }
    gpu_ = PickGpu();
    if (!gpu_) {
        log::Warn("player", "no Vulkan GPU with a graphics queue");
        return false;
    }
    device_extensions_.clear();
    retro_vulkan_context context{};
    if (negotiation_ && negotiation_->interface_version >= 2 && negotiation_->create_device2) {
        core_made_device_ = negotiation_->create_device2(&context, instance_, gpu_, VK_NULL_HANDLE,
                                                          fn_.GetInstanceProcAddr, CreateDeviceWrapper, this);
        if (!core_made_device_) {
            core_made_device_ = negotiation_->create_device2(&context, instance_, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                              fn_.GetInstanceProcAddr, CreateDeviceWrapper, this);
        }
    } else if (negotiation_ && negotiation_->create_device) {
        std::vector<const char*> required = SharingExtensions(gpu_);
        core_made_device_ = negotiation_->create_device(
            &context, instance_, gpu_, VK_NULL_HANDLE, fn_.GetInstanceProcAddr, required.data(),
            static_cast<unsigned>(required.size()), nullptr, 0, nullptr);
        if (core_made_device_) {
            device_extensions_.assign(required.begin(), required.end());
        }
    }
    if (core_made_device_) {
        gpu_ = context.gpu;
        device_ = context.device;
        queue_ = context.queue;
        queue_family_ = context.queue_family_index;
    } else {
        // The libretro default: one graphics and compute queue, every feature the GPU has but
        // robust buffer access (which changes behaviour).
        const int family = GraphicsQueueFamily(fn_, gpu_);
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = static_cast<std::uint32_t>(family);
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures features{};
        fn_.GetPhysicalDeviceFeatures(gpu_, &features);
        features.robustBufferAccess = VK_FALSE;
        std::vector<const char*> extensions = SharingExtensions(gpu_);
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        info.pEnabledFeatures = &features;
        if (fn_.CreateDevice(gpu_, &info, nullptr, &device_) != VK_SUCCESS) {
            device_ = VK_NULL_HANDLE;
            log::Warn("player", "no Vulkan device");
            return false;
        }
        device_extensions_.assign(extensions.begin(), extensions.end());
        queue_family_ = static_cast<std::uint32_t>(family);
    }
    if (!fn_.LoadDevice(device_)) {
        log::Warn("player", "the Vulkan device lacks calls the player needs");
        return false;
    }
    if (!core_made_device_) {
        fn_.GetDeviceQueue(device_, queue_family_, 0, &queue_);
    }
    VkPhysicalDeviceProperties props{};
    fn_.GetPhysicalDeviceProperties(gpu_, &props);
    log::Info("player", "Vulkan device for the core: {} (API {}.{}, {})", static_cast<const char*>(props.deviceName),
              VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
              core_made_device_ ? "made by the core" : "made by the player");
    return MakeFrameObjects();
}

bool VulkanHost::MakeFrameObjects() {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queue_family_;
    if (fn_.CreateCommandPool(device_, &pool, nullptr, &pool_) != VK_SUCCESS) {
        return false;
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = SLOTS;
    if (fn_.AllocateCommandBuffers(device_, &alloc, commands_.data()) != VK_SUCCESS) {
        return false;
    }
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (VkFence& f : fences_) {
        if (fn_.CreateFence(device_, &fence, nullptr, &f) != VK_SUCCESS) {
            return false;
        }
    }
    gfx::VkShareDevice share;
    share.fn = &fn_;
    share.instance = instance_;
    share.gpu = gpu_;
    share.device = device_;
    share.queue_family = queue_family_;
    share.extensions = device_extensions_;
    share_ = gfx::VkShare::Create(share, GlProc, gles_);
    if (!share_) {
        return false;
    }
    FillInterface();
    return true;
}

void VulkanHost::Shutdown() {
    if (device_) {
        {
            std::lock_guard lock(queue_mutex_);
            fn_.DeviceWaitIdle(device_);
        }
        share_.reset();
        for (VkFence& f : fences_) {
            if (f) {
                fn_.DestroyFence(device_, f, nullptr);
            }
            f = VK_NULL_HANDLE;
        }
        if (pool_) {
            fn_.DestroyCommandPool(device_, pool_, nullptr);
        }
        pool_ = VK_NULL_HANDLE;
        commands_ = {};
        if (core_made_device_ && negotiation_ && negotiation_->destroy_device) {
            negotiation_->destroy_device();
        }
        fn_.DestroyDevice(device_, nullptr);
    }
    if (instance_) {
        fn_.DestroyInstance(instance_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
    gpu_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    core_made_device_ = false;
    negotiation_ = nullptr;
    slot_ = 0;
    shown_slot_ = -1;
    image_ = nullptr;
    wait_semaphores_.clear();
    core_commands_.clear();
    signal_semaphore_ = VK_NULL_HANDLE;
    picture_width_ = picture_height_ = 0;
}

VulkanHost::~VulkanHost() {
    Shutdown();
    if (loader_loaded_) {
        SDL_Vulkan_UnloadLibrary();
    }
}

} // namespace den::player
