# Vulkan cores

A libretro core that draws with the GPU asks for a context with `SET_HW_RENDER`. EmuDEN
gives OpenGL (desktop, or ES on Android) and Vulkan. The app's own drawing (the canvas, picture
presets, the second screen) stays on OpenGL; a Vulkan core's picture is handed to OpenGL as a
texture, so everything after that point treats it like a GL core's picture.

## Pieces

| Piece | Where | What it does |
|---|---|---|
| `HwRenderHost` | `player/src/core/hw_render.h` | What the libretro glue asks of the player. Gains Vulkan calls: the device, `retro_hw_render_interface_vulkan`, the negotiation interface. |
| `HwHost` | `player/src/hw_host.*` | The host a session gives the core. Holds the renderer choice, answers `GET_PREFERRED_HW_RENDER`, and forwards to the GL context or the Vulkan host, whichever the core accepted. |
| `HwContext` | `player/src/hw_context.*` | The GL context of a GL core (unchanged). |
| `VulkanHost` | `player/src/vk_host*.cpp` | Loads Vulkan, makes the instance and device (or lets the core make them through the negotiation interface), implements the retro interface, and copies each frame into the shared image. |
| `gfx::VkShare` | `gfx/include/den/gfx/vk_share.h`, `gfx/src/vk_share_*.cpp` | Images that Vulkan writes and OpenGL samples, and the wait between them. One implementation per way of sharing (below). |

Vulkan is loaded at run time through SDL (`SDL_Vulkan_LoadLibrary`), so no build links the
Vulkan loader and a device without Vulkan just answers that it has none. The headers come from
SDL's copy of the Khronos headers.

## Frame

1. Before `retro_run` the host waits for the fence of the sync slot it is about to reuse (two
   slots), so the core's `get_sync_index` and `wait_sync_index` mean what the libretro header
   says.
2. The core draws and calls `set_image` with its image (any `GENERAL` or
   `SHADER_READ_ONLY_OPTIMAL` layout) and optional semaphores, then `video_refresh` with
   `RETRO_HW_FRAME_BUFFER_VALID`.
3. `Present` records one command buffer: acquire the core's image from its queue family when
   that differs, blit the picture part (width x height) into the slot's shared image, release
   the core's image back, and hand the shared image to the external (GL) side. The submit waits
   for the core's semaphores and signals the slot's fence and, where the sharing has one, the
   export semaphore. Every submit holds the queue lock the core also takes.
4. When the app draws the picture, GL waits for that frame (`VkShare::BeginRead`) and samples
   the slot's texture; the picture is top row first, as Vulkan draws.

A duplicated frame (`video_refresh` with `NULL`) keeps the last picture; nothing is copied.

## Sharing the image with OpenGL

Tried in this order; the first that works for the device is used for the whole session.

| Kind | Hosts | Vulkan side | GL side | Wait |
|---|---|---|---|---|
| Hardware buffer | Android | `AHardwareBuffer` imported with `VK_ANDROID_external_memory_android_hardware_buffer` | `eglGetNativeClientBufferANDROID`, `EGLImage`, `glEGLImageTargetTexture2DOES` | Semaphore exported as a sync fd, `EGL_ANDROID_native_fence_sync` + `eglWaitSyncKHR` (GPU wait). Without sync fds, the fence is waited on the CPU. |
| Memory object | Windows, Linux | Dedicated memory exported as an NT handle (Windows) or an opaque fd | `GL_EXT_memory_object(_win32/_fd)`, `glTexStorageMem2DEXT` | `GL_EXT_semaphore(_win32/_fd)`: `glImportSemaphore*`, `glWaitSemaphoreEXT` |
| Copy | everywhere | The picture is copied into a host-visible buffer | `glTexSubImage2D` into an ordinary texture | The fence, on the CPU |

The copy costs a CPU stall and an upload each frame; it exists so a Vulkan core still runs on a
driver whose GL lacks the extensions. The log names the kind in use (`vulkan picture via ...`).
Both APIs must run on the same GPU for the first two: the host picks the physical device whose
`deviceUUID` matches the GL driver's `GL_DEVICE_UUID_EXT` when GL reports one, and the first
discrete or integrated GPU otherwise.

## Device

* Instance: Vulkan 1.1 (or what the core's `get_application_info` asks, if higher), with the
  external memory/semaphore capability extensions. A core with a v2 negotiation interface and
  `create_instance` makes it through the host's wrapper, which adds those extensions.
* Device: one queue family with graphics and compute. Extensions the sharing needs are added to
  what the core asks for (`create_device2` through the wrapper, or `create_device` with the
  required list); without a negotiation interface the host makes the device with the sharing
  extensions and every feature the GPU has that cores commonly need (the libretro default).
  Some cores ignore the required list or the wrapper and call `vkCreateDevice` themselves
  (Flycast), so the `vkGetInstanceProcAddr` a core gets hands out a `vkCreateDevice` that adds
  the sharing extensions too; everything else goes to the loader.
* When the sharing extensions are missing the host still makes a device and uses the copy.

## Renderer choice

The player menu gets a `Renderer` entry for cores that draw with the GPU: Auto, OpenGL or
Vulkan. It is stored like a core option, as `core_options.<core>.emuden_renderer` for the
console and `game_options.<system>.<game>.emuden_renderer` for one game, and takes effect when
the game starts again (the menu says so). It stays on the device: sync leaves it out, since what
works depends on each device's GPU and drivers.

* Auto: `GET_PREFERRED_HW_RENDER` answers OpenGL, as before, and whichever API the core then
  asks for is given when the device has it. Cores keep their own defaults.
* OpenGL: Vulkan requests are refused, so cores that try Vulkan first fall back to GL.
* Vulkan: `GET_PREFERRED_HW_RENDER` answers Vulkan. A core that asks for GL anyway still gets it
  (it may have no Vulkan renderer), and the menu shows what is actually in use.

## What keeps working

* Frame pacing, rewind and save states: unchanged. Save states are the core's business; rewind
  only calls `retro_serialize`.
* Screenshots and resume previews read the shared texture back through GL, as for GL cores (the
  copy kind reads its own buffer).
* Shader chains (`gfx/retro_chain`) and the second screen sample the GL texture, so they run on
  Vulkan frames too. If the image cannot be shared at all (no Vulkan sharing and no copy), the
  core is refused Vulkan at `SET_HW_RENDER` and falls back to GL; the menu's renderer entry
  says "Vulkan is not available on this device".

## Threads

Cores may submit from their own threads (Play!'s GS thread). `lock_queue`/`unlock_queue` take a
mutex that the host's own submits take too. Everything else runs on the thread that runs the
core, which is the app's GL thread.
