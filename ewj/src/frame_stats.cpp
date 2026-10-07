// Guest frame-rate measurement.
//
// sub_82C211F8 is the title's D3D swap routine (builds the swap packet and
// calls VdSwap), so it runs once per presented guest frame. Count calls to
// get the real game frame rate (the host presenter can run faster), log it
// once a second, and feed ReXGlue's F3 debug overlay. Also applies the
// ewj_frame_rate cap.

#include "frame_stats.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <rex/hook.h>
#include <rex/logging.h>

#include "settings.h"

namespace ewj {
namespace {

using Clock = std::chrono::steady_clock;

std::atomic<uint64_t> g_frames{0};
std::mutex g_mutex;
Clock::time_point g_window_start = Clock::now();
uint64_t g_window_frames = 0;
double g_window_swap_ms = 0;  // time spent inside the swap call this window
rex::ui::FrameStats g_stats;

struct Deferred {
  double after;
  std::function<void()> fn;
};
std::vector<Deferred> g_deferred;
Clock::time_point g_first_frame{};

void RunDeferredIfDue(Clock::time_point now) {
  std::vector<std::function<void()>> due;
  {
    std::lock_guard lock(g_mutex);
    if (g_first_frame == Clock::time_point{}) g_first_frame = now;
    if (g_deferred.empty()) return;
    const double elapsed = std::chrono::duration<double>(now - g_first_frame).count();
    for (auto it = g_deferred.begin(); it != g_deferred.end();) {
      if (elapsed >= it->after) {
        due.push_back(std::move(it->fn));
        it = g_deferred.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& fn : due) fn();
}

// Every 10 s: average and 1% low frame rate, the worst frame and how many
// frames took over 50 and 100 ms, so stutter shows up in players' logs.
void RecordFrameTime(double frame_ms) {
  static std::vector<double> times;
  static Clock::time_point start = Clock::now();
  times.push_back(frame_ms);
  const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
  if (elapsed < 10.0) return;
  std::sort(times.begin(), times.end());
  const size_t n = times.size();
  double sum = 0, low_sum = 0;
  for (double t : times) sum += t;
  const size_t low_n = std::max<size_t>(1, n / 100);
  for (size_t i = n - low_n; i < n; ++i) low_sum += times[i];
  const auto over = [&](double ms) { return size_t(times.end() - std::upper_bound(times.begin(), times.end(), ms)); };
  REXLOG_INFO("Frame times: {:.1f} FPS average, {:.1f} FPS 1% low, worst {:.0f} ms, {} over 50 ms, {} over 100 ms",
              1000.0 * n / sum, 1000.0 * low_n / low_sum, times.back(), over(50.0), over(100.0));
  times.clear();
  start = Clock::now();
}

void OnGuestSwap() {
  RunDeferredIfDue(Clock::now());
  const uint64_t total = g_frames.fetch_add(1) + 1;
  std::lock_guard lock(g_mutex);
  ++g_window_frames;
  const auto now = Clock::now();
  // Log single long frames (stutter); the per-second average hides them.
  static Clock::time_point last_swap{};
  if (last_swap != Clock::time_point{}) {
    const double frame_ms = std::chrono::duration<double, std::milli>(now - last_swap).count();
    if (frame_ms >= 50.0) REXLOG_INFO("Hitch: {:.0f} ms frame", frame_ms);
    RecordFrameTime(frame_ms);
  }
  last_swap = now;
  const double elapsed = std::chrono::duration<double>(now - g_window_start).count();
  if (elapsed >= 1.0) {
    g_stats.fps = g_window_frames / elapsed;
    g_stats.frame_time_ms = elapsed * 1000.0 / g_window_frames;
    g_stats.frame_count = total;
    REXLOG_INFO("Guest FPS: {:.1f} ({:.2f} ms/frame, {:.2f} ms in swap)",
                g_stats.fps, g_stats.frame_time_ms, g_window_swap_ms / g_window_frames);
    g_window_start = now;
    g_window_frames = 0;
    g_window_swap_ms = 0;
  }
}


void AddSwapTime(double ms) {
  std::lock_guard lock(g_mutex);
  g_window_swap_ms += ms;
}

}  // namespace

rex::ui::FrameStats GetGuestFrameStats() {
  std::lock_guard lock(g_mutex);
  return g_stats;
}

void RunAfterFirstFrame(double seconds, std::function<void()> fn) {
  std::lock_guard lock(g_mutex);
  g_deferred.push_back({seconds, std::move(fn)});
}

void RunAfterDelay(double seconds, std::function<void()> fn) {
  std::lock_guard lock(g_mutex);
  const double now = g_first_frame == Clock::time_point{}
                         ? 0.0
                         : std::chrono::duration<double>(Clock::now() - g_first_frame).count();
  g_deferred.push_back({now + seconds, std::move(fn)});
}

}  // namespace ewj

namespace ewj {
namespace {

// The game's own frame pacing: main() (0x82BEEA48) reads a millisecond clock,
// skips the frame while less than the minimum frame time at 0x8384465C has
// passed (16 ms, set at startup: ~62.5 FPS), then calls the game's update with
// the elapsed milliseconds. Game logic runs on that delta, so a smaller minimum
// lets it run above 60 at normal speed; LimitFrameRate() does the exact pacing.
constexpr uint32_t kMinFrameMsAddr = 0x8384465C;

void ApplyGameFrameCap(uint8_t* base) {
  const int32_t fps = REXCVAR_GET(ewj_frame_rate);
  // At or below 60 keep the game's own 16 ms; above it, the largest whole
  // millisecond count that still allows the chosen rate (1 for unlimited).
  const uint32_t min_ms = fps > 0 && fps <= 60 ? 16 : fps > 0 ? std::max(1, 1000 / fps) : 1;
  uint8_t* p = base + kMinFrameMsAddr;
  const uint32_t current = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  if (current == min_ms || current == 0) return;  // 0: main() has not set it up yet
  p[0] = uint8_t(min_ms >> 24);
  p[1] = uint8_t(min_ms >> 16);
  p[2] = uint8_t(min_ms >> 8);
  p[3] = uint8_t(min_ms);
}

// Frame limiter for ewj_frame_rate: high-resolution waitable timer for the bulk
// of the wait, then a short spin for precision.
void LimitFrameRate() {
  const int32_t fps = REXCVAR_GET(ewj_frame_rate);
  static Clock::time_point next{};
  if (fps <= 0) {
    next = {};
    return;
  }
  const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps));
  auto now = Clock::now();
  if (next == Clock::time_point{} || now - next > period) {
    next = now + period;  // first frame, or fell behind: resync
    return;
  }
#if defined(_WIN32)
  static HANDLE timer =
      CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  const auto coarse = next - now - std::chrono::microseconds(500);
  if (timer && coarse > Clock::duration::zero()) {
    LARGE_INTEGER due;
    due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(coarse).count() / 100;
    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, INFINITE);
  }
#else
  // Sleep for the bulk of the wait, then spin for precision.
  const auto coarse = next - now - std::chrono::microseconds(500);
  if (coarse > Clock::duration::zero()) std::this_thread::sleep_for(coarse);
#endif
  while (Clock::now() < next) std::this_thread::yield();
  next += period;
}

}  // namespace
}  // namespace ewj

