// Developer-only tracing hooks (-DEWJ_DEV_TOOLS=ON); never in release builds.
// Logs guest context around functions being investigated.

#include <atomic>

#include <rex/hook.h>
#include <rex/logging.h>

REX_EXTERN(__imp__sub_83391F80);
REX_HOOK_RAW(sub_83391F80) {
  static std::atomic<int> calls{0};
  if (calls++ < 5 || ctx.r3.u32 == 0)
    REXLOG_ERROR("EWJ trace: sub_83391F80(this={:08X}) lr={:08X} r4={:08X} r5={:08X}", ctx.r3.u32,
                 uint32_t(ctx.lr), ctx.r4.u32, ctx.r5.u32);
  __imp__sub_83391F80(ctx, base);
}

// Is the abi_helpers.cpp override of the weak generated __restgprlr_N_b used?
REX_EXTERN(__imp____restgprlr_29_b);
extern "C" void ewj_trace_restgpr_used() {
  static std::atomic<bool> once{false};
  if (!once.exchange(true)) REXLOG_ERROR("EWJ trace: __restgprlr_*_b override is in use");
}

// Callees of sub_83391F80's loop: does r31 survive them?
#define EWJ_TRACE_R31(addr)                                                                               REX_EXTERN(__imp__sub_##addr);                                                                         REX_HOOK_RAW(sub_##addr) {                                                                               const uint32_t r31 = ctx.r31.u32, r1 = ctx.r1.u32;                                                     __imp__sub_##addr(ctx, base);                                                                          if (ctx.r31.u32 != r31 || ctx.r1.u32 != r1)                                                               REXLOG_ERROR("EWJ trace: sub_" #addr " changed r31 {:08X}->{:08X} r1 {:08X}->{:08X}", r31, ctx.r31.u32,                    r1, ctx.r1.u32);                                                                       }
EWJ_TRACE_R31(83391CA0)
EWJ_TRACE_R31(8338FCE8)
EWJ_TRACE_R31(8338FE00)
EWJ_TRACE_R31(83390330)
EWJ_TRACE_R31(833907A8)
EWJ_TRACE_R31(83390C00)
EWJ_TRACE_R31(83390FA8)
EWJ_TRACE_R31(83391B68)

static uint32_t Load32(const uint8_t* base, uint32_t a) {
  return uint32_t(base[a]) << 24 | uint32_t(base[a + 1]) << 16 | uint32_t(base[a + 2]) << 8 | base[a + 3];
}
REX_EXTERN(__imp__sub_83391E70);
REX_HOOK_RAW(sub_83391E70) {
  const uint32_t obj = Load32(base, ctx.r3.u32 + 0x18);
  const uint32_t cb = obj ? Load32(base, obj + 12) : 0;
  const uint32_t r31 = ctx.r31.u32, r1 = ctx.r1.u32;
  __imp__sub_83391E70(ctx, base);
  if (ctx.r31.u32 != r31 || ctx.r1.u32 != r1)
    REXLOG_ERROR("EWJ trace: sub_83391E70 obj={:08X} callback={:08X} r1 {:08X}->{:08X}", obj, cb, r1, ctx.r1.u32);
}

// Profile load ("Can't Read Gamer Profile"): log what the game's profile code passes and gets back.
#define EWJ_TRACE_CALL(addr)                                                                                 REX_EXTERN(__imp__sub_##addr);                                                                            REX_HOOK_RAW(sub_##addr) {                                                                                  const uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32, a5 = ctx.r5.u32, a6 = ctx.r6.u32, a7 = ctx.r7.u32;      const uint32_t lr = uint32_t(ctx.lr);                                                                     __imp__sub_##addr(ctx, base);                                                                             REXLOG_ERROR("EWJ trace: sub_" #addr "({:08X}, {:08X}, {:08X}, {:08X}, {:08X}) from {:08X} -> {:08X}", a3, a4,                  a5, a6, a7, lr, ctx.r3.u32);                                                               }
EWJ_TRACE_CALL(82F3DDA0)

EWJ_TRACE_CALL(82F34BC8)
EWJ_TRACE_CALL(82F34C50)
EWJ_TRACE_CALL(82F3EF50)
EWJ_TRACE_CALL(82BEFFE8)
