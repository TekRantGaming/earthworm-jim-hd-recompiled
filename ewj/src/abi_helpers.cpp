// Second copy of the compiler's __restgprlr_N helpers.
//
// The image links the register save/restore helpers twice (0x829A8DD0.. and
// 0x83250E20..). ReXGlue recognises only the first copy, so overrides.toml
// declares each entry of the second copy as its own 4-byte function: config
// functions may not overlap, and a 4-byte function returns after one load
// instead of falling through to the next entry. These strong definitions
// replace those (weak) generated functions with the real behaviour:
//   __restgprlr_N:  ld rN..r31, -(8 * (32 - N)) - 8 (r1) ...; lwz r12, -8(r1);
//                   mtlr r12; blr

#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>

namespace {

template <typename T>
T LoadBE(const uint8_t* base, uint32_t addr) {
  T v;
  std::memcpy(&v, base + addr, sizeof(v));
  return std::byteswap(v);
}

// PPCContext does not keep the GPRs in register order.
constexpr PPCRegister PPCContext::*kSavedGprs[] = {
    &PPCContext::r14, &PPCContext::r15, &PPCContext::r16, &PPCContext::r17, &PPCContext::r18, &PPCContext::r19,
    &PPCContext::r20, &PPCContext::r21, &PPCContext::r22, &PPCContext::r23, &PPCContext::r24, &PPCContext::r25,
    &PPCContext::r26, &PPCContext::r27, &PPCContext::r28, &PPCContext::r29, &PPCContext::r30, &PPCContext::r31,
};

// r14 is at -0x98(r1), r15 at -0x90(r1), ... r31 at -0x10(r1).
#if defined(EWJ_DEV_TOOLS)
extern "C" void ewj_trace_restgpr_used();
#endif

template <int N>
void RestGprLr(PPCContext& ctx, uint8_t* base) {
#if defined(EWJ_DEV_TOOLS)
  ewj_trace_restgpr_used();
#endif
  const uint32_t sp = ctx.r1.u32;
  for (int r = N; r <= 31; ++r) (ctx.*kSavedGprs[r - 14]).u64 = LoadBE<uint64_t>(base, sp - 0x98 + 8 * (r - 14));
  ctx.r12.u64 = LoadBE<uint32_t>(base, sp - 8);
  ctx.lr = ctx.r12.u64;
}

}  // namespace

#define EWJ_RESTGPRLR(n) \
  REX_HOOK_RAW(__restgprlr_##n##_b) { RestGprLr<n>(ctx, base); }

EWJ_RESTGPRLR(14)
EWJ_RESTGPRLR(15)
EWJ_RESTGPRLR(16)
EWJ_RESTGPRLR(17)
EWJ_RESTGPRLR(18)
EWJ_RESTGPRLR(19)
EWJ_RESTGPRLR(20)
EWJ_RESTGPRLR(21)
EWJ_RESTGPRLR(22)
EWJ_RESTGPRLR(23)
EWJ_RESTGPRLR(24)
EWJ_RESTGPRLR(25)
EWJ_RESTGPRLR(26)
EWJ_RESTGPRLR(27)
EWJ_RESTGPRLR(28)
EWJ_RESTGPRLR(29)
EWJ_RESTGPRLR(30)
EWJ_RESTGPRLR(31)
