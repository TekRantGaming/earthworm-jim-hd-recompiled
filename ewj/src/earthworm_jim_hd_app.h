// earthworm_jim_hd - ReXGlue Recompiled Project
//
// The app: GPU/audio setup, the TRG launcher before the runtime starts, port
// defaults, achievement art and notifications.

#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>

#include <imgui.h>

#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/kernel_state.h>
#include <rex/system/util/xdbf_utils.h>
#include <rex/system/xcontent.h>
#include <rex/system/xmemory.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "trg/game_helpers.h"

#include "art.h"
#include "frame_stats.h"
#include "launcher.h"
#include "overlay.h"
#include "platform.h"
#include "settings.h"
#include "toast.h"
#include "updater.h"

namespace ewj {
void InstallFpeGuard();  // fpe_guard.cpp
}

class EarthwormJimHdApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<EarthwormJimHdApp>(new EarthwormJimHdApp(ctx, "earthworm_jim_hd", PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    // Register the GPU plugin's cvars before the config is read so the
    // launcher can edit and save them, and set the port's own defaults.
    ewj::update::CleanUpPreviousUpdate();  // *.old files from the last update
    ewj::PreloadGpuPlugin();
    ewj::ApplyPortDefaults();
    // An AppImage runs from a read-only mount, so keep the game files and the
    // settings beside the .AppImage file instead of beside the program.
    std::filesystem::path base = rex::filesystem::GetExecutableFolder();
    if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage) {
      const auto exe_dir = base;
      base = std::filesystem::path(appimage).parent_path();
      if (paths.config_path.empty() || paths.config_path.parent_path() == exe_dir)
        paths.config_path = base / (paths.config_path.empty() ? std::filesystem::path("earthworm_jim_hd.toml")
                                                              : paths.config_path.filename());
      if (REXCVAR_GET(log_file).empty()) {
        std::error_code ec;
        std::filesystem::create_directories(base / "logs", ec);
        ewj::SetCvarDefault("log_file", (base / "logs" / "earthworm_jim_hd.log").string());
      }
    }
    if (paths.game_data_root.empty()) paths.game_data_root = base / "game";
    // Achievement names and icons, written out of the player's default.xex on first run.
    if (paths.metadata_root.empty()) paths.metadata_root = ewj::art::AchievementDir(paths.game_data_root);
  }

  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    user_data_root_ = defaults.user_data_root;
    const bool skip_once = REXCVAR_GET(ewj_skip_launcher);
    rex::cvar::ResetToDefault("ewj_skip_launcher");  // never persist it
    const bool files_ok =
        ewj::GameFilesPresent(defaults.game_data_root) && ewj::GameVersionMatches(defaults.game_data_root);
    const bool show = !files_ok || ewj::IsShiftHeld() || (REXCVAR_GET(ewj_launcher) && !skip_once);
    if (!show) {
      ewj::ApplyRenderPreset(OutputSize().second);
      return defaults;
    }

    ewj::LauncherCallbacks cb;
    cb.play = [this, resume, defaults] {
      app_context().CallInUIThreadDeferred([this, resume, defaults] {
        ewj::ApplyRenderPreset(OutputSize().second);
        resume(defaults);
      });
    };
    cb.restart_and_play = [this] {
      ewj::RelaunchSelf(L"--ewj_skip_launcher=true");
      app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); });
    };
    cb.quit = [this] { app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); }); };
    cb.set_fullscreen = [this](bool on) {
      if (window()) window()->SetFullscreen(on);
    };
    cb.dpi_scale = [this] { return window() ? double(window()->GetDpi()) / window()->GetMediumDpi() : 1.0; };
    cb.screen_size = [] { return ewj::PrimaryScreenSize(); };
    cb.output_size = [this] { return OutputSize(); };
    ewj::ShowLauncher(imgui_drawer(), immediate_drawer(),
                      {defaults.game_data_root, defaults.user_data_root, defaults.config_path}, std::move(cb));
    return std::nullopt;
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override { ewj::LoadUiFont(atlas); }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics && config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
    if (!config.audio_factory) config.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
  }

  void OnPostSetup() override {
    ewj::InstallFpeGuard();  // after the runtime's own signal handlers
    REXLOG_INFO("EWJ: {}", trg::TuneProcessScheduling());
    {
      const std::filesystem::path log = REXCVAR_GET(log_file);
      const auto logs = log.has_parent_path() ? log.parent_path() : rex::filesystem::GetExecutableFolder() / "logs";
      trg::InstallCrashReports(logs.string(), "Earthworm Jim HD");
    }
    ewj::ApplyRuntimeOverrides();

    SeedTitleSpecificProfileSettings();
    ExportAchievementArt();
    // Give the launcher the achievement names (read from the game by the runtime).
    if (!user_data_root_.empty())
      ewj::art::WriteAchievementCache(achievements().ListAchievements(),
                                      ewj::art::AchievementCachePath(user_data_root_));
    ScheduleTitleCapture();
    ScheduleWelcomeAchievement();

    // Debug aid: set EWJ_DUMP_IMAGE=<file> to write the decrypted guest image
    // for offline analysis (tools/FindMissingFuncs.cs).
    const char* dump_path = std::getenv("EWJ_DUMP_IMAGE");
    if (!dump_path || !*dump_path) return;
    const uint8_t* base = runtime()->virtual_membase();
    std::ofstream out(dump_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(base + PPCImageConfig.image_base), PPCImageConfig.image_size);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    SetGuestFrameStats(ewj::GetGuestFrameStats);  // F3 overlay "Guest: N FPS"
    ewj::CreateFpsOverlay(drawer);
  }

  // Xbox 360-style toast with a chime for the game's achievements.
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override {
    auto toast = std::make_unique<ewj::AchievementToast>(imgui_drawer(), immediate_drawer(), game_data_root(),
                                                         user_data_root_);
    toast_ = toast.get();
    return toast;
  }

 private:
  // Size the game is shown at: the monitor in fullscreen, else the window.
  std::pair<int, int> OutputSize() const {
    if (REXCVAR_QUERY(bool, fullscreen) || !window()) return ewj::PrimaryScreenSize();
    return {int(window()->GetActualPhysicalWidth()), int(window()->GetActualPhysicalHeight())};
  }

  // The game keeps its progress and options in the profile's three
  // title-specific blobs (XPROFILE_TITLE_SPECIFIC1-3, read together by
  // sub_82F34B50). ReXGlue answers a read of a blob that was never written
  // with data type 0 ("unset"); a real console answers "binary, length 0".
  // The game rejects the former with "Can't Read Gamer Profile" and then has
  // nowhere to save. Store an empty binary blob for any that has no saved
  // data yet; GetSetting loads a saved one first, so saves are never touched.
  // ReXGlue writes and reloads them (Documents/earthworm_jim_hd/...) itself.
  void SeedTitleSpecificProfileSettings() {
    auto* ks = runtime() ? runtime()->kernel_state() : nullptr;
    auto* profile = ks ? ks->user_profile() : nullptr;
    if (!profile) return;
    for (uint32_t id : {0x63E83FFFu, 0x63E83FFEu, 0x63E83FFDu}) {
      auto* setting = profile->GetSetting(id);
      if (setting && setting->is_set) continue;
      profile->AddSetting(std::make_unique<rex::system::xam::UserProfile::BinarySetting>(id, std::vector<uint8_t>{}));
      REXLOG_INFO("EWJ: created empty profile setting {:08X} (new profile)", id);
    }
  }

  // The launcher shows the achievements' names and pictures from
  // game/achievements. On the first run after installing, write them out of
  // the game's own executable (its XDBF resource).
  void ExportAchievementArt() {
    const auto dir = ewj::art::AchievementDir(game_data_root());
    std::error_code ec;
    if (std::filesystem::exists(dir / "achievements.toml", ec)) return;
    auto* ks = runtime() ? runtime()->kernel_state() : nullptr;
    if (!ks) return;
    const auto db = ks->title_xdbf();
    if (!db.is_valid()) return;
    std::filesystem::create_directories(dir / "icons", ec);
    const auto lang = db.GetExistingLanguage(rex::system::XLanguage::kEnglish);
    auto quote = [](const std::string& s) {
      std::string out = "\"";
      for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
      }
      return out + "\"";
    };
    std::ofstream toml(dir / "achievements.toml", std::ios::binary);
    for (const auto& a : db.GetAchievements()) {
      const uint32_t image = a.image_id;
      toml << "[[achievements]]\n"
           << "id = " << uint32_t(a.id) << "\n"
           << "label = " << quote(db.GetStringTableEntry(lang, a.label_id)) << "\n"
           << "description = " << quote(db.GetStringTableEntry(lang, a.description_id)) << "\n"
           << "unachieved_description = " << quote(db.GetStringTableEntry(lang, a.unachieved_id)) << "\n"
           << "gamerscore = " << uint32_t(a.gamerscore) << "\n"
           << "image_id = " << image << "\n"
           << "icon_path = \"icons/" << image << ".png\"\n\n";
      const auto block = db.GetEntry(rex::system::util::XdbfSection::kImage, image);
      if (block.buffer && block.size) {
        std::ofstream png(dir / "icons" / (std::to_string(image) + ".png"), std::ios::binary);
        png.write(reinterpret_cast<const char*>(block.buffer), std::streamsize(block.size));
      }
    }
    REXLOG_INFO("EWJ: wrote achievement list and icons to {}", dir.string());
  }

  // On first play, keep a frame of the game's title screen as launcher art.
  void ScheduleTitleCapture() {
    if (user_data_root_.empty()) return;
    const auto path = ewj::art::TitleCapturePath(user_data_root_);
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return;
    ewj::RunAfterFirstFrame(kTitleCaptureDelay, [this, path] {
      app_context().CallInUIThread([this, path] {
        rex::ui::RawImage image;
        auto* gfx = runtime() ? runtime()->graphics_system() : nullptr;
        auto* presenter = gfx ? gfx->presenter() : nullptr;
        if (presenter && presenter->CaptureGuestOutput(image) && ewj::art::SaveTitleCapture(image, path))
          REXLOG_INFO("EWJ: saved launcher art {}x{}", image.width, image.height);
      });
    });
  }

  // The port's own "welcome" achievement: unlocks a few seconds into the first
  // play so players learn the game has achievements.
  void ScheduleWelcomeAchievement() {
    const auto& welcome = ewj::PortAchievements().front();
    if (user_data_root_.empty() || ewj::IsPortAchievementUnlocked(user_data_root_, welcome.id)) return;
    ewj::RunAfterFirstFrame(6.0, [this, &welcome] {
      if (ewj::UnlockPortAchievement(user_data_root_, welcome.id) && toast_) toast_->Show(welcome.title, 0, 0);
    });
  }

  // Seconds after the first frame when the title screen is showing.
  static constexpr double kTitleCaptureDelay = 20.0;

  std::filesystem::path user_data_root_;
  ewj::AchievementToast* toast_ = nullptr;  // owned by ReXApp
};
