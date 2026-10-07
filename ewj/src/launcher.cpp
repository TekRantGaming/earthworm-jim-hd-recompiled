#include "launcher.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <imgui.h>
#include <toml++/toml.hpp>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

#include "trg/headers.h"
#include "trg/launcher.h"
#include "trg/platform.h"

#include "art.h"
#include "platform.h"
#include "settings.h"
#include "stfs.h"
#include "toast.h"
#include "updater.h"

namespace ewj {
namespace {

// Settings the presenter/window read before the launcher runs; changing them
// needs a relaunch to take effect.
const std::vector<std::string> kRestartCvars = {"present_effect", "window_width", "window_height", "monitor"};

// Lime accent over the stock Midnight blues.
const ImVec4 kAccent = ImVec4(0.60f, 0.86f, 0.18f, 1.0f);

struct Achievement {
  uint32_t id = 0;
  std::string label, description, unachieved;
  uint32_t gamerscore = 0;
  bool unlocked = false;
  ImTextureID icon{};
};

ImTextureID Tex(rex::ui::ImmediateTexture* t) { return reinterpret_cast<ImTextureID>(t); }

const rex::cvar::FlagEntry* FindFlag(std::string_view name) {
  for (auto& e : rex::cvar::GetRegistry())
    if (e.name == name) return &e;
  return nullptr;
}

// Only offer the values a cvar accepts in this build (e.g. present_effect has
// no FidelityFX options in the prebuilt SDK).
std::vector<trg::Option> Allowed(const char* cvar, std::vector<trg::Option> options) {
  if (const auto* flag = FindFlag(cvar); flag && !flag->constraints.allowed_values.empty()) {
    const auto& allowed = flag->constraints.allowed_values;
    std::erase_if(options, [&](const trg::Option& o) {
      return std::find(allowed.begin(), allowed.end(), o.value) == allowed.end();
    });
  }
  return options;
}

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, LauncherPaths paths,
                 LauncherCallbacks callbacks)
      : ImGuiDialog(drawer), immediate_(immediate), paths_(std::move(paths)), cb_(std::move(callbacks)) {
    files_ok_ = GameFilesPresent(paths_.game_dir);
    BindSettings();
    LoadArt();
    toast_ = std::make_unique<AchievementToast>(drawer, immediate_, paths_.game_dir, paths_.user_dir);
    launcher_ = std::make_unique<trg::Launcher>(MakeConfig());
    if (settings_.Get("ewj_updates", "ask") != "off") StartUpdateCheck();
    if (!files_ok_) launcher_->GoToPage(0);
  }

  ~LauncherDialog() override {
    // The frame that closes the launcher is drawn after this dialog is gone:
    // keep its pictures for the rest of the run.
    for (auto& texture : textures_) KeepTextureAlive(std::move(texture));
  }

 protected:
  void OnDraw(ImGuiIO&) override {
    if (done_) return;
    PollUpdates();
    if (done_) return;
    switch (launcher_->Frame()) {
      case trg::Result::kPlay: StartGame(); break;
      case trg::Result::kQuit:
        done_ = true;
        if (cb_.quit) cb_.quit();
        break;
      case trg::Result::kNone: break;
    }
  }

 private:
  // ----------------------------------------------------------- settings ---
  void BindSettings() {
    settings_.true_value = "true";
    settings_.false_value = "false";
    settings_.get = [](std::string_view k) { return rex::cvar::GetFlagByName(std::string(k)); };
    settings_.set = [this](std::string_view k, const std::string& v) {
      rex::cvar::SetFlagByName(std::string(k), v);
      if (k == "fullscreen" && cb_.set_fullscreen) cb_.set_fullscreen(v == "true");
    };
    settings_.save = [this] { return SaveSettings(paths_.config_path); };
    settings_.reset = [this] {
      for (auto& e : rex::cvar::GetRegistry()) {
        if (e.type == rex::cvar::FlagType::Command || IsPinnedCvar(e.name)) continue;
        if (e.source == rex::cvar::Source::kCommandLine || e.source == rex::cvar::Source::kEnvironment) continue;
        rex::cvar::ResetToDefault(e.name);
      }
      if (cb_.set_fullscreen) cb_.set_fullscreen(REXCVAR_QUERY(bool, fullscreen));
    };
  }