REX_EXTERN(__imp__sub_82C211F8);
REX_HOOK_RAW(sub_82C211F8) {
#if defined(EWJ_DEV_TOOLS)
  // Guest call stack (back chain at 0(r1), saved LR at -8 of the caller's frame)
  // a few times, to find the game's main loop.
  static int traced = 0;
  if (++traced % 600 == 300 && traced < 2000) {
    auto load = [base](uint32_t a) {
      const uint8_t* p = base + a;
      return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    };
    std::string chain = fmt::format("{:08X}", uint32_t(ctx.lr));
    uint32_t sp = ctx.r1.u32;
    for (int i = 0; i < 12 && sp; ++i) {
      const uint32_t prev = load(sp);
      // Frames are small and grow upward toward the stack top; stop at anything else
      // (the outermost frame's link points past the mapped stack).
      if (!prev || prev <= sp || prev - sp > 0x8000) break;
      const uint32_t lr = load(prev - 8);
      if (lr < 0x82000000 || lr >= 0x84000000) break;
      chain += fmt::format(" <- {:08X}", lr);
      sp = prev;
    }
    REXLOG_INFO("EWJ trace: swap stack {}", chain);
  }
  {
    // Game time vs wall time: the game's per-frame delta (ms) at 0x838D27C8.
    static double game_ms = 0;
    static auto window = ewj::Clock::now();
    const uint8_t* d = base + 0x838D27C8;
    game_ms += int32_t(uint32_t(d[0]) << 24 | uint32_t(d[1]) << 16 | uint32_t(d[2]) << 8 | d[3]);
    const double wall_ms = std::chrono::duration<double, std::milli>(ewj::Clock::now() - window).count();
    if (wall_ms >= 10000) {
      REXLOG_INFO("EWJ trace: game time {:.0f} ms in {:.0f} ms wall (x{:.3f})", game_ms, wall_ms, game_ms / wall_ms);
      game_ms = 0;
      window = ewj::Clock::now();
    }
  }
#endif
  ewj::ApplyGameFrameCap(base);
  ewj::LimitFrameRate();
  ewj::OnGuestSwap();
  const auto start = ewj::Clock::now();
  __imp__sub_82C211F8(ctx, base);
  ewj::AddSwapTime(std::chrono::duration<double, std::milli>(ewj::Clock::now() - start).count());
}
