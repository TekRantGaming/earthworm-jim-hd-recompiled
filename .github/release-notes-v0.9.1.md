## Earthworm Jim HD PC Port v0.9.1

### Fixed
- **Crash when choosing Continue** ([#2](https://github.com/TekRantGaming/earthworm-jim-hd-recompiled/issues/2)). The game reads its leaderboard stats when you continue a saved game, and the runtime answered that request with garbage, so the game crashed. It now answers the way an Xbox 360 does when it isn't signed in to Xbox Live: the game skips the leaderboard data and carries on. Your saves were never affected.

### How to update
Open the launcher: it offers the update and installs it for you (or use **About > Check now**). You can also download **EarthwormJimHD-v0.9.1-windows-x64.zip** below and copy it over your current folder. Your `game` folder, saves and settings are kept.

### New install
1. Unzip **EarthwormJimHD-v0.9.1-windows-x64.zip** anywhere and run **earthworm_jim_hd.exe**.
2. On the **Play** page click **Install from package...** and pick your own Earthworm Jim HD Xbox Live Arcade package (title ID `584109E2`, game version 1.0.0.11; see the README for the exact file).
3. Press **PLAY**.

**No game files are included.**

Requires Windows 10 or 11 (64-bit) and a DirectX 12 graphics card.
