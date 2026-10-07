## Earthworm Jim HD PC Port v0.9.0 (preview)

Play the Xbox 360 edition of Earthworm Jim HD natively on Windows, with the TRG launcher and modern PC options.

### Install
1. Unzip **EarthwormJimHD-v0.9.0-windows-x64.zip** anywhere and run **earthworm_jim_hd.exe**.
2. On the launcher's **Play** page click **Install from package...** and pick your own Earthworm Jim HD Xbox Live Arcade package.
3. Press **PLAY**.

**No game files are included.** You need your own package, this exact release:

| | |
| --- | --- |
| Title ID | `584109E2` (Xbox Live Arcade, content type `000D0000`) |
| Package file | `8510D3A10187458FD69203D1E4067E736923865658` (455,823,360 bytes), found in `Content\0000000000000000\584109E2\000D0000\` on an Xbox 360 drive |
| Game version | `default.xex` 1.0.0.11 (2010-04-27), 26,726,400 bytes, CRC32 `D6366187`, no title update |

The launcher checks both and explains if your package is a different version.

### What's in it
- The TRG launcher, with art from your copy of the game
- Up to 8K render resolution with quality presets, FXAA, 2x MSAA and 16x texture filtering
- Frame-rate caps from 30 to unlimited at the game's normal speed (currently tops out around 110 FPS)
- Letterbox or stretch, windowed or fullscreen, monitor choice
- All six languages
- Saves work (fixed: the game could not read its profile data under the runtime)
- Xbox 360 style achievement pop-ups with your choice of sound
- Deadzone, vibration, button remapping and keyboard play
- The full game unlocked (switch to the trial on the Play page if you want)
- **Updates**: the launcher checks GitHub when it opens and offers to install new versions (About page: Ask / Automatic / Off)

### Other download
**EarthwormJimHD-Builder** builds the same thing on your own PC from source and your package (about 15 to 30 minutes).

### This is a preview
Tested from the title screen into the first level, with saving and reloading. Please report crashes with the `crash-*.txt` and `.dmp` files from the `logs` folder next to the game.

Requires Windows 10 or 11 (64-bit) and a DirectX 12 graphics card.

This port was made with AI (Claude Code). See the README for details.
