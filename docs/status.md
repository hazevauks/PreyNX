# Status

What has been checked, and how. Nothing here counts as working until a console has shown it.

## Checked

| What | How |
| --- | --- |
| The engine, the game and the renderer build | locally and on GitHub Actions |
| The NRO's name, author and icon | read back from the file: Prey, hazevauks, 256x256 JPEG |
| No OpenGL, Mesa or SDL2 in the build | the link has none of their symbols; the NRO is about 9 MB |
| Extraction from the discs and the 1.4 patch | all `.pk4` files pass an archive test |
| The game's shaders | 54 stages from 28 programs compile with `uam` |
| The renderer's own shaders | all compile with `uam` |
| The repository holds no game data or personal data | `tools/check_repo.py`, also on CI |

## Not checked yet

Everything that needs the console: the boot, the picture, input, sound, and every stage below.

## Roadmap

| Stage | State |
| --- | --- |
| Data, file system on Horizon | built, not run |
| Platform layer: threads, memory, input, audio, network | built, not run |
| Boot on the console, logs in `SD_LOG` | **next: waiting on the first test** |
| Asset loading: `.pk4`, materials, `.dds` images | built, not run |
| Native deko3D renderer: menus, HUD, levels, shadows | built, not run |
| Portals, wall walking, spirit walk | not run |
| Multiplayer | not run |
| Back end on its own thread (core 1) | to do |
| NEON versions of `idSIMD` for skinning and shadows | to do |
| 60 fps at 1020/460/1331 MHz | not measured. The first port reached 30 fps with shadows off, and spent most of each frame in the OpenGL driver, which is gone |

## Known gaps

| Gap | Note |
| --- | --- |
| Deathwalk | when the player dies the level's deathwalk is not entered, as in openPREY: the engine cannot yet append its map |
| Glow overlay | Prey's glow pass is not drawn yet |
| Brightness and gamma | the menu's settings have no effect yet |
| Docked resolution | always 1280x720, scaled by the system |
| Button prompts | show the Switch's names (A, ZR, D-Pad Up), not icons yet |
| Weapon wheel | L and R step through the game's own weapon list; there is no wheel yet |
| Render demos and screenshots | not available |
