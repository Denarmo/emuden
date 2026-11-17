// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The libretro environment callback: what a core asks the player and tells it.

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "core/core_impl.h"
#include "den/base/log.h"

namespace den::player {

LibretroCore::Impl* g_core = nullptr;

namespace {

// A core may show the same message every frame; the log keeps it once.
void LogMessage(const char* text) {
    static std::string last;
    if (last != text) {
        last = text;
        log::Info("core", "{}", text);
    }
}

} // namespace

bool LibretroCore::Impl::ResizeHwTarget(const retro_game_geometry& announced) {
    unsigned width = std::max(announced.max_width, announced.base_width);
    unsigned height = std::max(announced.max_height, announced.base_height);
    if (hw_vulkan) {
        // A Vulkan core draws into its own image: the picture starts at the base size and
        // grows when a frame needs more.
        width = announced.base_width;
        height = announced.base_height;
    } else if (geometry) {
        width = std::max({width, geometry->max_width, geometry->base_width});
        height = std::max({height, geometry->max_height, geometry->base_height});
    }
    width = std::max(std::clamp(width, 1u, 16384u), hw_width);
    height = std::max(std::clamp(height, 1u, 16384u), hw_height);
    if (width == hw_width && height == hw_height) {
        return true;
    }
    if (!hw_host->ResizeTarget(width, height)) {
        return false;
    }
    hw_width = width;
    hw_height = height;
    return true;
}

const std::string* LibretroCore::Impl::OptionValue(const std::string& key) const {
    const auto declared = option_table.find(key);
    if (const auto set = options.find(key); set != options.end()) {
        if (declared == option_table.end()) {
            return &set->second;
        }
        const auto& values = declared->second.values;
        if (std::any_of(values.begin(), values.end(),
                        [&](const OptionChoice& value) { return value.value == set->second; })) {
            return &set->second;
        }
        // A hint or tuning value written for another version of the core.
        if (unknown_values.insert(key + "=" + set->second).second) {
            std::string offered;
            for (const OptionChoice& value : values) {
                offered += (offered.empty() ? "" : ", ") + value.value;
            }
            log::Warn("core", "option {}: \"{}\" is not one of the core's values ({}); it uses \"{}\"", key,
                      set->second, offered, declared->second.default_value);
        }
    }
    return declared != option_table.end() ? &declared->second.default_value : nullptr;
}

void LibretroCore::Impl::DeclareOption(OptionInfo option) {
    if (option.key.empty() || option.values.empty()) {
        return;
    }
    if (option.label.empty()) {
        option.label = option.key;
    }
    if (option.default_value.empty()) {
        option.default_value = option.values.front().value;
    }
    if (!option_table.contains(option.key)) {
        option_order.push_back(option.key);
    }
    std::string key = option.key;
    option_table[std::move(key)] = std::move(option);
}

namespace {

void RETRO_CALLCONV LogCallback(retro_log_level level, const char* format, ...) {
    if (level < RETRO_LOG_WARN) {
        return;
    }
    char text[1024];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    std::string line = text;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    if (level == RETRO_LOG_ERROR) {
        log::Error("core", "{}", line);
    } else {
        log::Warn("core", "{}", line);
    }
}

// Legacy "Description; first|second|..." strings: the first value is the default.
void AddVariables(const retro_variable* variables) {
    for (; variables && variables->key; ++variables) {
        const char* value = variables->value ? std::strstr(variables->value, "; ") : nullptr;
        if (!value) {
            continue;
        }
        OptionInfo option;
        option.key = variables->key;
        option.label.assign(variables->value, value);
        for (value += 2; *value;) {
            const std::size_t size = std::strcspn(value, "|");
            if (size > 0) {
                option.values.push_back({std::string(value, size), std::string(value, size)});
            }
            value += size;
            if (*value == '|') {
                ++value;
            }
        }
        g_core->DeclareOption(std::move(option));
    }
}

// v1 and v2 definitions share key, desc, info, values[] and default_value.
template <typename Definition>
OptionInfo MakeOption(const Definition& def) {
    OptionInfo option;
    option.key = def.key;
    option.label = def.desc ? def.desc : "";
    option.description = def.info ? def.info : "";
    for (unsigned i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && def.values[i].value; ++i) {
        const char* value = def.values[i].value;
        const char* label = def.values[i].label;
        option.values.push_back({value, label && *label ? label : value});
    }
    option.default_value = def.default_value ? def.default_value : "";
    return option;
}

void AddDefinitions(const retro_core_option_definition* def) {
    for (; def && def->key; ++def) {
        g_core->DeclareOption(MakeOption(*def));
    }
}

void AddOptionsV2(const retro_core_options_v2* options) {
    for (auto* def = options ? options->definitions : nullptr; def && def->key; ++def) {
        OptionInfo option = MakeOption(*def);
        for (auto* category = def->category_key ? options->categories : nullptr;
             category && category->key; ++category) {
            if (std::strcmp(category->key, def->category_key) == 0) {
                option.category = category->desc ? category->desc : category->key;
                break;
            }
        }
        g_core->DeclareOption(std::move(option));
    }
}

void SetInputDescriptors(const retro_input_descriptor* descriptor) {
    g_core->button_names.clear();
    for (; descriptor && descriptor->description; ++descriptor) {
        if (descriptor->device == RETRO_DEVICE_JOYPAD && descriptor->id < 16 &&
            descriptor->port < MAX_PORTS && *descriptor->description) {
            g_core->button_names.push_back(
                {descriptor->port, descriptor->id, descriptor->description});
        }
    }
}

std::uintptr_t RETRO_CALLCONV HwCurrentFramebuffer() {
    return g_core->hw_host->CurrentFramebuffer();
}

retro_proc_address_t RETRO_CALLCONV HwGetProcAddress(const char* name) {
    return reinterpret_cast<retro_proc_address_t>(g_core->hw_host->GetProcAddress(name));
}

// OpenGL and Vulkan: Direct3D is refused, and cores then ask for one of the others.
bool SetHwRender(retro_hw_render_callback* callback) {
    if (!callback || !g_core->hw_host || g_core->game_loaded) {
        return false;
    }
    HwRenderRequest request;
    switch (callback->context_type) {
    case RETRO_HW_CONTEXT_OPENGL:
        request.api = HwRenderRequest::Api::OpenGL;
        break;
    case RETRO_HW_CONTEXT_OPENGL_CORE:
        request.api = HwRenderRequest::Api::OpenGLCore;
        request.major = callback->version_major;
        request.minor = callback->version_minor;
        break;
    case RETRO_HW_CONTEXT_OPENGLES2:
        request.api = HwRenderRequest::Api::OpenGLES;
        request.major = 2;
        break;
    case RETRO_HW_CONTEXT_OPENGLES3:
        request.api = HwRenderRequest::Api::OpenGLES;
        request.major = 3;
        break;
    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
        request.api = HwRenderRequest::Api::OpenGLES;
        request.major = callback->version_major;
        request.minor = callback->version_minor;
        break;
    case RETRO_HW_CONTEXT_VULKAN:
        request.api = HwRenderRequest::Api::Vulkan;
        request.major = callback->version_major;
        request.minor = callback->version_minor;
        break;
    default:
        g_core->hw_accepted = false;
        g_core->hw_refused = true;
        return false;
    }
    request.depth = callback->depth;
    request.stencil = callback->stencil;
    request.bottom_left_origin = callback->bottom_left_origin;
    if (!g_core->hw_host->Accept(request)) {
        g_core->hw_accepted = false;
        g_core->hw_refused = true;
        return false;
    }
    callback->get_current_framebuffer = HwCurrentFramebuffer;
    callback->get_proc_address = HwGetProcAddress;
    g_core->hw = *callback;
    g_core->hw_vulkan = request.api == HwRenderRequest::Api::Vulkan;
    g_core->hw_accepted = true;
    g_core->hw_refused = false;
    return true;
}

bool RETRO_CALLCONV RumbleCallback(unsigned port, retro_rumble_effect effect,
                                   std::uint16_t strength) {
    if (g_core == nullptr || port >= MAX_PORTS) {
        return false;
    }
    if (effect == RETRO_RUMBLE_STRONG) {
        g_core->rumble_strong[port] = strength;
    } else if (effect == RETRO_RUMBLE_WEAK) {
        g_core->rumble_weak[port] = strength;
    } else {
        return false;
    }
    return true;
}

bool OptionsCommand(unsigned command, void* data, bool* handled) {
    *handled = true;
    switch (command) {
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* variable = static_cast<retro_variable*>(data);
        if (!variable || !variable->key) {
            return false;
        }
        variable->value = nullptr;
        if (const std::string* value = g_core->OptionValue(variable->key)) {
            variable->value = value->c_str();
        }
        return variable->value != nullptr;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLE:
        // The core changes one of its own options; null only asks whether that is supported.
        if (const auto* variable = static_cast<const retro_variable*>(data)) {
            if (!variable->key || !variable->value) {
                return false;
            }
            g_core->options[variable->key] = variable->value;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = g_core->options_changed;
        g_core->options_changed = false;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        AddVariables(static_cast<const retro_variable*>(data));
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        AddDefinitions(static_cast<const retro_core_option_definition*>(data));
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        if (data) {
            AddDefinitions(static_cast<const retro_core_options_intl*>(data)->us);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        AddOptionsV2(static_cast<const retro_core_options_v2*>(data));
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        if (data) {
            AddOptionsV2(static_cast<const retro_core_options_v2_intl*>(data)->us);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        if (const auto* display = static_cast<const retro_core_option_display*>(data);
            display && display->key) {
            if (const auto option = g_core->option_table.find(display->key);
                option != g_core->option_table.end()) {
                option->second.visible = display->visible;
            }
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        g_core->update_display =
            data ? static_cast<const retro_core_options_update_display_callback*>(data)->callback
                 : nullptr;
        return true;
    default:
        *handled = false;
        return false;
    }
}

bool VideoCommand(unsigned command, void* data, bool* handled) {
    *handled = true;
    switch (command) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const auto format = *static_cast<const retro_pixel_format*>(data);
        if (format != RETRO_PIXEL_FORMAT_0RGB1555 && format != RETRO_PIXEL_FORMAT_XRGB8888 &&
            format != RETRO_PIXEL_FORMAT_RGB565) {
            return false;
        }
        g_core->pixel_format = format;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool*>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        if (const auto* geometry = static_cast<const retro_game_geometry*>(data)) {
            g_core->geometry = *geometry;
            g_core->av_changed = true;
            // Now: the core may draw at the new size before this frame ends.
            if (g_core->hw_ready) {
                g_core->ResizeHwTarget(*geometry);
            }
            return true;
        }
        return false;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        if (const auto* av = static_cast<const retro_system_av_info*>(data)) {
            g_core->geometry = av->geometry;
            g_core->timing = av->timing;
            g_core->av_changed = true;
            if (g_core->hw_ready) {
                g_core->ResizeHwTarget(av->geometry);
            }
            return true;
        }
        return false;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        // Bit 0: the picture is used, bit 1: the sound is used.
        *static_cast<int*>(data) = 1 | (g_core->audio_muted ? 0 : 2);
        return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        return SetHwRender(static_cast<retro_hw_render_callback*>(data));
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        if (!g_core->hw_host || !data) {
            return false;
        }
        switch (g_core->hw_host->PreferredApi()) {
        case HwRenderRequest::Api::OpenGLES:
            *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGLES3;
            break;
        case HwRenderRequest::Api::OpenGL:
            *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGL;
            break;
        case HwRenderRequest::Api::OpenGLCore:
            *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGL_CORE;
            break;
        case HwRenderRequest::Api::Vulkan:
            *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_VULKAN;
            break;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: {
        const void* iface = g_core->hw_host && g_core->hw_vulkan ? g_core->hw_host->VulkanInterface() : nullptr;
        if (!iface || !data) {
            return false;
        }
        *static_cast<const void**>(data) = iface;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT: {
        auto* query = static_cast<retro_hw_render_context_negotiation_interface*>(data);
        if (!query) {
            return false;
        }
        query->interface_version =
            query->interface_type == RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN ? 2 : 0;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE: {
        const auto* negotiation = static_cast<const retro_hw_render_context_negotiation_interface*>(data);
        if (!negotiation || !g_core->hw_host || !g_core->hw_vulkan ||
            negotiation->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN) {
            return false;
        }
        return g_core->hw_host->SetVulkanNegotiation(negotiation);
    }
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:
        // The core's context is its own already.
        return g_core->hw_host != nullptr;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:
        *static_cast<float*>(data) = 60.0f;
        return true;
    default:
        *handled = false;
        return false;
    }
}

// Kept for an achievements runtime: the descriptors point into the core's own memory.
bool SetMemoryMaps(const retro_memory_map* map) {
    LibretroCore::Impl& c = *g_core;
    c.memory_descriptors.clear();
    c.memory_names.clear();
    if (map && map->descriptors) {
        c.memory_descriptors.assign(map->descriptors, map->descriptors + map->num_descriptors);
        for (retro_memory_descriptor& descriptor : c.memory_descriptors) {
            if (descriptor.addrspace) {
                descriptor.addrspace = c.memory_names.emplace_back(descriptor.addrspace).c_str();
            }
        }
    }
    c.memory_map.descriptors = c.memory_descriptors.data();
    c.memory_map.num_descriptors = static_cast<unsigned>(c.memory_descriptors.size());
    c.memory_map_set = !c.memory_descriptors.empty();
    c.memory_moved = true;
    return true;
}

} // namespace

bool RETRO_CALLCONV EnvironmentCallback(unsigned command, void* data) {
    if (!g_core) {
        return false;
    }
    bool handled = false;
    if (const bool result = OptionsCommand(command, data, &handled); handled) {
        return result;
    }
    if (const bool result = VideoCommand(command, data, &handled); handled) {
        return result;
    }
    switch (command) {
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        static_cast<retro_rumble_interface*>(data)->set_rumble_state = RumbleCallback;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
        *static_cast<std::uint64_t*>(data) = (1u << RETRO_DEVICE_JOYPAD) |
                                             (1u << RETRO_DEVICE_ANALOG) |
                                             (1u << RETRO_DEVICE_POINTER);
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *static_cast<const char**>(data) = g_core->system_dir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = g_core->save_dir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        *static_cast<const char**>(data) = g_core->core_path.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = LogCallback;
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *static_cast<unsigned*>(data) = g_core->language;
        return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        *static_cast<unsigned*>(data) = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        if (const auto* message = static_cast<const retro_message*>(data);
            message && message->msg) {
            LogMessage(message->msg);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        if (const auto* message = static_cast<const retro_message_ext*>(data);
            message && message->msg) {
            LogMessage(message->msg);
        }
        return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        *static_cast<bool*>(data) = g_core->audio_muted;
        return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        SetInputDescriptors(static_cast<const retro_input_descriptor*>(data));
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        return SetMemoryMaps(static_cast<const retro_memory_map*>(data));
    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
        return SetNetpacketInterface(data);
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        return true;
    default:
        return false;
    }
}

} // namespace den::player