  trg::LauncherConfig MakeConfig() {
    trg::LauncherConfig c;
    c.settings = &settings_;
    c.theme = trg::Theme::Midnight().WithAccent(kAccent);
    const UiFonts& f = GetUiFonts();
    c.fonts.regular = f.regular;
    c.fonts.semibold = f.semibold;
    c.fonts.bold = f.bold;
    c.branding.title = "EARTHWORM JIM HD";
    c.branding.icon = title_icon_;
    // The game's own art once we have it (title capture, else the package's
    // banner), a purple-planet starfield until then.
    c.branding.background = trg::headers::Image(&header_art_, &header_aspect_, header_v_offset_,
                                                trg::headers::Space({ImVec4(0.78f, 0.36f, 0.62f, 1.0f), 170}));
    c.pages = {
        {"Play", "Install the game from your own Xbox Live Arcade package and start playing.",
         [this](trg::Ui& ui) { PagePlay(ui); }, [this] { return !files_ok_; }},
        {"Display", "Window, monitor and how the picture fits your screen.", [this](trg::Ui& ui) { PageDisplay(ui); }},
        {"Graphics", "Render resolution, anti-aliasing and texture filtering.",
         [this](trg::Ui& ui) { PageGraphics(ui); }},
        {"Gameplay", "Frame rate, language and the frame counter.", [](trg::Ui& ui) { PageGameplay(ui); }},
        {"Controls", "Sticks, vibration, button remapping and keyboard play.",
         [this](trg::Ui& ui) { PageControls(ui); }},
        {"Cheats", "Infinite health, lives and ammo, and a few extras. All off by default.",
         [](trg::Ui& ui) { PageCheats(ui); }},
        {"Achievements", "Your progress on the game's achievements.", [this](trg::Ui& ui) { PageAchievements(ui); }},
        {"About", "About this port, and where your saves and settings live.", [this](trg::Ui& ui) { PageAbout(ui); }},
    };
    c.can_play = [this] {
      if (install_.running()) return trg::PlayCheck{false, "Wait for the install to finish.", 0};
      if (update_install_.running()) return trg::PlayCheck{false, "Wait for the update to finish.", 0};
      if (!files_ok_) return trg::PlayCheck{false, "Install the game first: select your Xbox Live Arcade package.", 0};
      if (!GameVersionMatches(paths_.game_dir))
        return trg::PlayCheck{false, "This is a different version of the game; the port needs version 1.0.0.11.", 0};
      return trg::PlayCheck{};
    };
    c.on_file_drop = [this](const std::string& path) {
      launcher_->GoToPage(0);
      StartInstall(path);
    };
    c.restart_keys = kRestartCvars;
    return c;
  }

  // ---------------------------------------------------------------- art ---
  ImTextureID MakeTexture(const art::Image& img) {
    if (!img || !immediate_) return {};
    textures_.push_back(immediate_->CreateTexture(uint32_t(img.width), uint32_t(img.height),
                                                  rex::ui::ImmediateTextureFilter::kLinear, false, img.rgba.data()));
    return Tex(textures_.back().get());
  }

  void LoadArt() {
    header_art_ = {};
    if (auto img = art::LoadImage(art::TitleCapturePath(paths_.user_dir))) {
      header_art_ = MakeTexture(img);
      header_aspect_ = float(img.width) / float(img.height);
      header_v_offset_ = 0.04f;
    } else if (auto banner = art::LoadImage(paths_.game_dir / "feathered_EWJ_banner.png")) {
      header_art_ = MakeTexture(banner);
      header_aspect_ = float(banner.width) / float(banner.height);
      header_v_offset_ = 0.0f;
    }
    title_icon_ = MakeTexture(art::LoadImage(art::TitleIconPath(paths_.game_dir)));
    if (launcher_) launcher_->config().branding.icon = title_icon_;
    LoadAchievements();
  }

  void LoadAchievements() {
    achievements_.clear();
    have_achievement_names_ = false;
    const auto icons = art::AchievementIcons(paths_.game_dir);
    std::map<uint32_t, Achievement> by_id;
    for (auto& [id, path] : icons) by_id[id].id = id;
    try {
      // Names: the list extracted from the game, else the one the game wrote on first play.
      std::error_code ec;
      auto source = art::AchievementDir(paths_.game_dir) / "achievements.toml";
      if (!std::filesystem::exists(source, ec)) source = art::AchievementCachePath(paths_.user_dir);
      auto table = toml::parse_file(source.string());
      if (auto* list = table["achievements"].as_array()) {
        for (auto& node : *list) {
          auto* e = node.as_table();
          if (!e) continue;
          const uint32_t id = uint32_t((*e)["id"].value_or<int64_t>(0));
          auto& a = by_id[id];
          a.id = id;
          a.label = (*e)["label"].value_or<std::string>("");
          a.description = (*e)["description"].value_or<std::string>("");
          a.unachieved = (*e)["unachieved_description"].value_or<std::string>("");
          a.gamerscore = uint32_t((*e)["gamerscore"].value_or<int64_t>(0));
        }
        have_achievement_names_ = !list->empty();
      }
    } catch (...) {
    }
    try {
      auto unlocks = toml::parse_file(art::AchievementUnlockPath(paths_.user_dir).string());
      if (auto* t = unlocks["unlocked"].as_table())
        for (auto& [key, value] : *t) by_id[uint32_t(std::stoul(std::string(key.str())))].unlocked = true;
    } catch (...) {
    }
    for (auto& [id, a] : by_id) {
      if (id == 0) continue;
      if (auto it = icons.find(id); it != icons.end()) a.icon = MakeTexture(art::LoadImage(it->second));
      achievements_.push_back(a);
    }
  }

