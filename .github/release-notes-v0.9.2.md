## Earthworm Jim HD PC Port v0.9.2

### Fixed
- **Crash when choosing Continue, for real this time** ([#2](https://github.com/TekRantGaming/earthworm-jim-hd-recompiled/issues/2)). v0.9.1 only fixed half of it. Continue opens the level select, which reads the leaderboards; without Xbox Live the game still crashed on one of those reads. The port now answers like a leaderboard with no entries, so Level Select opens and shows "World record: No data...". Tested with a save that has Continue on the main menu.

### New: Linux and Steam Deck
**EarthwormJimHD-v0.9.2-linux-x86_64.AppImage** runs natively on Linux and the Steam Deck.

On a Steam Deck:
1. In **Desktop Mode**, download the AppImage, right-click it > **Properties** > **Permissions** and tick **Is executable**.
2. Copy your Earthworm Jim HD package to the Deck (USB stick, SD card or network).
3. Double-click the AppImage. On the **Play** page click **Install from package...** and pick your package. The game files go in a `game` folder next to the AppImage.
4. In Steam, **Games > Add a Non-Steam Game** and pick the AppImage, then play it from **Game Mode**.

In the launcher, use the touchscreen or the trackpad to change settings; **Start** presses Play. You can turn the launcher off on the Play page (hold Shift at start to bring it back).

### How to update
Open the launcher: it offers the update and installs it for you (or **About > Check now**). On Windows it replaces the program files; on Linux it replaces the AppImage. Your game files, saves and settings are kept.

### New install (Windows)
1. Unzip **EarthwormJimHD-v0.9.2-windows-x64.zip** anywhere and run **earthworm_jim_hd.exe**.
2. On the **Play** page click **Install from package...** and pick your own Earthworm Jim HD Xbox Live Arcade package (title ID `584109E2`, game version 1.0.0.11; see the README for the exact file).
3. Press **PLAY**.

**No game files are included.**

Requires Windows 10 or 11 (64-bit) and a DirectX 12 graphics card, or a 64-bit Linux system / Steam Deck with Vulkan.
