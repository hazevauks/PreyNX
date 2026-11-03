# How the port is put together

## Layout

```
src/                 the engine, from openPREY (upstream c9601e0), with the game
  renderer/deko3d/   the native renderer
  sys/switch/        the platform layer: files, threads, input, audio, network, logs
shaders/             the renderer's own GLSL, compiled into the NRO with uam
tools/               on the PC: extraction, the game's shaders, SD card
cmake/               build helpers
SD_CARD/             test build, ready to copy to the card (not in git)
SD_LOG/              logs copied back from the card (not in git)
```

## The game module

Homebrew cannot load a library, but the game code expects to be one: it has its own copies of
the engine's interface globals and of idlib, and exposes only `GetGameAPI`. So its objects are
linked into one relocatable object, with COMDAT groups dissolved, and every symbol but
`GetGameAPI` is made local. `Sys_DLL_Load` then hands that function out
(`cmake/PreyGameObject.cmake`, `src/sys/switch/switch_main.cpp`).

## The renderer

There is no OpenGL in the build, and no Mesa. The front end is the original one. Everything that
touched OpenGL is gone or replaced:

| File | Replaces | Does |
| --- | --- | --- |
| `dk_device.cpp` | `glimp`, driver | device, memory blocks, swapchain, command buffers, uploads, frame pacing |
| `dk_state.cpp` | `GL_State`, `tr_backend.cpp` | tracks state, writes only what changed before a draw |
| `dk_draw.cpp` | `draw_common`, `draw_arb2`, `tr_render` | the view's passes |
| `dk_image.cpp` | `gl_Image`, parts of `Image_load` | image storage, uploads, retail `.dds` files, screen copies |
| `dk_programs.cpp` | ARB program loading | shader modules by the name materials use |
| `VertexCache.cpp` | same file | static and per-frame GPU memory |

### Conventions

Clip space has y up and depth -1 to 1, as on OpenGL, so matrices and winding work unchanged. The screen's
first row is its top, which differs from OpenGL in three places, all of them converted: viewports and
scissors (`Dk_SetViewport`, `Dk_SetScissor`), a fragment's position (`arb2glsl.py`), and screen copies, which
flip so programs read `_currentRender` as on OpenGL (`idImage::CopyFramebuffer`).

### Uniforms

Programs read registers, as ARB programs do. `program.env`, `program.local` and the matrices are uniform
blocks, pushed into the command stream when they change (`Dk_FlushForDraw`):

| Vertex binding | Fragment binding | Holds |
| --- | --- | --- |
| 0 | | `mvp`, `modelView`, `projection`, `texture`, as rows |
| 1 | 0 | `program.env` |
| 2 | 1 | `program.local` |

### Vertex attributes

`0` position, `1` texture coordinates, `2` normal, `3` tangent, `4` bitangent, `5` color. In ARB
programs: `attrib[8]` is 1, `attrib[9]` 3, `attrib[10]` 4, `attrib[11]` 2.

### Shaders

The renderer's own passes are in `shaders/`, and go into the NRO's romfs.

Prey's materials also name programs from the game's data, in ARB assembly (`glprogs/*.vfp`: skin,
hair, heat haze and so on). There is no compiler on the console, and those files are the game's, so they do
not belong in this repository. `tools/build_shaders.py` reads them from your `.pk4` files, translates
them to GLSL with `tools/arb2glsl.py` and compiles them with `uam`. The game loads the results from
`sd:/switch/prey/shaders/`. A program with no file leaves its stages undrawn, and a warning in the log.

### Images

Prey ships nearly all its textures compressed in `dds/` (`pak001.pk4`), named after the image program
that would make them. They load as they are (`idImage::LoadPrecompressed`): no decoding or compressing on
the console, a quarter of the memory, and the GPU's own BC1 to BC3 decoding. Normal maps are `RXGB`, with X in
alpha, as the game's programs expect. A `.tga` that has no `.dds` takes the slow path and is kept
uncompressed.

### Stencil shadows

Each shadow volume is one draw with two-sided stencil, where OpenGL's single-sided path needs two to
four. The engine's front faces are the GPU's back faces, which `Dk_SetShadowStencilOps` accounts for.

### What deko3D 0.5.0 needs

Learned by the ports that run deko3D on hardware, and followed here:

- Its release library ends the process silently on an error. Test builds link the debug library
  (`PREY_DEKO3D_DEBUG`), whose callback writes the error to the log.
- Images need `Dk_FixTileSize`, or BC textures 24 to 32 pixels tall corrupt themselves.
- A command can write past what it reserved, so each piece of command memory keeps a gap.
- Clears respect the scissor. Coarse depth culling (ZCULL) stays off until tested on the console: `r_dkZcull 1`.
- Depth-only draws bind an empty fragment program instead of none.
- `uam` from devkitPro lacks the fixes those ports made to it. Flickering blocks of pixels would point there.

## Threads and cores

| Core | Runs |
| --- | --- |
| 0 | the game and the render front end and back end |
| 1 | free, for the back end's own thread |
| 2 | async tick, sound, every library thread |
| 3 | the system |

`pthread_create` is wrapped so threads started inside libraries land on their core too.

## Audio

devkitPro's OpenAL Soft calls seven of SDL2's audio functions. SDL2 itself would pull in its video driver
and all of Mesa, so `switch_audio.cpp` implements those seven over the system's `audout` service. Input
goes straight to libnx's HID too. That leaves SDL2 out of the build, and the NRO around 9 MB.

## Fixes that came from the first port

[openPREYswitch](https://github.com/hazevauks/openPREYswitch), this project's first attempt, played the
game's opening on hardware and found these in the shared code. They are all carried over:

- `long` is 64 bits on arm64. It broke the float sign macros, `RSqrt`, `rvRandom`, `idPVS`, tangents and
  the hash functions, and with them savegame checksums.
- A script function's vector parameter took 24 bytes on 64-bit, and everything after it was read wrong.
- `char` is unsigned on arm64, and the map CRC mixed in a Quake 4 key. Prey's `.cm` and `.aas` files were
  rejected and rebuilt, with holes.
- Prey's AAS files are version 1.07, without the feature fields.
- `sys.trigger()` must pass the local player to targets in single player.
- Savegames restored render entities with garbage in the fields they do not carry.
- A rejected savegame destroyed objects that had never been spawned.