  // --------------------------------------------------------------- Play ---
  void PagePlay(trg::Ui& ui) {
    std::string error;
    switch (install_.Finish(&error)) {
      case trg::Task::State::kDone:
        files_ok_ = GameFilesPresent(paths_.game_dir);
        ui.SetStatus(files_ok_ ? "Installed. Press Play to start the game."
                               : "The package was extracted, but default.xex is missing.",
                     8);
        if (files_ok_) LoadArt();
        break;
      case trg::Task::State::kFailed: ui.SetStatus("Install failed: " + error, 10); break;
      case trg::Task::State::kCancelled: ui.SetStatus("Install cancelled."); break;
      default: break;
    }
    if (update_install_.running())
      ui.StatusCard(trg::Status::kBusy, (update_install_.label() + "...").c_str(), nullptr,
                    std::max(0.0f, update_install_.fraction()));
    else if (install_.running())
      ui.StatusCard(trg::Status::kBusy, "Installing...", nullptr, std::max(0.0f, install_.fraction()));
    else if (files_ok_ && !GameVersionMatches(paths_.game_dir))
      ui.StatusCard(trg::Status::kAttention, "Different version of the game",
                    "This port needs the Xbox Live Arcade release, version 1.0.0.11 (default.xex CRC32 D6366187). "
                    "Install that package below.");
    else if (files_ok_)
      ui.StatusCard(trg::Status::kReady, "Ready to play", paths_.game_dir.string().c_str());
    else
      ui.StatusCard(trg::Status::kAttention, "Game files needed",
                    "Install from your Earthworm Jim HD Xbox Live Arcade package below.");

    ui.Row("Game package",
           "Your own Earthworm Jim HD package from an Xbox 360 (a file with a long hexadecimal name, found under "
           "Content\\...\\584109E2\\000D0000). Its files (about 430 MB) are copied next to the game. You can also "
           "drop the file onto this window.");
    if (!ui.TaskProgress(install_)) {
      const bool clicked = files_ok_ ? ImGui::Button("Reinstall from package...", ImVec2(-FLT_MIN, 0))
                                     : ui.AccentButton("Install from package...");
      if (clicked) {
        const std::string pkg = trg::BrowseForFile("Select your Earthworm Jim HD Xbox Live Arcade package",
                                                   {{"Xbox Live Arcade package", "*"}});
        if (!pkg.empty()) StartInstall(pkg);
      }
    }
    ui.Choice("Edition",
              "Xbox Live Arcade games shipped as trials that unlocked when bought, which is no longer possible. "
              "The full game is in the package; Trial plays it as the demo.",
              "license_mask", "1", {{"1", "Full game"}, {"0", "Trial"}});
    ui.LauncherVisibilityRow("ewj_launcher");
    ui.EndRows();
  }

