// Leaderboard stats without Xbox Live (issue #2: crash on "Continue").
//
// Continuing a saved game reads leaderboard stats. The XDK wrappers
// XUserCreateStatsEnumeratorByRank / ByXuid / ByRating (sub_82BF0D80 /
// sub_82BF0DF8 / sub_82BF0E70; args: title, pivot, rows, spec count, specs,
// DWORD* buffer size, HANDLE* enum) end in XamUserCreateStatsEnumerator, which
// ReXGlue only stubs: no return value and no buffer size, so the game
// allocated garbage (v0.9.0 crash).
//
// Answering with an error (v0.9.1) is not enough: on the by-rank path
// (sub_82B15838) the game records the error, then overwrites it with
// "in progress", enumerates anyway and parses a null result buffer (read of
// guest 0x4). The game never expected this to fail, so answer the way a
// leaderboard with no entries does:
//  - create: success, buffer size for XUSER_STATS_READ_RESULTS plus one
//    XUSER_STATS_VIEW per requested spec, and a port-owned handle;
//  - XEnumerate (sub_82BF1360 / sub_82BF1380; args: handle, buffer, size,
//    DWORD* items, XOVERLAPPED*) on that handle: write the header and one view
//    per spec with zero rows, complete the overlapped with success, and return
//    ERROR_IO_PENDING like the real asynchronous call. Other handles pass
//    through to the game's own code untouched.

#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>

namespace {

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorIoPending = 0x3E5;
constexpr uint32_t kHandleBase = 0xEC000000u;  // never used by the runtime's handles (0xF8xxxxxx)
constexpr uint32_t kSpecSize = 4 + 4 + 64 * 2;  // XUSER_STATS_SPEC: view id, column count, 64 column ids
constexpr uint32_t kViewSize = 16;              // XUSER_STATS_VIEW: id, total rows, rows, rows ptr

uint32_t LoadBE32(const uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, sizeof(v));
  return std::byteswap(v);
}

void StoreBE32(uint8_t* base, uint32_t addr, uint32_t v) {
  v = std::byteswap(v);
  std::memcpy(base + addr, &v, sizeof(v));
}

std::mutex g_mutex;
std::map<uint32_t, std::vector<uint32_t>> g_enumerators;  // handle -> requested view ids
uint32_t g_next = 1;

void CreateEmptyStatsEnumerator(PPCContext& ctx, uint8_t* base, const char* which) {
  const uint32_t spec_count = ctx.r6.u32;
  const uint32_t specs = ctx.r7.u32;
  const uint32_t size_ptr = ctx.r8.u32;
  const uint32_t handle_ptr = ctx.r9.u32;
  if (!spec_count || spec_count > 64 || !specs || !size_ptr || !handle_ptr) {
    ctx.r3.u64 = 0x57;  // ERROR_INVALID_PARAMETER, as XAM answers
    return;
  }
  std::vector<uint32_t> views;
  for (uint32_t i = 0; i < spec_count; ++i) views.push_back(LoadBE32(base, specs + i * kSpecSize));
  uint32_t handle;
  {
    std::lock_guard lock(g_mutex);
    handle = kHandleBase | (g_next++ & 0xFFFFFF);
    g_enumerators[handle] = std::move(views);
  }
  StoreBE32(base, size_ptr, 8 + kViewSize * spec_count);
  StoreBE32(base, handle_ptr, handle);
  ctx.r3.u64 = kErrorSuccess;
  REXLOG_INFO("EWJ: leaderboard stats requested ({}, {} views); no Xbox Live, answering with empty leaderboards",
              which, spec_count);
}

// True when it handled the call (one of our handles).
bool EnumerateEmptyStats(PPCContext& ctx, uint8_t* base) {
  const uint32_t handle = ctx.r3.u32;
  std::vector<uint32_t> views;
  {
    std::lock_guard lock(g_mutex);
    auto it = g_enumerators.find(handle);
    if (it == g_enumerators.end()) return false;
    views = it->second;
  }
  const uint32_t buffer = ctx.r4.u32;
  const uint32_t size = ctx.r5.u32;
  const uint32_t items_ptr = ctx.r6.u32;
  const uint32_t overlapped = ctx.r7.u32;
  const uint32_t needed = 8 + kViewSize * uint32_t(views.size());
  uint32_t result = kErrorSuccess;
  if (!buffer || size < needed) {
    result = 0x7A;  // ERROR_INSUFFICIENT_BUFFER
  } else {
    // XUSER_STATS_READ_RESULTS { views, pViews } then the views, each with no rows.
    std::memset(base + buffer, 0, needed);
    StoreBE32(base, buffer, uint32_t(views.size()));
    StoreBE32(base, buffer + 4, buffer + 8);
    for (uint32_t i = 0; i < views.size(); ++i) StoreBE32(base, buffer + 8 + i * kViewSize, views[i]);
  }
  const uint32_t items = result == kErrorSuccess ? 1 : 0;
  if (items_ptr) StoreBE32(base, items_ptr, items);
  if (overlapped) {
    // XOVERLAPPED: InternalLow (result), InternalHigh (items), ... The game polls
    // InternalLow until it is no longer ERROR_IO_PENDING.
    // XGetOverlappedResult (sub_82BF1330) then returns the extended error at +0x18.
    StoreBE32(base, overlapped + 0x18, result);
    StoreBE32(base, overlapped + 4, items);
    StoreBE32(base, overlapped, result);
    ctx.r3.u64 = kErrorIoPending;
  } else {
    ctx.r3.u64 = result;
  }
  return true;
}

}  // namespace

REX_HOOK_RAW(sub_82BF0D80) { CreateEmptyStatsEnumerator(ctx, base, "by rank"); }
REX_HOOK_RAW(sub_82BF0DF8) { CreateEmptyStatsEnumerator(ctx, base, "by XUID"); }
REX_HOOK_RAW(sub_82BF0E70) { CreateEmptyStatsEnumerator(ctx, base, "by rating"); }

REX_EXTERN(__imp__sub_82BF1360);
REX_HOOK_RAW(sub_82BF1360) {
  if (!EnumerateEmptyStats(ctx, base)) __imp__sub_82BF1360(ctx, base);
}
REX_EXTERN(__imp__sub_82BF1380);
REX_HOOK_RAW(sub_82BF1380) {
  if (!EnumerateEmptyStats(ctx, base)) __imp__sub_82BF1380(ctx, base);
}
