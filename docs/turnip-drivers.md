# Custom Vulkan drivers (Turnip) on Android

Researched 2026-11-17; not built. How PPSSPP, Dolphin, Eden, GameNative, DroidDeck and ARMSX2
let players pick a Mesa "Turnip" driver for Qualcomm Adreno GPUs, and what it would take here.

## Mechanism

[libadrenotools](https://github.com/bylaws/libadrenotools) (BSD-2-Clause) loads a driver
without root: `adrenotools_open_libvulkan(...)` opens the system `libvulkan.so` in a private
linker namespace whose driver lookup points at the chosen driver, and returns a handle;
`vkGetInstanceProcAddr` from that handle is used for everything after. Android 9 (API 28) or
later, arm64. The app ships four hook libraries built from libadrenotools and passes its
`nativeLibraryDir`, which needs `useLegacyPackaging = true` (not set in
`shells/android/app/build.gradle` today). A wrong directory fails silently: the system
driver loads instead. Drivers live in app-private storage, as a zip with `meta.json`
(`name`, `driverVersion`, `libraryName`, ...) and the `.so`.

## In EmuDEN

- One change point: `LoadLoader()` in `player/src/vk_host_device.cpp`, which today calls
  `SDL_Vulkan_LoadLibrary(nullptr)`. With a driver chosen, it takes `vkGetInstanceProcAddr`
  from the adrenotools handle instead. Cores already get Vulkan through EmuDEN's forwarding
  `vkGetInstanceProcAddr` (libretro HW render and the negotiation interface), so in-process
  cores follow. A core that `dlopen`s `libvulkan.so` itself would not.
- The loader is loaded once per process, so a per-game driver needs a fresh process (or
  unloading, untested).
- **Biggest risk:** core frames reach the presenter through AHardwareBuffer and sync fds
  (`gfx/src/vk_share*.cpp`), and the presenter is Qualcomm's own GLES driver. PPSSPP and
  Dolphin present through their own Vulkan swapchain, so they prove nothing about sharing
  between Turnip and the system GLES driver. Test that first.
- Standalone emulator apps (the PS2 app) cannot be covered: each must integrate it itself.

## Drivers

Mesa is MIT. Builds: K11MCH1/AdrenoToolsDrivers (about fortnightly; the same page also posts
Qualcomm proprietary drivers, which must never be bundled or mirrored),
Weab-chan/freedreno_turnip-CI, whitebelyash (A8xx), Banners-Turnip, lfdevs. Download on the
player's request, pinned by URL and SHA-256, or build one from Mesa source ourselves.

| Device | GPU | Turnip |
|---|---|---|
| Retroid Pocket 5, Mini | Adreno 650 | mature |
| Retroid Pocket Duo | Adreno 722 | not in upstream Mesa; community patches unverified on hardware |
| Retroid Pocket Classic | reported as Adreno A12 | unverified |

So on the Retroid Duo, the device that matters most here, there may be nothing good to load
yet.

## Design if built

A "GPU drivers" plugin with a pinned list (URL, SHA-256, GPU ids, minimum API) plus a setting
shown only on Adreno with API 28+: download or import a zip, choose per device with a per-game
override, show the driver in use (`VkPhysicalDeviceDriverProperties`), and a "launch pending"
marker written before the instance is created, so a start that crashed falls back to the
system driver and says so. Who gains: the Switch engine most; PPSSPP little; Dolphin, Flycast
and Azahar have Vulkan paths (unchecked on Android); DenSX2 is probably GLES (no gain).
Downloading executable code is a Play Store policy risk, not one for GitHub releases.