  void StartInstall(const std::string& pkg) {
    if (install_.running()) return;
    const uint32_t title = stfs::ReadTitleId(std::filesystem::u8path(pkg));
    if (title == 0) {
      launcher_->SetStatus("That file is not an Xbox 360 content package (LIVE/PIRS/CON).", 8);
      return;
    }
    if (title != kTitleId) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "That package is title %08X, not Earthworm Jim HD (%08X).", title, kTitleId);
      launcher_->SetStatus(buf, 8);
      return;
    }
    const auto dest = paths_.game_dir;
    install_.Start("Installing", [pkg, dest](trg::Task& t) {
      // stfs::Extract reports through its own counters; mirror them (and
      // Cancel) into the Task until it returns.
      stfs::Progress progress;
      std::atomic<bool> finished{false};
      std::thread watcher([&] {
        while (!finished) {
          t.Progress(static_cast<long long>(progress.bytes_done.load()),
                     static_cast<long long>(progress.bytes_total.load()));
          if (t.cancelled()) progress.cancel = true;
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      });
      std::string result = stfs::Extract(std::filesystem::u8path(pkg), dest, &progress);
      finished = true;
      watcher.join();
      return result;
    });
  }

  // ------------------------------------------------------------ Display ---
  void PageDisplay(trg::Ui& ui) {
    ui.Toggle("Window mode", "Fullscreen uses a borderless window at your desktop resolution.", "fullscreen", false,
              "Windowed", "Fullscreen");
    ui.Row("Window size", "Size of the window in windowed mode. Applies when the game starts.");
    WindowSizeCombo();
    ui.Row("Monitor", "Which display the game opens on. Applies when the game starts.");
    MonitorCombo();
    // The runtime's flag is "allow tearing", which is VSync off.
    ui.Row("VSync",
           "On waits for the display for tear-free frames. Off has the lowest latency and lets G-Sync/FreeSync "
           "displays run freely.");
    {
      const bool tearing = settings_.GetBool("d3d12_allow_variable_refresh_rate_and_tearing", true);
      if (int i = ui.Segmented("vsync", {"Off", "On"}, tearing ? 0 : 1); i >= 0)
        settings_.SetBool("d3d12_allow_variable_refresh_rate_and_tearing", i == 0);
    }
    ui.Row("Aspect ratio", "The game is 16:9. Letterbox keeps its shape on other screens; stretch fills them.");
    {
      const bool letterbox = settings_.GetBool("present_letterbox", true);
      if (int i = ui.Segmented("aspect", {"Letterbox 16:9", "Stretch"}, letterbox ? 0 : 1); i >= 0)
        settings_.SetBool("present_letterbox", i == 0);
    }
  }

  void WindowSizeCombo() {
    // window_width/height are in logical pixels (96 DPI); offer real pixel sizes
    // that fit on the screen and convert with the window's DPI scale.
    const double scale = cb_.dpi_scale ? cb_.dpi_scale() : 1.0;
    auto to_physical = [&](int logical) { return int(logical * scale + 0.5); };
    const int w = to_physical(settings_.GetInt("window_width", 0)), h = to_physical(settings_.GetInt("window_height", 0));
    static const std::pair<int, int> kSizes[] = {{0, 0},       {1280, 720},  {1600, 900},
                                                 {1920, 1080}, {2560, 1440}, {3200, 1800}};
    auto label = [](int sw, int sh) {
      return sw == 0 ? std::string("Default") : std::to_string(sw) + " \xC3\x97 " + std::to_string(sh);
    };
    const auto [screen_w, screen_h] = cb_.screen_size ? cb_.screen_size() : std::pair<int, int>{1 << 16, 1 << 16};
    auto close_to = [](int a, int b) { return std::abs(a - b) <= 2; };
    ImGui::BeginDisabled(settings_.GetBool("fullscreen", false));
    if (ImGui::BeginCombo("##winsize", label(w, h).c_str())) {
      for (auto [sw, sh] : kSizes) {
        if (sw >= screen_w || sh >= screen_h) continue;  // must fit with its frame
        if (ImGui::Selectable(label(sw, sh).c_str(), close_to(sw, w) && close_to(sh, h))) {
          settings_.SetInt("window_width", int(sw / scale + 0.5));
          settings_.SetInt("window_height", int(sh / scale + 0.5));
        }
      }
      ImGui::EndCombo();
    }
    ImGui::EndDisabled();
  }

  void MonitorCombo() {
    if (monitors_.empty()) monitors_ = ListMonitors();
    const std::string cur = settings_.Get("monitor", "0");
    std::string preview = "Default";
    std::vector<std::pair<std::string, std::string>> opts = {{"0", "Default"}};
    for (size_t i = 0; i < monitors_.size(); ++i) {
      const auto& m = monitors_[i];
      opts.push_back({std::to_string(i + 1), "Display " + std::to_string(i + 1) + (m.primary ? " (primary)" : "") +
                                                 "   " + std::to_string(m.width) + " \xC3\x97 " +
                                                 std::to_string(m.height)});
    }
    for (auto& [v, l] : opts)
      if (v == cur) preview = l;
    if (ImGui::BeginCombo("##monitor", preview.c_str())) {
      for (auto& [v, l] : opts)
        if (ImGui::Selectable(l.c_str(), v == cur)) settings_.Set("monitor", v);
      ImGui::EndCombo();
    }
  }

  // ----------------------------------------------------------- Graphics ---
  void PageGraphics(trg::Ui& ui) {
    const auto [out_w, out_h] = cb_.output_size ? cb_.output_size() : std::pair<int, int>{1280, 720};
    ui.Row("Render quality",
           "How sharply the game is drawn compared with your screen. Native matches it; Quality, Balanced and the "
           "Performance modes draw fewer pixels and scale up; Supersample draws more for the cleanest edges. The "
           "game renders in steps of its original 720p.");
    {
      const std::string cur = settings_.Get("ewj_render_quality", "native");
      std::vector<std::string> labels, values;
      for (const auto& p : RenderPresets()) {
        const int scale = RenderScaleFor(p.id, out_h);
        labels.push_back(std::string(p.label) + "   " + std::to_string(1280 * scale) + " \xC3\x97 " +
                         std::to_string(720 * scale));
        values.push_back(p.id);
      }
      labels.push_back("Custom");
      values.push_back("custom");
      int sel = -1;
      for (size_t i = 0; i < values.size(); ++i)
        if (values[i] == cur) sel = int(i);
      if (ImGui::BeginCombo("##quality", sel >= 0 ? labels[size_t(sel)].c_str() : cur.c_str(),
                            ImGuiComboFlags_HeightLarge)) {
        for (size_t i = 0; i < labels.size(); ++i)
          if (ImGui::Selectable(labels[i].c_str(), int(i) == sel)) settings_.Set("ewj_render_quality", values[i]);
        ImGui::EndCombo();
      }
      char size[96];
      std::snprintf(size, sizeof(size), "%s: %d \xC3\x97 %d",
                    settings_.GetBool("fullscreen", false) ? "Your screen" : "Game window", out_w, out_h);
      ui.Help(size);
    }
    if (settings_.Get("ewj_render_quality") == "custom")
      ui.Combo("Internal resolution", "Draw the game at an exact multiple of its native 1280 \xC3\x97 720.",
               "resolution_scale", "1",
               {{"1", "1\xC3\x97   1280 \xC3\x97 720 (original)"},
                {"2", "2\xC3\x97   2560 \xC3\x97 1440"},
                {"3", "3\xC3\x97   3840 \xC3\x97 2160 (4K)"},
                {"4", "4\xC3\x97   5120 \xC3\x97 2880"},
                {"5", "5\xC3\x97   6400 \xC3\x97 3600"},
                {"6", "6\xC3\x97   7680 \xC3\x97 4320 (8K)"}});
    ui.Choice("Anti-aliasing", "Smooths jagged edges after the frame is drawn. Extreme is softer but cleaner.",
              "swap_post_effect", "none",
              Allowed("swap_post_effect", {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA Extreme"}}));
    ui.Toggle("Multisampling", "Real 2\xC3\x97 MSAA wherever the game asks the Xbox 360 GPU for it.", "native_2x_msaa",
              false, "Off", "2\xC3\x97 MSAA");
    ui.Choice("Texture filtering", "Keeps distant and angled textures sharp.", "anisotropic_override", "-1",
              {{"-1", "Game"}, {"0", "Off"}, {"2", "2\xC3\x97"}, {"3", "4\xC3\x97"}, {"4", "8\xC3\x97"}, {"5", "16\xC3\x97"}},
              6);
    ui.Toggle("Shader preparing",
              "Each new effect is prepared the first time it appears, then saved for next time. Wait draws it "
              "correctly with a short pause, the first time only. Background avoids the pause, but effects can "
              "briefly be missing.",
              "async_shader_compilation", false, "Wait", "Background");
  }

  // ----------------------------------------------------------- Gameplay ---
  static void PageGameplay(trg::Ui& ui) {
    std::vector<trg::Option> rates;
    for (int f : kFrameRateChoices) rates.push_back({std::to_string(f), f <= 0 ? "Unlimited" : std::to_string(f)});
    ui.Choice("Frame rate", "The frame-rate cap. Higher is smoother.", "ewj_frame_rate", "60", rates, 7);
    ui.Toggle("Frame counter", "Shows the game's frame rate in the corner. F2 toggles it while playing.",
              "ewj_show_fps", false, "Hidden", "Shown");
    // The package's languages, as the Xbox 360 system language that selects them.
    ui.Combo("Language", "The game's language.", "user_language", "1",
             {{"1", "English"},
              {"3", "Deutsch"},
              {"5", "Espa\xC3\xB1ol"},
              {"4", "Fran\xC3\xA7" "ais"},
              {"6", "Italiano"},
              {"2", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"}});
  }

  // ------------------------------------------------------------- Cheats ---
  static void PageCheats(trg::Ui& ui) {
    ui.Toggle("Infinite health", "Hits no longer drain Jim's health. Pits and other instant deaths still count.",
              "ewj_cheat_health", false);
    ui.Toggle("Infinite lives", "Jim keeps his lives when he dies, so it is never Game Over.", "ewj_cheat_lives",
              false);
    ui.Toggle("Infinite ammo", "The plasma gun never runs low.", "ewj_cheat_ammo", false);
    ui.Toggle("Rapid fire", "The plasma gun fires three times as fast.", "ewj_cheat_rapid_fire", false);
    ui.Toggle("High jump", "Jim jumps higher.", "ewj_cheat_high_jump", false);
    ui.Toggle("Fast run", "Jim runs half as fast again.", "ewj_cheat_fast_run", false);
    ui.Help("Cheats work in Jim's platform levels. Achievements can still be unlocked with cheats on.");
  }

  // ----------------------------------------------------------- Controls ---
  void PageControls(trg::Ui& ui) {
    ui.Toggle("Input", "Controllers work automatically. Keyboard emulates a controller; keys are below.", "mnk_mode",
              false, "Controller", "Keyboard");
    ui.SliderInt("Stick deadzone", "Ignores small stick movements. Raise it if Jim creeps on his own.", "ewj_deadzone",
                 0, 0, 40, "%d%%");
    ui.Toggle("Vibration", "Controller rumble.", "ewj_vibration", true);
    if (settings_.GetBool("ewj_vibration", true))
      ui.SliderInt("Vibration strength", nullptr, "ewj_vibration_strength", 100, 10, 100, "%d%%");
    {
      ui.Row("Left stick", "Invert the left stick's axes.");
      const bool x = settings_.GetBool("ewj_invert_ls_x", false), y = settings_.GetBool("ewj_invert_ls_y", false);
      const int sel = x && y ? 3 : x ? 1 : y ? 2 : 0;
      if (int i = ui.Segmented("ls", {"Normal", "Invert X", "Invert Y", "Both"}, sel); i >= 0) {
        settings_.SetBool("ewj_invert_ls_x", i == 1 || i == 3);
        settings_.SetBool("ewj_invert_ls_y", i == 2 || i == 3);
      }
    }
    if (ui.Section("Button remapping")) {
      if (ImGui::Button("Reset to defaults##remap"))
        for (size_t i = 0; i < kPadCount; ++i) SetMapping(static_cast<Pad>(i), static_cast<Pad>(i));
      for (size_t i = 0; i < kPadCount; ++i) {
        const auto physical = static_cast<Pad>(i);
        const Pad target = GetMapping(physical);
        ui.Row(GetPadInfo(physical).label);
        ImGui::PushID(int(i));
        const char* preview = target == Pad::kNone ? "(nothing)" : GetPadInfo(target).label;
        if (ImGui::BeginCombo("##t", preview, ImGuiComboFlags_HeightLarge)) {
          if (ImGui::Selectable("(nothing)", target == Pad::kNone)) SetMapping(physical, Pad::kNone);
          for (size_t j = 0; j < kPadCount; ++j)
            if (ImGui::Selectable(GetPadInfo(static_cast<Pad>(j)).label, target == static_cast<Pad>(j)))
              SetMapping(physical, static_cast<Pad>(j));
          ImGui::EndCombo();
        }
        ImGui::PopID();
      }
    }
    if (ui.Section("Keyboard bindings")) {
      std::vector<const rex::cvar::FlagEntry*> binds;
      for (auto& e : rex::cvar::GetRegistry())
        if (e.category == "Input/Keybinds/Controller") binds.push_back(&e);
      if (ImGui::Button("Reset to defaults##keys"))
        for (auto* e : binds) rex::cvar::ResetToDefault(e->name);
      ImGui::SameLine();
      ImGui::TextDisabled("Click a binding, then press a key (Esc cancels).");
      for (auto* e : binds) {
        if (auto key = ui.KeyBindRow(e->description.c_str(), nullptr, e->getter())) {
          const int vk = trg::ImGuiKeyToVirtualKey(*key);
          const std::string name = vk ? rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk)) : "";
          if (!name.empty()) rex::cvar::SetFlagByName(e->name, name);
        }
      }
    }
  }

  // ------------------------------------------------------- Achievements ---
  void PageAchievements(trg::Ui& ui) {
    ui.Toggle("Notifications", "An Xbox 360-style pop-up when you unlock an achievement in game.",
              "ewj_achievement_toasts", true);
    ui.Toggle("Sound", "The sound that plays with each pop-up.", "ewj_achievement_sound", true);
    if (settings_.GetBool("ewj_achievement_sound", true)) {
      ui.Row("Sound to play",
             "Pick the built-in chime or a sound you added. Put .wav files in the sounds folder to see them here.");
      SoundCombo();
      if (ImGui::Button("Open sounds folder", ImVec2(-FLT_MIN, 0))) {
        OpenInExplorer(SoundsDir(paths_.user_dir));
        sounds_.clear();  // rescan when the list is next drawn
      }
      ui.SliderInt("Volume", nullptr, "ewj_achievement_volume", 80, 0, 100, "%d%%");
      if (ImGui::IsItemDeactivatedAfterEdit()) PlayAchievementSound(paths_.user_dir);  // preview
    }
    if (ui.ButtonRow("Test", "Shows a sample notification right now.", "Test notification", true)) {
      if (!settings_.GetBool("ewj_achievement_toasts", true))
        ui.SetStatus("Notifications are off; switch them on to see the test.");
      const uint32_t icon = achievements_.empty() ? 0 : achievements_[test_index_++ % achievements_.size()].id;
      toast_->Show("Test achievement", 10, icon);
    }

    ui.Heading("PC port");
    std::vector<trg::AchievementCard> port;
    for (const auto& pa : PortAchievements())
      port.push_back({title_icon_, pa.title, pa.description, "", 0, IsPortAchievementUnlocked(paths_.user_dir, pa.id)});
    ui.AchievementGrid(port, 1);

    ui.Heading("Earthworm Jim HD");
    if (achievements_.empty()) {
      ui.Paragraph("Install the game to see its achievements.");
      return;
    }
    int unlocked = 0, score = 0, total_score = 0;
    std::vector<trg::AchievementCard> cards;
    for (auto& a : achievements_) {
      total_score += int(a.gamerscore);
      if (a.unlocked) ++unlocked, score += int(a.gamerscore);
      cards.push_back({a.icon, a.label.empty() ? "Achievement " + std::to_string(a.id) : a.label, a.description,
                       a.unachieved, int(a.gamerscore), a.unlocked});
    }
    ui.AchievementSummary(unlocked, int(cards.size()), have_achievement_names_ ? score : 0,
                          have_achievement_names_ ? total_score : 0);
    if (!have_achievement_names_) ui.Help("Names and descriptions appear after you have played the game once.");
    ui.AchievementGrid(cards);
  }

  void SoundCombo() {
    if (sounds_.empty() || ImGui::GetTime() - sounds_scanned_ > 3.0) {
      sounds_ = ListSounds(paths_.user_dir);
      sounds_.insert(sounds_.begin(), std::filesystem::path());  // built-in chime
      sounds_scanned_ = ImGui::GetTime();
    }
    const std::string cur = settings_.Get("ewj_achievement_sound_file");
    auto label = [](const std::filesystem::path& p) {
      return p.empty() ? std::string("Original chime (built in)") : SoundLabel(p);
    };
    std::string preview = "Original chime (built in)";
    for (auto& p : sounds_)
      if (p.string() == cur) preview = label(p);
    if (ImGui::BeginCombo("##sound", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
      for (auto& p : sounds_) {
        if (ImGui::Selectable(label(p).c_str(), p.string() == cur)) {
          settings_.Set("ewj_achievement_sound_file", p.string());
          PlayAchievementSound(paths_.user_dir);  // preview
        }
      }
      ImGui::EndCombo();
    }
  }

  // -------------------------------------------------------------- About ---
  void PageAbout(trg::Ui& ui) {
    ui.Paragraph(
        "Earthworm Jim HD (Gameloft, 2010) running natively on PC: the original Xbox 360 game code statically "
        "recompiled to C++ with the ReXGlue SDK. No game code or assets are included; the game runs from your own "
        "Xbox Live Arcade package.");
    ui.Spacer(4);
    ui.FolderRow("Save data", "Your saves, achievements and caches.", "Open save folder", paths_.user_dir.string());
    ui.FolderRow("Game files", "Where the game is installed.", "Open game folder", paths_.game_dir.string());
    if (ui.ButtonRow("Settings file", "Every launcher setting, as plain text.", "Open settings file")) {
      SaveSettings(paths_.config_path);
      OpenInExplorer(paths_.config_path);
    }
    if (ui.ButtonRow("Launcher art",
                     "The header shows the game's own title screen, captured the first time you reach it.",
                     "Capture again next time I play")) {
      std::error_code ec;
      std::filesystem::remove(art::TitleCapturePath(paths_.user_dir), ec);
      ui.SetStatus("The title screen will be captured again next time you play.");
    }
    ui.Choice("Updates",
              "When the launcher opens, look for a newer version of this port on GitHub. Ask shows a pop-up first; "
              "Automatic installs it straight away. Only GitHub is contacted, and only when the launcher opens.",
              "ewj_updates", "ask", {{"ask", "Ask"}, {"auto", "Automatic"}, {"off", "Off"}});
    if (!ui.TaskProgress(update_install_)) {
      if (ui.ButtonRow("Check for updates", update_status_.empty() ? nullptr : update_status_.c_str(),
                       update_check_.running() ? "Checking..." : "Check now") &&
          !update_check_.running()) {
        manual_check_ = true;
        StartUpdateCheck();
      }
    }
    ui.ResetAllRow();
    ui.Info("Port version", update::CurrentVersion());
    ui.Info("Version", "ReXGlue SDK 0.10.0   \xC2\xB7   TRG Launcher 1.1.0   \xC2\xB7   title 584109E2, v1.0.0.11");
    ui.Help("github.com/TekRantGaming/earthworm-jim-hd-recompiled");
  }

  // ------------------------------------------------------------ updates ---
  void StartUpdateCheck() {
    update_check_.Start("Checking for updates", [this](trg::Task&) {
      std::string error;
      update_found_ = update::CheckLatest(&error);
      update_error_ = error;
      return std::string();
    });
  }

  void StartUpdateInstall() {
    if (!update_found_ || update_install_.running()) return;
    launcher_->GoToPage(0);
    const update::Release release = *update_found_;
    update_install_.Start("Updating", [release](trg::Task& t) { return update::Install(t, release); });
  }

  void PollUpdates() {
    if (update_check_.Finish() != trg::Task::State::kIdle) {
      if (update_found_) {
        update_status_ = "Version " + update_found_->tag + " is available.";
        if (settings_.Get("ewj_updates", "ask") == "auto") {
          StartUpdateInstall();
        } else {
          trg::PlayPrompt p;
          p.title = "Update available: " + update_found_->tag;
          p.paragraphs = {"A newer version of the Earthworm Jim HD PC port is available (you have " +
                              std::string(update::CurrentVersion()) + ").",
                          "Update now downloads it from GitHub, installs it and restarts the launcher. Your game "
                          "files, saves and settings are kept."};
          p.footnote = update_found_->page;
          p.buttons = {{"Update now", true, false, [this] { StartUpdateInstall(); }}, {"Later", false, false, {}}};
          launcher_->ShowPrompt(std::move(p));
        }
      } else {
        update_status_ = update_error_.empty() ? "You have the latest version." : "Could not check: " + update_error_;
        if (manual_check_) launcher_->SetStatus(update_status_);
      }
      manual_check_ = false;
    }
    std::string error;
    switch (update_install_.Finish(&error)) {
      case trg::Task::State::kDone:
        // New files are in place: start them and close this instance.
        RelaunchSelf(L"");
        done_ = true;
        if (cb_.quit) cb_.quit();
        break;
      case trg::Task::State::kFailed: launcher_->SetStatus("Update failed: " + error, 10); break;
      case trg::Task::State::kCancelled: launcher_->SetStatus("Update cancelled."); break;
      default: break;
    }
  }

  // --------------------------------------------------------------- play ---
  void StartGame() {
    if (done_) return;
    done_ = true;
    auto action = launcher_->restart_needed() ? cb_.restart_and_play : cb_.play;
    Close();  // deletes this dialog after the current draw
    if (action) action();
  }

  rex::ui::ImmediateDrawer* immediate_;
  LauncherPaths paths_;
  LauncherCallbacks cb_;
  trg::CallbackSettings settings_;
  std::unique_ptr<trg::Launcher> launcher_;
  bool files_ok_ = false;
  bool done_ = false;
  trg::Task install_;
  trg::Task update_check_, update_install_;
  std::optional<update::Release> update_found_;  // written by the check Task, read after Finish()
  std::string update_error_, update_status_;
  bool manual_check_ = false;
  std::vector<MonitorInfo> monitors_;

  std::vector<std::unique_ptr<rex::ui::ImmediateTexture>> textures_;
  ImTextureID header_art_{};
  float header_aspect_ = 16.0f / 9.0f;
  float header_v_offset_ = 0.04f;
  ImTextureID title_icon_{};
  std::vector<Achievement> achievements_;
  bool have_achievement_names_ = false;
  std::unique_ptr<AchievementToast> toast_;
  size_t test_index_ = 0;
  std::vector<std::filesystem::path> sounds_;
  double sounds_scanned_ = -10.0;
};

}  // namespace

bool GameVersionMatches(const std::filesystem::path& game_dir) {
  // The port is built for one exact default.xex (addresses, jump tables and
  // hooks all depend on it): the XBLA release, version 1.0.0.11. A title
  // update or another revision would crash in confusing ways, so check it.
  static std::filesystem::path checked;
  static std::filesystem::file_time_type checked_time{};
  static bool result = false;
  const auto xex = game_dir / "default.xex";
  std::error_code ec;
  const auto time = std::filesystem::last_write_time(xex, ec);
  if (checked == game_dir && checked_time == time) return result;  // same file as last time
  checked = game_dir;
  checked_time = time;
  result = false;
  if (ec || std::filesystem::file_size(xex, ec) != kXexSize || ec) return result;
  std::ifstream in(xex, std::ios::binary);
  uint32_t table[256];
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    table[i] = c;
  }
  uint32_t crc = 0xFFFFFFFFu;
  std::vector<char> chunk(1 << 20);
  while (in) {
    in.read(chunk.data(), std::streamsize(chunk.size()));
    for (std::streamsize i = 0; i < in.gcount(); ++i) crc = table[(crc ^ uint8_t(chunk[size_t(i)])) & 0xFF] ^ (crc >> 8);
  }
  result = (crc ^ 0xFFFFFFFFu) == kXexCrc32;
  if (!result) REXLOG_WARN("EWJ: default.xex is not the supported version (CRC32 {:08X})", crc ^ 0xFFFFFFFFu);
  return result;
}

bool GameFilesPresent(const std::filesystem::path& game_dir) {
  std::error_code ec;
  return !game_dir.empty() && std::filesystem::exists(game_dir / "default.xex", ec) &&
         std::filesystem::exists(game_dir / "data.dat", ec) &&
         std::filesystem::exists(game_dir / "data" / "sounds" / "xbox" / "ALL_LEVEL.xwb", ec);
}

void PreloadGpuPlugin() {
#if defined(_WIN32)
  const auto dir = rex::filesystem::GetExecutableFolder();
  for (const char* name : {"rexgpu-xenosrd.dll", "rexgpu-xenos.dll", "rexgpu-xenosd.dll"}) {
    if (std::filesystem::exists(dir / name) && LoadLibraryW((dir / name).c_str())) return;
  }
  REXLOG_WARN("EWJ: GPU plugin not found for preload; graphics settings unavailable in launcher");
#else
  const auto dir = rex::filesystem::GetExecutableFolder();
  for (const char* name : {"librexgpu-xenosrd.so", "librexgpu-xenos.so", "librexgpu-xenosd.so"}) {
    for (const auto& path : {dir / name, dir / ".." / "lib" / name})
      if (std::filesystem::exists(path) && dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL)) return;
  }
  REXLOG_WARN("EWJ: GPU plugin not found for preload; graphics settings unavailable in launcher");
#endif
}

void ShowLauncher(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, LauncherPaths paths,
                  LauncherCallbacks callbacks) {
  new LauncherDialog(drawer, immediate, std::move(paths), std::move(callbacks));
}

}  // namespace ewj
