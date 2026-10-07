#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>

#include <rex/logging.h>
#include <rex/string.h>

REXCVAR_DEFINE_BOOL(ewj_launcher, true, "EWJ",
                    "Show the launcher before starting the game (hold Shift at start to force it)");
REXCVAR_DEFINE_STRING(ewj_updates, "ask", "EWJ",
                      "Updates from GitHub when the launcher opens: ask, auto or off")
    .allowed({"ask", "auto", "off"});
REXCVAR_DEFINE_BOOL(ewj_skip_launcher, false, "EWJ",
                    "Internal: skip the launcher once (used when it relaunches the game)");
REXCVAR_DEFINE_INT32(ewj_frame_rate, 60, "EWJ/Video",
                     "Frame-rate cap: 30, 60, 120, 144, 165, 240, or 0 for unlimited");
REXCVAR_DEFINE_STRING(ewj_render_quality, "native", "EWJ/Video",
                      "Render resolution relative to the output: native, quality, balanced, performance, "
                      "ultra_performance, supersample, or custom (use resolution_scale)");
REXCVAR_DEFINE_BOOL(ewj_show_fps, false, "EWJ/Video", "Show a frame-rate counter (toggle in game with F2)");
REXCVAR_DEFINE_INT32(ewj_deadzone, 0, "EWJ/Controls", "Extra stick deadzone in percent (0-50)");
REXCVAR_DEFINE_INT32(ewj_camera_sensitivity, 100, "EWJ/Controls", "Camera (right stick) sensitivity in percent");
REXCVAR_DEFINE_BOOL(ewj_achievement_toasts, true, "EWJ/Achievements", "Show achievement notifications");
REXCVAR_DEFINE_BOOL(ewj_achievement_sound, true, "EWJ/Achievements", "Play the achievement sound");
REXCVAR_DEFINE_STRING(ewj_achievement_sound_file, "", "EWJ/Achievements",
                      "Achievement sound from the sounds folder (empty = built-in chime)");
REXCVAR_DEFINE_INT32(ewj_achievement_volume, 80, "EWJ/Achievements", "Achievement sound volume in percent");
REXCVAR_DEFINE_BOOL(ewj_vibration, true, "EWJ/Controls", "Controller vibration");
REXCVAR_DEFINE_INT32(ewj_vibration_strength, 100, "EWJ/Controls", "Vibration strength in percent");
REXCVAR_DEFINE_BOOL(ewj_invert_rs_x, false, "EWJ/Controls", "Invert right stick horizontal (camera)");
REXCVAR_DEFINE_BOOL(ewj_invert_rs_y, false, "EWJ/Controls", "Invert right stick vertical");
REXCVAR_DEFINE_BOOL(ewj_invert_ls_x, false, "EWJ/Controls", "Invert left stick horizontal");
REXCVAR_DEFINE_BOOL(ewj_invert_ls_y, false, "EWJ/Controls", "Invert left stick vertical");

