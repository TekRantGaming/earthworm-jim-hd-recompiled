// "Can't Read Gamer Profile" fix (part 2 of 2; part 1 is
// SeedTitleSpecificProfileSettings in earthworm_jim_hd_app.h).
//
// The game keeps its progress and options in the profile's three
// title-specific settings. sub_82F34B50 is its XamUserReadProfileSettings
// wrapper (title, user index, count, setting ids, buffer size ptr, buffer,
// overlapped); the result parser sub_82B54688 then rejects the read unless
// each returned X_USER_PROFILE_SETTING (0x28 bytes) has from = 1 or 2 and a
// non-zero 8-byte user field at +8 (sub_82B54AD0 compares it with 0). A real
// console fills that field with the player's XUID; ReXGlue writes the user
// index (0) when the title reads by index, so the game always failed.
//
// ReXGlue completes the read before returning (CompleteOverlappedImmediate),
// so the buffer is final when the call returns: fill in the XUID there.

#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>

namespace {

template <typename T>
T LoadBE(const uint8_t* base, uint32_t addr) {
  T v;
  std::memcpy(&v, base + addr, sizeof(v));
  return std::byteswap(v);
}

template <typename T>
void StoreBE(uint8_t* base, uint32_t addr, T v) {
  v = std::byteswap(v);
  std::memcpy(base + addr, &v, sizeof(v));
}

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorIoPending = 0x3E5;
constexpr uint32_t kSettingSize = 0x28;  // X_USER_PROFILE_SETTING

}  // namespace

REX_EXTERN(__imp__sub_82F34B50);
REX_HOOK_RAW(sub_82F34B50) {
  const uint32_t buffer = ctx.r8.u32;
  __imp__sub_82F34B50(ctx, base);
  const uint32_t result = ctx.r3.u32;
  if (!buffer || (result != kErrorSuccess && result != kErrorIoPending)) return;

  auto* ks = REX_KERNEL_STATE();
  auto* profile = ks ? ks->user_profile() : nullptr;
  if (!profile) return;
  const uint64_t xuid = profile->xuid();

  // X_USER_READ_PROFILE_SETTINGS: setting_count, settings_ptr.
  const uint32_t count = LoadBE<uint32_t>(base, buffer);
  const uint32_t settings = LoadBE<uint32_t>(base, buffer + 4);
  if (!settings || count > 32) return;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t entry = settings + i * kSettingSize;
    if (LoadBE<uint64_t>(base, entry + 8) == 0) StoreBE<uint64_t>(base, entry + 8, xuid);
  }
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("EWJ: profile read of {} settings, XUID {:016X} filled in", count, xuid);
  }
}
