# Testing

## On the console

1. Build, then fill `SD_CARD/`:

   ```bash
   ./build.sh
   python tools/make_sd.py --base game_data/base --shaders game_data/shaders
   ```

2. Copy `SD_CARD/` to the card once. After that only `switch/prey/prey.nro` changes.
3. Copy `sd:/switch/prey/SD_LOG/` back to `SD_LOG/` after each run.

For faster rounds, without taking the card out:

```bash
nxlink -s -a <console address> build/prey.nro
```

That prints the log in the terminal as it runs. Start the homebrew menu's NetLoader first (Y).

## Reading a crash

`prey_crash.txt` gives addresses in the executable as `elf offset`:

```bash
aarch64-none-elf-addr2line -f -C -e build/prey.elf <offset> <offset>
```

The build keeps frame pointers, so the call stack is reliable.

## Measuring

| Cvar | Use |
| --- | --- |
| `com_showFPS 1` | fps on screen |
| `r_dkLogPerf 1` | back end time, draws and memory in the log, once a second |
| `com_speeds 1` | game, front end and back end time, on screen |
| `r_shadows 0` | stencil shadows off, to see their cost |

## What to check before calling a stage done

| Stage | Check |
| --- | --- |
| Data | the log lists all the `.pk4` files, and no `Couldn't load image` at the main menu |
| Boot | the main menu draws, and the pointer moves |
| Level | the game starts a new game and reaches the roadhouse |
| Shaders | no `no shader for program` in the log |
| Prey's mechanics | a portal draws the room behind it, wall walking turns the view, spirit walk has its filter, the wrench level's gravity works |
| Memory | no `Out of memory` after three map changes |
| Speed | `r_dkLogPerf`, at the same places each time |
