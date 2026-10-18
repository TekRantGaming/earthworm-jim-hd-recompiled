## Earthworm Jim HD PC Port v0.9.6

### Fixed
- **VSync on Linux and Steam Deck** ([#5](https://github.com/TekRantGaming/earthworm-jim-hd-recompiled/issues/5)): the launcher's VSync switch could not be turned on, because it changed a Windows-only setting. It now works on Linux too (pressing Play restarts the game to apply it). Thanks to @ZacOnGH for the report.

This build ships the same antivirus-clean `rexruntime.dll` as v0.9.4 (SHA-256 `e87c3555602c41b18579ef8932639f7afc9324b0ca448f6e7608010d10edbbc3`).

### How to update
Open the launcher: it offers the update and installs it for you (or **About > Check now**). Your game files, saves and settings are kept.

### New install
- **Windows:** unzip **EarthwormJimHD-v0.9.6-windows-x64.zip** anywhere, run **earthworm_jim_hd.exe**, click **Install from package...** on the **Play** page and pick your own Earthworm Jim HD Xbox Live Arcade package (title ID `584109E2`, game version 1.0.0.11; see the README for the exact file).
- **Linux / Steam Deck:** download **EarthwormJimHD-v0.9.6-linux-x86_64.AppImage**, make it executable and run it; the README has the Steam Deck steps.

**No game files are included.**

### SHA-256 and VirusTotal
- `EarthwormJimHD-v0.9.6-windows-x64.zip`: `2c940956830f735b9c9ff31e2e590eb955ad4776ba20606ba1aa3beec8383bc7` ([VirusTotal](https://www.virustotal.com/gui/file/2c940956830f735b9c9ff31e2e590eb955ad4776ba20606ba1aa3beec8383bc7))
- `EarthwormJimHD-v0.9.6-linux-x86_64.AppImage`: `19d04f23268b9716d8e80808b6434aa98deb2cbe6eba19879bc0255d2e372c00` ([VirusTotal](https://www.virustotal.com/gui/file/19d04f23268b9716d8e80808b6434aa98deb2cbe6eba19879bc0255d2e372c00))
