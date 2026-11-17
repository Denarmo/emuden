# Frame generation: options

Researched 2026-11-17. Three separate things go by this name in EmuDEN:

1. **Helix SR** and **LSFG** for PC (Steam) games: other people's programs, set up per game by
   a feature plugin. See [game-mods-api.md](game-mods-api.md).
2. **EmuDEN's own frame generation** for the games EmuDEN draws itself (libretro cores), on
   every operating system. This page.
3. Engine apps that draw their own window (PS2, Switch): out of reach of 2; on Linux the LSFG
   layer can cover them, since they are Vulkan programs.

## What exists

EmuDEN has the finished picture of a game and nothing else: no motion vectors, no depth. So
only methods that work from two pictures apply.

| Option | Licence | Usable in a GPL app | Where it runs | Verdict |
|---|---|---|---|---|
| AMD FSR 3.1 optical flow (FidelityFX SDK 1.1.4, `framegeneration/fsr3`) | MIT | Yes | HLSL and GLSL 4.50 compute source | **Reference for motion estimation**: 8x8 blocks, a 7-level pyramid, a scene-cut detector |
| AMD FSR 3.1 frame interpolation | MIT | Yes | Same | Needs the game's motion vectors and depth; not for us |
| AMD FSR 4 ML frame generation | Binary only | No | Signed Windows DLLs | Rejected |
| afmf-linux | MIT | Yes | A Linux Vulkan layer | **Reference for the warp**, the fallback where the two warps disagree, and pacing |
| lsfg-vk | CC BY-NC-ND 4.0 | No | Linux Vulkan | A loader for the shaders of the user's paid Lossless Scaling; plugin only |
| Intel XeSS-FG | Binary only | No | Windows DX12 | Rejected |
| Arm ASR | MIT | Yes | Mobile | An upscaler, no frame generation |
| Qualcomm `GL_QCOM_frame_extrapolation` | Driver extension | Calling it is fine | Adreno, GLES | An optional later path: extrapolation, so no added latency, more artefacts |
| RIFE (rife-ncnn-vulkan) | MIT | Yes | Any Vulkan | Too slow for handhelds |
| gamescope, RetroArch | | | | Neither has it (an open request each) |

Not checked against primary sources: mpv/libplacebo (temporal blending, not interpolation),
MVTools (CPU), SVP (closed core), MediaTek.

## Recommended: our own, in the picture chain

EmuDEN draws every core's picture through `gfx::EffectChain`: fragment shaders for GL 3.3 and
GLES 3.0, with a pass's previous frame available as `History()`. The same code runs on
Windows, Linux, SteamOS and Android. Black frame insertion (`den/player/bfi.h`,
`app/screens/player/black_frames.cpp`) already asks the display for a multiple of the game's
rate and presents in step with it. Frame generation is those two put together.

Because EmuDEN owns the frame, it can do what a layer cannot:

- its own menus and overlays are drawn after the generated frame, so they are never warped;
- at the core's own resolution, a 2D game's scroll can be snapped to whole pixels;
- it knows the core's exact frame rate, so the generated frame lands exactly halfway.

### First milestone

Fragment shaders only (no compute), so it runs on every device the app runs on.

1. Keep the last two pictures of the core.
2. Motion: a luma pyramid (4 levels at core resolution is enough for 240p to 480p pictures),
   block matching per level seeded by the level above, both directions.
3. A scene-cut test (mean absolute difference): on a cut, repeat the frame.
4. One warp pass: sample both pictures along half the vector; where the two disagree, show
   the nearer real frame.
5. A dominant-vector vote for 2D games: most of the screen scrolls together, and repeating
   tiles fool local matching.
6. Present real and generated frames half a frame apart, through the pacing BFI uses.
7. A quick-settings toggle next to BFI, off by default, with a debug view of the vectors.

Measure the milliseconds per generated frame on the Retroid, the Legion Go S and the PC, and
look at a 2D scroller, a 30 fps 3D game and a game with a flickering sprite.

### Risks

- **Latency.** Interpolation holds one real frame back: 33 ms for a 30 fps game, 17 ms at
  60 fps. It suits 30 fps games on 60 Hz and 60 fps games on 120 Hz, and works against
  run-ahead. Off by default.
- **Pixel art.** Repeating tiles, parallax layers, 30 Hz sprite flicker, palette cycling and
  dithering all break block matching. The dominant-vector vote, whole-pixel vectors and
  "no confident match: repeat the frame" are the answers; expect games where it must stay off.
- **The game's own HUD** is part of the picture: static regions need detecting.
- **Pacing** needs a display at twice the game's rate, or VRR. Bad pacing looks worse than
  none.
- **Cost on mobile** is unmeasured; the figure of 2 to 5 ms at 720p on a Steam Deck class GPU
  is an estimate from afmf-linux's desktop numbers.
