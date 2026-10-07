## Earthworm Jim HD PC Port v0.9.4

### Fixed
- **Antivirus warnings on the Windows download.** The warnings came from `rexruntime.dll`, the runtime from the ReXGlue SDK v0.10.0 that the port is built on: several antivirus engines, Windows Defender among them, wrongly flag that particular build of it (reported upstream: https://github.com/rexglue/rexglue-sdk/issues/485). This release ships `rexruntime.dll` built from the same ReXGlue v0.10.0 source, so the game itself is unchanged.

Nothing else changed since v0.9.3.

### How to update
Open the launcher: it offers the update and installs it for you (or **About > Check now**). Your game files, saves and settings are kept. If Windows quarantined a file from an earlier version, download this release fresh instead.

### New install
- **Windows:** unzip **EarthwormJimHD-v0.9.4-windows-x64.zip** anywhere, run **earthworm_jim_hd.exe**, click **Install from package...** on the **Play** page and pick your own Earthworm Jim HD Xbox Live Arcade package (title ID `584109E2`, game version 1.0.0.11; see the README for the exact file).
- **Linux / Steam Deck:** download **EarthwormJimHD-v0.9.4-linux-x86_64.AppImage**, make it executable and run it; the README has the Steam Deck steps.

**No game files are included.**

### SHA-256
- `EarthwormJimHD-v0.9.4-windows-x64.zip`: `76f5b79823718760222ad68c320d388b5937fe87deecbf7d20566318c8c5c734`
- `EarthwormJimHD-v0.9.4-linux-x86_64.AppImage`: `4828f5ddae29d3ae36ce47f436084ad9cd40234c116f2b4547b9627cf015a2f5`
- `rexruntime.dll` (inside the zip): `e87c3555602c41b18579ef8932639f7afc9324b0ca448f6e7608010d10edbbc3`