// Button remapping: ewj_map_<physical> = <game button> (or "none").
#define EWJ_MAP_CVAR(id, def, label) \
  REXCVAR_DEFINE_STRING(ewj_map_##id, def, "EWJ/Controls/Remap", label " sends")
EWJ_MAP_CVAR(dpad_up, "dpad_up", "D-pad up");
EWJ_MAP_CVAR(dpad_down, "dpad_down", "D-pad down");
EWJ_MAP_CVAR(dpad_left, "dpad_left", "D-pad left");
EWJ_MAP_CVAR(dpad_right, "dpad_right", "D-pad right");
EWJ_MAP_CVAR(start, "start", "Start");
EWJ_MAP_CVAR(back, "back", "Back");
EWJ_MAP_CVAR(ls, "ls", "Left stick click");
EWJ_MAP_CVAR(rs, "rs", "Right stick click");
EWJ_MAP_CVAR(lb, "lb", "Left bumper");
EWJ_MAP_CVAR(rb, "rb", "Right bumper");
EWJ_MAP_CVAR(a, "a", "A");
EWJ_MAP_CVAR(b, "b", "B");
EWJ_MAP_CVAR(x, "x", "X");
EWJ_MAP_CVAR(y, "y", "Y");
EWJ_MAP_CVAR(lt, "lt", "Left trigger");
EWJ_MAP_CVAR(rt, "rt", "Right trigger");
#undef EWJ_MAP_CVAR

namespace ewj {
namespace {

constexpr std::array<PadInfo, kPadCount> kPads = {{
    {"dpad_up", "D-pad Up", 0x0001},
    {"dpad_down", "D-pad Down", 0x0002},
    {"dpad_left", "D-pad Left", 0x0004},
    {"dpad_right", "D-pad Right", 0x0008},
    {"start", "Start", 0x0010},
    {"back", "Back", 0x0020},
    {"ls", "Left Stick Click", 0x0040},
    {"rs", "Right Stick Click", 0x0080},
    {"lb", "Left Bumper", 0x0100},
    {"rb", "Right Bumper", 0x0200},
    {"a", "A", 0x1000},
    {"b", "B", 0x2000},
    {"x", "X", 0x4000},
    {"y", "Y", 0x8000},
    {"lt", "Left Trigger", 0},
    {"rt", "Right Trigger", 0},
}};

std::string& MapStorage(Pad p) {
  switch (p) {
    case Pad::kDpadUp: return REXCVAR_GET(ewj_map_dpad_up);
    case Pad::kDpadDown: return REXCVAR_GET(ewj_map_dpad_down);
    case Pad::kDpadLeft: return REXCVAR_GET(ewj_map_dpad_left);
    case Pad::kDpadRight: return REXCVAR_GET(ewj_map_dpad_right);
    case Pad::kStart: return REXCVAR_GET(ewj_map_start);
    case Pad::kBack: return REXCVAR_GET(ewj_map_back);
    case Pad::kLeftThumb: return REXCVAR_GET(ewj_map_ls);
    case Pad::kRightThumb: return REXCVAR_GET(ewj_map_rs);
    case Pad::kLeftShoulder: return REXCVAR_GET(ewj_map_lb);
    case Pad::kRightShoulder: return REXCVAR_GET(ewj_map_rb);
    case Pad::kA: return REXCVAR_GET(ewj_map_a);
    case Pad::kB: return REXCVAR_GET(ewj_map_b);
    case Pad::kX: return REXCVAR_GET(ewj_map_x);
    case Pad::kY: return REXCVAR_GET(ewj_map_y);
    case Pad::kLeftTrigger: return REXCVAR_GET(ewj_map_lt);
    default: return REXCVAR_GET(ewj_map_rt);
  }
}

}  // namespace

const PadInfo& GetPadInfo(Pad pad) { return kPads[static_cast<size_t>(pad)]; }

Pad ParsePad(std::string_view id) {
  std::string lower(id);
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (size_t i = 0; i < kPadCount; ++i)
    if (lower == kPads[i].id) return static_cast<Pad>(i);
  return Pad::kNone;
}

Pad GetMapping(Pad physical) { return ParsePad(MapStorage(physical)); }

void SetMapping(Pad physical, Pad target) {
  rex::cvar::SetFlagByName(std::string("ewj_map_") + GetPadInfo(physical).id,
                           target == Pad::kNone ? "none" : GetPadInfo(target).id);
}

const std::array<RenderPreset, 6>& RenderPresets() {
  // Ratios follow the usual upscaler naming (output / render); the game renders
  // at integer multiples of its native 720p, so the nearest multiple is used.
  static const std::array<RenderPreset, 6> kPresets = {{
      {"supersample", "Supersample", 0.5},
      {"native", "Native", 1.0},
      {"quality", "Quality", 1.5},
      {"balanced", "Balanced", 1.7},
      {"performance", "Performance", 2.0},
      {"ultra_performance", "Ultra Performance", 3.0},
  }};
  return kPresets;
}

int RenderScaleFor(std::string_view preset, int output_height) {
  for (const auto& p : RenderPresets()) {
    if (preset != p.id) continue;
    const double target = std::max(1, output_height) / p.ratio;
    return std::clamp(static_cast<int>(std::lround(target / 720.0)), 1, 8);
  }
  return 0;  // custom
}

void ApplyRenderPreset(int output_height) {
  const int scale = RenderScaleFor(REXCVAR_GET(ewj_render_quality), output_height);
  if (scale <= 0) return;  // custom: the player's resolution_scale is used as is
  // The runtime only honours resolution_scale when it differs from its default,
  // so a preset drives the per-axis scales instead (read directly). They are set
  // as defaults so the derived value is not written to the config, and any
  // resolution_scale left over from an earlier custom choice is cleared.
  rex::cvar::ResetToDefault("resolution_scale");
  for (const char* axis : {"draw_resolution_scale_x", "draw_resolution_scale_y"}) {
    SetCvarDefault(axis, std::to_string(scale));
    rex::cvar::ResetToDefault(axis);
  }
  REXLOG_INFO("EWJ: render preset {} at {}p output -> {}x ({}p)", REXCVAR_GET(ewj_render_quality),
              output_height, scale, scale * 720);
}

bool SaveSettings(const std::filesystem::path& path) {
  // Like rex::cvar::SaveConfig, but values that came from the command line or
  // environment (e.g. --game_data_root, --log_file) are one-off overrides and
  // are not written back. The file's own line for such a setting is kept, so a
  // one-off --ewj_frame_rate=30 does not erase the player's saved choice.
  std::map<std::string, std::string> previous;  // name -> the file's line
  {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
      const auto eq = line.find('=');
      if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
      std::string name = line.substr(0, eq);
      while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
      previous[name] = line;
    }
  }
  std::string out = "# Earthworm Jim HD settings (edited by the launcher)\n";
  for (const auto& e : rex::cvar::GetRegistry()) {
    if (e.type == rex::cvar::FlagType::Command || e.is_debug_only) continue;
    if (IsPinnedCvar(e.name)) continue;  // set by the port at every start
    if (e.source == rex::cvar::Source::kCommandLine || e.source == rex::cvar::Source::kEnvironment) {
      if (auto it = previous.find(e.name); it != previous.end()) out += it->second + '\n';
      continue;
    }
    const std::string value = e.getter();
    if (value == e.default_value) continue;
    out += e.name + " = ";
    if (e.type == rex::cvar::FlagType::String) {
      out += '"';
      for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
      }
      out += '"';
    } else {
      out += value;
    }
    out += '\n';
  }
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    REXLOG_ERROR("EWJ: cannot write settings to {}", path.string());
    return false;
  }
  file << out;
  return static_cast<bool>(file);
}

