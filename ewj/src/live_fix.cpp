// Leaderboard stats without Xbox Live (issue #2: crash on "Continue").
//
// sub_82BF0D80 / sub_82BF0DF8 / sub_82BF0E70 are the XDK's
// XUserCreateStatsEnumeratorByRank / ByXuid / ByRating wrappers
// (title, pivot, rows, spec count, specs, DWORD* buffer size, HANDLE* enum).
// They end in XamUserCreateStatsEnumerator, which ReXGlue only stubs: it sets
// neither the return value nor *buffer_size, so the game allocated whatever
// size was left in memory ("BaseHeap::Alloc page count too big"), got null
// and wrote through it. The game reads its leaderboard stats when you
// Continue, so it crashed there.
//
// Answer the way a console that is not signed in to Xbox Live does: an error,
// no buffer and no handle. The game then shows no leaderboard data.

#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>

namespace {

constexpr uint32_t kErrorNotLoggedOn = 0x4DD;      // ERROR_NOT_LOGGED_ON
constexpr uint32_t kInvalidHandle = 0xFFFFFFFFu;  // INVALID_HANDLE_VALUE

void StoreBE32(uint8_t* base, uint32_t addr, uint32_t v) {
  v = std::byteswap(v);
  std::memcpy(base + addr, &v, sizeof(v));
}

void NoLiveStats(PPCContext& ctx, uint8_t* base, const char* which) {
  if (const uint32_t size_ptr = ctx.r8.u32) StoreBE32(base, size_ptr, 0);
  if (const uint32_t handle_ptr = ctx.r9.u32) StoreBE32(base, handle_ptr, kInvalidHandle);
  ctx.r3.u64 = kErrorNotLoggedOn;
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("EWJ: leaderboard stats requested ({}); no Xbox Live, answering not signed in", which);
  }
}

}  // namespace

REX_HOOK_RAW(sub_82BF0D80) { NoLiveStats(ctx, base, "by rank"); }
REX_HOOK_RAW(sub_82BF0DF8) { NoLiveStats(ctx, base, "by XUID"); }
REX_HOOK_RAW(sub_82BF0E70) { NoLiveStats(ctx, base, "by rating"); }
