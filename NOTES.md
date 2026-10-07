# Earthworm Jim HD: Xbox 360 static recompilation

Toolchain: [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) v0.10.0 (prebuilt, in `tools/rexglue/win-amd64`).
Build: VS 2022 Build Tools (clang 19.1.5), CMake, Ninja. Launcher: [TRG Launcher](https://github.com/TekRantGaming/trg-launcher)
v1.1.0 as a submodule (`third_party/trg-launcher`), the first port to use it as a library.

## Game
- Package: XBLA STFS `LIVE` file `584109E2/000D0000/8510D3A1...` (supply your own)
- Title ID `584109E2`, version 1.0.0.11, built 2010-04-27 (Gameloft). XEX is devkit-key encrypted.
- Contents: `default.xex` (26 MB), `data*.dat` (18 archives, 206 MB), `data/sounds/xbox/*.xwb` (XACT wave banks,
  186 MB), achievement/avatar art (`1.png`-`12.png`, `ArcadeInfo.xml`, `EWJ_icon.png`, `feathered_EWJ_banner.png`).

## Quick start
```
setup.ps1 -Package <path to your LIVE package>   # SDK + extract + codegen
ewj\build.bat                                    # configure + build (ewj-relwithdebinfo by default)
ewj\run.bat                                      # launcher first; hold Shift to force it when disabled
```

## Layout
| Path | What |
|---|---|
| `tools/extract_stfs.ps1`, `tools/StfsExtract.cs` | STFS extractor (no Python needed) |
| `tools/dump_image/` | `ewj_dump_image`: writes the decrypted guest image (`ewj/image.bin`) for analysis |
| `tools/ppcdis.py` | Disassembles the dumped image (capstone) |
| `tools/find_thunks.py`, `find_veneers.py`, `find_switch_tables.py`, `find_missing_funcs.py`, `seed_unresolved.py`, `find_callers.py`, `find_refs.py`, `find_setjmp.py` | Analysis helpers (below) |
| `tools/patch_setjmp.cmake` | Post-codegen fix for setjmp/longjmp via veneers (run after every codegen) |
| `tools/dev_build.bat ON/OFF` | Quick rebuild with `src/dev_trace.cpp` tracing hooks (`EWJ_DEV_TOOLS`) |
| `ewj/` | ReXGlue project (`assets/` = junction to `game/`, `generated/` = recompiled C++) |
| `ewj/overrides.toml` | Hand-made analysis fixes |
| `ewj/*_funcs.toml` | Generated function lists |
| `ewj/src/launcher.cpp` | The TRG launcher pages for this port |

## Image layout (why the analysis needed help)
Code is not only in `.text`:

| Section | Range | Contents |
|---|---|---|
| `.xidata` | `0x826F0000-0x827E55C0` | 58,715 long-branch veneers (`lis r11; addi r11; mtctr r11; bctr`) + C++ thunks |
| `.xidata` | `0x827E55C0-0x82819875` | import thunks |
| `.text` | `0x82819A00-0x83658000` | game code |
| `.embsec_` x8 | `0x83658000-0x8382C467` | more code, a **second identical veneer table**, a second copy of the register save/restore helpers |

- Far calls and function pointers go through veneers, so imports are never `bl`'d directly: e.g. XamInputGetState
  (thunk `0x827E5610`) is reached via veneers `0x826F25E0` / `0x83707360`. `tools/find_callers.py` follows veneers.
- Thread entries and callbacks are veneer addresses (first runtime failure: `XThread::Execute - No function registered
  at 82796560`, a veneer). `find_veneers.py` registers all 117,430 veneers and their targets (`veneer_funcs.toml`).
- C++ this-adjusting thunks (`addi r3,r3,N; b target`, 8 bytes) are packed back to back; the analyzer saw each run as
  one function and resolved one target per codegen pass. `find_thunks.py` -> `thunk_funcs.toml`.
- Tail-call-only targets (`b` without `bl`): seeded from the codegen error list by `seed_unresolved.py` ->
  `tailcall_funcs.toml` (first run: 3003 unresolved branches).
- Register helpers exist twice (`0x829A8DD0..` and `0x83250E20..`). ReXGlue registers only the first copy
  (`detectSaveRestoreHelpers` takes the first pattern match). Tail calls into the second copy's `__restgprlr_N`
  are declared in `overrides.toml` as 4-byte functions (config functions can't overlap) and replaced by correct
  hand-written versions in `src/abi_helpers.cpp` (weak generated symbols are overridden).

- **setjmp/longjmp** (`setjmp_address = 0x83259AE0`, `longjmp_address = 0x832596B0` in overrides.toml) are
  only ever called through veneers (setjmp: `0x827CFFC0`/`0x837E4D40`, longjmp: `0x82728C90`/`0x8373DA10`), so
  ReXGlue never emits ppc_setjmp/ppc_longjmp for them. libjpeg's error handler longjmps: without the fix guest
  registers were restored but the host stack was not (crash: read of guest 0x1A4 in `sub_83391F80`).
  `tools/patch_setjmp.cmake` rewrites the 50 call sites after every codegen (setup.ps1 runs it).
- **Jump tables**: ReXGlue sometimes recovers fewer cases than a table has; an index past them hits
  `__builtin_trap` (crash 0xC000001D in `sub_82AAFB28`: table bounded at 32, only case 0 recovered).
  `tools/find_switch_tables.py` reads all 217 tables from the image (absolute `lwzx` tables, inline tables,
  byte/halfword offset tables; bound from `cmplwi`, a `clrlwi` mask, or the function extent) into
  `switch_tables.toml`. Four entries of the 6-bit table at `0x82C062E4` point into the next function and are
  sent to the table's default handler.

- **Texture fetch constants with type 0**: the game binds real textures whose fetch-constant type field is 0
  ("invalid"); the runtime unbound them (11,540 warnings in the first 9-minute playtest). Port default
  `gpu_allow_invalid_fetch_constants = true` (`ApplyPortDefaults`): 0 warnings since.

## Hooks
- `sub_82C211F8` = D3D swap (only caller of VdSwap): FPS counter + frame limiter (`src/frame_stats.cpp`).
- `sub_82BEFD18` = XDK `XInputGetState(user, state)` (tail-calls XamInputGetState(user, 1, state)): remap,
  deadzone, inversion (`src/input_remap.cpp`). `sub_82BEFD28` = XDK `XInputSetState`: vibration strength.

## Frame rate (src/frame_stats.cpp)
- The game paces itself: `main()` (`0x82BEEA48`) reads a millisecond clock, skips the frame while less than the
  minimum frame time at `0x8384465C` has passed (16 ms, set at startup -> ~62.5 FPS), then calls the game's update
  with the elapsed milliseconds (`bctrl` at `0x82BEED90`, r4 = delta; also stored at `0x838D27C8`).
- Game logic runs on that delta, so above 60 the port writes a smaller minimum (`1000 / ewj_frame_rate`, 1 for
  unlimited) every frame (`ApplyGameFrameCap`) and `LimitFrameRate()` does the exact pacing. Measured with the
  dev-build trace (sum of the game's deltas vs wall clock): game time x1.000 at 60, 120, 240 and unlimited.
- Main-loop call chain (dev trace): swap `sub_82C211F8` <- `82DA0118` <- `82BEEE18` (main) <- `82BF1EB0` (CRT start).

## Display
- **Letterbox fix**: ReXGlue's `VdQueryVideoMode` follows `window_width/height` when they are set and
  `video_mode_width/height` are not, so a launcher window size (e.g. 800x600) told the game the TV was 4:3; the game
  still draws 16:9 and the presenter stretched it whatever `present_letterbox` said. `PinGuestVideoMode()`
  keeps the guest mode at 1280x720 (clears the registered default so ReXGlue's "non-default" check passes); the
  pinned cvars are never saved or reset.
- Window sizes are 96-DPI units (at 250% scaling 1280x720 opens at 3200x1800 physical).

## Settings file
- `SaveSettings` used to drop any setting overridden on the command line, erasing the player's saved value; it now
  keeps the file's own line for those settings.

## Launcher (src/launcher.cpp)
- `trg::Launcher` embedded in a `rex::ui::ImGuiDialog`, shown from `OnFinalizePaths` (window + ImGui exist,
  runtime not yet built) when `ewj_launcher`, Shift held, or game files missing.
- trg-launcher links the ImGui inside `rexruntime.dll` (`ewj_imgui` INTERFACE target -> `rex::runtime`), so there
  is one ImGui context. `TRG_LAUNCHER_FETCH_DEPS` is off.
- Settings: `trg::CallbackSettings` over ReXGlue cvars, saved to `earthworm_jim_hd.toml` (non-defaults only,
  command-line overrides never saved). `restart_keys` = presenter/window cvars -> relaunch with
  `--ewj_skip_launcher=true`.
- Pages: Play (STFS install with title check `584109E2`), Display, Graphics, Gameplay, Controls, Achievements, About.
- Art only from the player's files: title-screen capture (first play), else the package's own banner; icon =
  `EWJ_icon.png`; achievement names/icons written out of the XEX's XDBF on first run.
- `TRG_LAUNCHER_AUTOPLAY=1` presses PLAY by itself (automated boot tests).

## Playtest 1 (2026-10-06, ~9 min, monitored)
- No crashes; clean exit. ~60 FPS average, 1% lows 45-54 FPS, two hitches over 100 ms.
- Found and fixed: invalid-type texture fetch constants (above).
- Harmless: game probes a file named `image` (game:\ then bare), like `#DefaultFont`; it carries on.
- Shader cache after the session: 4 KB `.xsh`, 1.2 KB `.xpso` (too small for a shader pack yet).

## Status (2026-10-06)
- Boots through the launcher to the intro and into the first level's attract mode; 3-minute soak at 4K/60 FPS with
  no errors. Not yet played with input: menus, levels, saving, achievements, audio quality all untested.
- Known harmless log noise: `update:\` (no title update mounted) and `#DefaultFont` probes fail; the game falls back
  to `game:\`.
