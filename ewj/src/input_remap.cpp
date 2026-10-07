// Controller remapping and stick inversion.
//
// sub_82BEFD18 is the XDK's XInputGetState(user, state), linked into the title
// (it tail-calls XamInputGetState(user, 1, state)); the game reads the pad only
// through it. After the real call
// we rewrite the guest XINPUT_STATE (big-endian):
//   +0 dwPacketNumber, +4 wButtons, +6 bLeftTrigger, +7 bRightTrigger,
//   +8 sThumbLX, +10 sThumbLY, +12 sThumbRX, +14 sThumbRY.
// Keyboard input arrives through the same path when ReXGlue's mnk_mode is on,
// so remaps and inversion apply to it too.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>

#include "settings.h"

namespace {

constexpr uint8_t kTriggerPressThreshold = 30;  // XINPUT_GAMEPAD_TRIGGER_THRESHOLD

template <typename T>
T LoadBE(const uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof(v));
  return std::byteswap(v);
}

template <typename T>
void StoreBE(uint8_t* p, T v) {
  v = std::byteswap(v);
  std::memcpy(p, &v, sizeof(v));
}

void Invert(uint8_t* p) {
  const int16_t v = LoadBE<int16_t>(p);
  StoreBE<int16_t>(p, v == INT16_MIN ? INT16_MAX : static_cast<int16_t>(-v));
}

// Radial deadzone (rescaled so output still starts at 0) and a gain, applied to
// one stick's X/Y pair.
void ShapeStick(uint8_t* xy, float deadzone, float gain) {
  if (deadzone <= 0.0f && gain == 1.0f) return;
  float x = LoadBE<int16_t>(xy) / 32767.0f, y = LoadBE<int16_t>(xy + 2) / 32767.0f;
  const float mag = std::sqrt(x * x + y * y);
  if (mag <= deadzone || mag == 0.0f) {
    x = y = 0.0f;
  } else {
    const float scaled = std::min(1.0f, (mag - deadzone) / (1.0f - deadzone) * gain);
    x = x / mag * scaled;
    y = y / mag * scaled;
  }
  StoreBE<int16_t>(xy, static_cast<int16_t>(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
  StoreBE<int16_t>(xy + 2, static_cast<int16_t>(std::clamp(y, -1.0f, 1.0f) * 32767.0f));
}

void Remap(uint8_t* state) {
  using ewj::Pad;
  const uint16_t in_buttons = LoadBE<uint16_t>(state + 4);
  const uint8_t in_lt = state[6], in_rt = state[7];

  uint16_t out_buttons = 0;
  uint8_t out_lt = 0, out_rt = 0;
  for (size_t i = 0; i < ewj::kPadCount; ++i) {
    const auto physical = static_cast<Pad>(i);
    uint8_t analog;  // 0..255 strength of the physical control
    if (physical == Pad::kLeftTrigger) {
      analog = in_lt;
    } else if (physical == Pad::kRightTrigger) {
      analog = in_rt;
    } else {
      analog = (in_buttons & ewj::GetPadInfo(physical).mask) ? 255 : 0;
    }
    if (analog == 0) continue;

    const Pad target = ewj::GetMapping(physical);
    if (target == Pad::kNone) continue;
    if (target == Pad::kLeftTrigger) {
      out_lt = std::max(out_lt, analog);
    } else if (target == Pad::kRightTrigger) {
      out_rt = std::max(out_rt, analog);
    } else if (analog > kTriggerPressThreshold) {
      out_buttons |= ewj::GetPadInfo(target).mask;
    }
  }
  StoreBE<uint16_t>(state + 4, out_buttons);
  state[6] = out_lt;
  state[7] = out_rt;

  if (REXCVAR_GET(ewj_invert_ls_x)) Invert(state + 8);
  if (REXCVAR_GET(ewj_invert_ls_y)) Invert(state + 10);
  if (REXCVAR_GET(ewj_invert_rs_x)) Invert(state + 12);
  if (REXCVAR_GET(ewj_invert_rs_y)) Invert(state + 14);

  const float deadzone = std::clamp(REXCVAR_GET(ewj_deadzone), 0, 50) / 100.0f;
  const float camera = std::clamp(REXCVAR_GET(ewj_camera_sensitivity), 10, 400) / 100.0f;
  ShapeStick(state + 8, deadzone, 1.0f);
  ShapeStick(state + 12, deadzone, camera);
}

}  // namespace

namespace ewj {
// Set by developer builds (dev_tools.cpp) to feed in pad input; returns true
// when it filled in the state.
bool (*g_dev_pad_input)(uint32_t user, uint8_t* state) = nullptr;
}  // namespace ewj

REX_EXTERN(__imp__sub_82BEFD18);
REX_HOOK_RAW(sub_82BEFD18) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t state_ptr = ctx.r4.u32;
  __imp__sub_82BEFD18(ctx, base);
  if (ewj::g_dev_pad_input && state_ptr && ewj::g_dev_pad_input(user, base + state_ptr)) {
    ctx.r3.u64 = 0;  // ERROR_SUCCESS: a pad is connected
    return;
  }
  if (ctx.r3.u32 == 0 && state_ptr) Remap(base + state_ptr);  // ERROR_SUCCESS
}

// sub_82BEFD28 is the XDK's XInputSetState(user, vibration); scale or
// drop the guest XINPUT_VIBRATION {u16 left, u16 right} before it is applied.
REX_EXTERN(__imp__sub_82BEFD28);
REX_HOOK_RAW(sub_82BEFD28) {
  if (const uint32_t vib = ctx.r4.u32) {
    const float strength =
        REXCVAR_GET(ewj_vibration) ? std::clamp(REXCVAR_GET(ewj_vibration_strength), 0, 100) / 100.0f : 0.0f;
    for (uint32_t offset : {0u, 2u}) {
      uint8_t* motor = base + vib + offset;
      StoreBE<uint16_t>(motor, static_cast<uint16_t>(LoadBE<uint16_t>(motor) * strength));
    }
  }
  __imp__sub_82BEFD28(ctx, base);
}
