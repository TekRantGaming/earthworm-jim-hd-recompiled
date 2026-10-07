// Pre-game launcher, built on TRG Launcher (third_party/trg-launcher): installs
// the game from the player's own Xbox Live Arcade package and edits display,
// graphics, gameplay and control settings before the runtime starts (shown
// from OnFinalizePaths). Its art comes from the player's own game files.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <utility>

namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace ewj {

struct LauncherCallbacks {
  std::function<void()> play;                // start the game (called deferred, UI thread)
  std::function<void()> restart_and_play;    // relaunch with saved settings, skip launcher
  std::function<void()> quit;
  std::function<void(bool)> set_fullscreen;  // apply window mode live
  std::function<double()> dpi_scale;         // window DPI / 96
  std::function<std::pair<int, int>()> screen_size;  // monitor size in physical pixels
  std::function<std::pair<int, int>()> output_size;  // size the game will be shown at
};

struct LauncherPaths {
  std::filesystem::path game_dir;
  std::filesystem::path user_dir;
  std::filesystem::path config_path;
};

// Earthworm Jim HD's Xbox Live Arcade title ID.
constexpr uint32_t kTitleId = 0x584109E2;

// The one default.xex the port is built for: version 1.0.0.11, 2010-04-27.
constexpr uint64_t kXexSize = 26726400;
constexpr uint32_t kXexCrc32 = 0xD6366187;

// True when default.xex under `game_dir` is that exact version (cached per folder).
bool GameVersionMatches(const std::filesystem::path& game_dir);

// True when the game files needed to boot exist under `game_dir`.
bool GameFilesPresent(const std::filesystem::path& game_dir);

// Loads the GPU plugin early so its cvars (resolution scale, FXAA, ...) are
// registered before the config is read and can be edited by the launcher.
void PreloadGpuPlugin();

// Creates the launcher dialog; it deletes itself when closed.
void ShowLauncher(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, LauncherPaths paths,
                  LauncherCallbacks callbacks);

}  // namespace ewj