void SetCvarDefault(std::string_view name, std::string_view value) {
  for (auto& e : rex::cvar::GetRegistry()) {
    if (e.name != name) continue;
    e.default_value = value;
    if (e.source == rex::cvar::Source::kDefault) e.setter(value);  // config/CLI still win
    return;
  }
}

bool IsPinnedCvar(std::string_view name) {
  // The guest video mode (PinGuestVideoMode), and what ApplyRuntimeOverrides
  // forces, which a save made while playing (F11) would otherwise write out.
  return name == "video_mode_width" || name == "video_mode_height" || name == "vsync" ||
         name == "d3d12_submit_on_primary_buffer_end";
}

void PinGuestVideoMode() {
  // ReXGlue derives the console's video mode from window_width/height whenever
  // those are set and video_mode_width/height are not (VdQueryVideoMode). A
  // window size from the launcher (say 800x600) then tells the game the TV is
  // 4:3: the game still draws 16:9, but the presenter treats the picture as 4:3
  // and stretches it, so "Letterbox 16:9" did nothing. Keep the console on
  // 1280x720. The check is "value != registered default", so the registered
  // default is cleared rather than the value changed.
  for (auto& e : rex::cvar::GetRegistry()) {
    if (!IsPinnedCvar(e.name)) continue;
    if (e.source == rex::cvar::Source::kDefault) e.setter(e.name == "video_mode_width" ? "1280" : "720");
    e.default_value.clear();
  }
}

void ApplyPortDefaults() {
  PinGuestVideoMode();
  // XBLA titles ship as trials that unlock via XamContentGetLicenseMask; the
  // port defaults to the full (purchased) license, like Xenia's license_mask = 1.
  // ReXGlue's default is 0, the trial. The launcher's Play page can switch it.
  SetCvarDefault("license_mask", "1");
  // Windowed by default so the launcher isn't a giant fullscreen dialog.
  SetCvarDefault("fullscreen", "false");
  // While a shader compiles in the background, the D3D12 backend skips every
  // draw that needs it, so objects vanish, turn into silhouettes or the frame
  // flashes bright on first sight. Waiting costs a short pause the first time
  // only, since shaders are saved for later runs.
  SetCvarDefault("async_shader_compilation", "false");
  // The game binds real textures (valid address, format and size) whose fetch
  // constant type field is 0, "invalid". The console samples them anyway; the
  // runtime otherwise unbinds them (11,540 warnings in a 9-minute playtest).
  // A GPU-plugin cvar: PreloadGpuPlugin registers it before this runs.
  SetCvarDefault("gpu_allow_invalid_fetch_constants", "true");
}

void ApplyRuntimeOverrides() {
  // ReXGlue submits GPU work at every primary ring-buffer end by default, which
  // stalls this title to ~27 ms per frame (the 30 FPS "lock"). Batching lets
  // it run at any rate; the title's delta-time keeps game speed correct.
  // Guest vsync only paces the emulated console (60 Hz vblank + coarse sleeps
  // in GPU waits). Frame pacing is done by ewj_frame_rate instead.
  for (const char* name : {"d3d12_submit_on_primary_buffer_end", "vsync"}) {
    if (!rex::cvar::SetFlagByName(name, "false"))
      REXLOG_WARN("EWJ: could not set {} (cvar not registered)", name);
  }
  REXLOG_INFO("EWJ: frame-rate cap {}", REXCVAR_GET(ewj_frame_rate));

  // The draw resolution scale the GPU uses (same rule as the runtime's
  // TextureCache::GetConfigDrawResolutionScale), for bug reports.
  auto axis = [](const char* name) {
    if (rex::cvar::HasNonDefaultValue("resolution_scale") && !rex::cvar::HasNonDefaultValue(name))
      return rex::cvar::GetFlagByName("resolution_scale");
    return rex::cvar::GetFlagByName(name);
  };
  REXLOG_INFO("EWJ: draw resolution scale {}x{}", axis("draw_resolution_scale_x"), axis("draw_resolution_scale_y"));

  // The GPU logs every occlusion ("viz") query at info level: thousands of
  // lines a second for this title, written from the GPU thread, plus a log
  // rotation every few seconds. Keep its warnings, drop the chatter, unless
  // the player asked for more detailed logs.
  if (REXCVAR_GET(log_level) == "info") {
    if (auto gpu = rex::FindCategory("gpu")) rex::SetCategoryLevel(*gpu, spdlog::level::warn);
  }
}

}  // namespace ewj
