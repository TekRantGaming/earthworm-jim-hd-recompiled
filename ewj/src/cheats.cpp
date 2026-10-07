// Cheats (issue #1), set on the launcher's Cheats page.
//
// Jim is the actor whose main vtable is 0x82425D00 (wormcfg01). Every actor's
// health goes through Actor::SetHP (sub_82854E00; health at +0x254, maximum at
// +0x284); a hit drains it a few points per frame through that call. Jim's
// object also holds his lives (+0x340, taken away when he respawns) and his
// plasma ammo (+0x348, 1000 when full). Found with dev_cheat_scan.cpp.
//
// His movement and gun settings are integers the game loads into globals from
// its wormcfg01 parameters (sub_82A58140) whenever a level loads; the cheats
// scale whatever the game last loaded, so turning one off gives the game's own
// value back.
//
// The checks run after Jim's per-frame update (sub_82A4F648, called with his
// second base, object + 0x118), so the HUD never shows a lost life or shot.

#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(ewj_cheat_health, false, "EWJ/Cheats", "Infinite health");
REXCVAR_DEFINE_BOOL(ewj_cheat_lives, false, "EWJ/Cheats", "Infinite lives");
REXCVAR_DEFINE_BOOL(ewj_cheat_ammo, false, "EWJ/Cheats", "Infinite plasma ammo");
REXCVAR_DEFINE_BOOL(ewj_cheat_rapid_fire, false, "EWJ/Cheats", "Rapid fire");
REXCVAR_DEFINE_BOOL(ewj_cheat_high_jump, false, "EWJ/Cheats", "High jump");
REXCVAR_DEFINE_BOOL(ewj_cheat_fast_run, false, "EWJ/Cheats", "Fast run");

namespace {

constexpr uint32_t kJimVtable = 0x82425D00;
constexpr uint32_t kHp = 0x254;
constexpr uint32_t kLives = 0x340;
constexpr uint32_t kAmmo = 0x348;

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, sizeof(v));
  return std::byteswap(v);
}

void Store32(uint8_t* base, uint32_t addr, uint32_t v) {
  v = std::byteswap(v);
  std::memcpy(base + addr, &v, sizeof(v));
}

bool IsJim(const uint8_t* base, uint32_t self) { return self && Load32(base, self) == kJimVtable; }

// A counter the cheat keeps from going down (it may still go up: pickups).
struct Keep {
  uint32_t object = 0;
  int32_t value = 0;

  void Apply(uint8_t* base, uint32_t self, uint32_t field, bool on) {
    const int32_t now = int32_t(Load32(base, self + field));
    if (!on || object != self || now >= value) {
      object = self;
      value = now;
      return;
    }
    Store32(base, self + field, uint32_t(value));
  }
};

Keep g_lives, g_ammo;

// A wormcfg01 parameter the game loaded into a global, scaled while a cheat is on.
struct Tunable {
  uint32_t addr;
  int32_t num, den;  // scale while on
  int32_t game = 0;  // the game's own value
  int32_t written = 0;
  bool seen = false;

  void Apply(uint8_t* base, bool on) {
    const int32_t now = int32_t(Load32(base, addr));
    if (!seen || now != written) game = now;  // first look, or the game loaded it again
    seen = true;
    int32_t want = on ? int32_t(int64_t(game) * num / den) : game;
    if (on && game != 0 && want == 0) want = game > 0 ? 1 : -1;
    if (want != now) Store32(base, addr, uint32_t(want));
    written = want;
  }
};

Tunable g_shoot_interval{0x838424E4, 1, 3};  // WORM_SHOOT_INTERVAL
Tunable g_jumps[] = {
    {0x838945A8, 5, 4},  // WORM_JUMP_SPEED_Y
    {0x8389452C, 5, 4},  // WORM_BARE_JUMP_SPEED_Y
    {0x838945D8, 5, 4},  // WORM_SUPER_JUMP_SPEED_Y
    {0x838944F4, 5, 4},  // WORM_SUPER_JUMP_SPEED_Y_NORMAL
    {0x83894508, 5, 4},  // WORM_SUPER_JUMP_SPEED_Y_FAST
};
Tunable g_runs[] = {
    {0x838945E4, 3, 2},  // WORM_RUN_SPEED
    {0x838944E4, 3, 2},  // WORM_RUN_JUMP_SPEED_X
    {0x83894540, 3, 2},  // WORM_JUMP_SPEED_X
};

}  // namespace

// Actor::SetHP(this, hp): Jim's health never goes down with infinite health
// (respawning and health pickups still set it).
REX_EXTERN(__imp__sub_82854E00);
REX_HOOK_RAW(sub_82854E00) {
  if (REXCVAR_GET(ewj_cheat_health) && IsJim(base, ctx.r3.u32)) {
    const int32_t hp = int32_t(Load32(base, ctx.r3.u32 + kHp));
    if (hp > 0 && ctx.r4.s32 < hp) ctx.r4.u64 = uint32_t(hp);
  }
  __imp__sub_82854E00(ctx, base);
}

// Jim's per-frame update.
REX_EXTERN(__imp__sub_82A4F648);
REX_HOOK_RAW(sub_82A4F648) {
  const uint32_t self = ctx.r3.u32 - 0x118;
  __imp__sub_82A4F648(ctx, base);
  if (!IsJim(base, self)) return;
  static uint32_t logged = 0;
  if (logged != self) {
    logged = self;
    REXLOG_INFO("EWJ: Jim spawned: {} lives, {} ammo, health {}/{}; cheats: health {} lives {} ammo {} rapid fire {} "
                "high jump {} fast run {}",
                int32_t(Load32(base, self + kLives)), int32_t(Load32(base, self + kAmmo)),
                int32_t(Load32(base, self + kHp)), int32_t(Load32(base, self + 0x284)), REXCVAR_GET(ewj_cheat_health),
                REXCVAR_GET(ewj_cheat_lives), REXCVAR_GET(ewj_cheat_ammo), REXCVAR_GET(ewj_cheat_rapid_fire),
                REXCVAR_GET(ewj_cheat_high_jump), REXCVAR_GET(ewj_cheat_fast_run));
  }
  g_lives.Apply(base, self, kLives, REXCVAR_GET(ewj_cheat_lives));
  g_ammo.Apply(base, self, kAmmo, REXCVAR_GET(ewj_cheat_ammo));
  g_shoot_interval.Apply(base, REXCVAR_GET(ewj_cheat_rapid_fire));
  for (auto& t : g_jumps) t.Apply(base, REXCVAR_GET(ewj_cheat_high_jump));
  for (auto& t : g_runs) t.Apply(base, REXCVAR_GET(ewj_cheat_fast_run));
}
