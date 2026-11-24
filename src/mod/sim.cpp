#include "mod/sim.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "common/log.h"
#include "mod/game.h"
#include "mod/input.h"
#include "mod/netplay.h"
#include "mod/notice.h"
#include "mod/menu.h"
#include "mod/patch.h"
#include "mod/rewind.h"
#include "mod/sound.h"
#include "mod/story.h"
#include "mod/video.h"

namespace sim {
namespace {

Config g_cfg;
std::atomic<uint32_t> g_frame{0};  // every pass of the game's system update (for the log's stamps)
std::atomic<uint32_t> g_tick{0};   // game frames: passes that follow a mode's Update
uint32_t g_epoch_tick = 0;         // the tick of the latest epoch
FILE* g_log = nullptr;
FILE* g_rng_log = nullptr;
uint32_t g_rng_calls = 0;  // draws of a random number in the current frame
DWORD g_started_tick = 0;
std::atomic<DWORD> g_update_thread{0};  // the thread that is inside a frame with an Update (0: none)
std::atomic<int> g_in_movie{0};

using Void_t = void(__cdecl*)();
using Random_t = uint32_t(__cdecl*)();
using Frame_t = void(__cdecl*)(char);
using Movie_t = void(__cdecl*)(const char*);
Void_t oClockStep = nullptr;
Void_t oReadDevices = nullptr;
Random_t oRandom = nullptr;
Frame_t oFrame = nullptr;
Movie_t oMovie = nullptr;

LARGE_INTEGER g_qpf{}, g_next{};
bool g_late = false;                 // the game is more than a frame behind its clock
int g_skipped_row = 0;               // pictures left out in a row
std::atomic<unsigned> g_skipped{0};  // pictures left out so far
bool g_epoch_pending = false;
bool g_epoch_boot = false;  // the epoch is the game's very first mode entered anew (the whole start-up again)
const char kFirstMode[] = "TtMemCardBootCheckMode";
uint32_t g_epoch_seed = 0;
uint32_t g_epochs = 0;
bool g_menu_seen = false;  // the main menu has been entered once: the start-up is over
bool g_quick_boot = false; // the start-up is being played again inside the running game (no logos, eight times as fast)

// A pass of the system update is a game frame when it ends a frame whose mode was updated. The others
// are pictures only: the game's second thread draws the loading screen while a level loads (as many
// times as the loading takes), and a movie plays in a loop of its own.
bool is_tick() { return g_update_thread.load() == GetCurrentThreadId() && g_in_movie.load() == 0; }

// A game started for a session runs its start-up (the logos, the "press Enter" screen) eight times as
// fast and without the pace of 60 frames a second: every game of the session does, so they stay the same.
bool quick_start() { return (netplay::session_game() || g_quick_boot) && !g_menu_seen; }

// Frames are held at 60 a second.
void pace() {
  if (g_cfg.fast || quick_start()) return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  LONGLONG step = g_qpf.QuadPart / 60;
  // (a quarter of a second behind: a long frame, a level loaded - no catching up)
  if (g_next.QuadPart == 0 || now.QuadPart - g_next.QuadPart > step * 15) g_next = now;
  g_late = now.QuadPart - g_next.QuadPart > step;
  while (now.QuadPart < g_next.QuadPart) {
    LONGLONG left_ms = (g_next.QuadPart - now.QuadPart) * 1000 / g_qpf.QuadPart;
    if (left_ms > 2)
      Sleep(static_cast<DWORD>(left_ms - 1));
    else
      YieldProcessor();
    QueryPerformanceCounter(&now);
  }
  g_next.QuadPart += step;
}

// The game's clock for the coming frame.
void __cdecl hkClockStep() {
  if (g_cfg.free_clock) {
    oClockStep();
    return;
  }
  if (g_in_movie.load() == 0) pace();  // (a movie paces itself)
  float step = *reinterpret_cast<const float*>(game::kSpeedScale) / 60.0f;
  if (quick_start()) step *= 8.0f;
  *reinterpret_cast<float*>(game::kDeltaTime) = step;
  *reinterpret_cast<float*>(game::kAveDeltaTime) = step;
  if (!is_tick()) return;  // (pictures of a loading screen are not the game's time)
  *reinterpret_cast<float*>(game::kElapsedTime) += step;
  ++*reinterpret_cast<int*>(game::kFrameCount);
}

std::atomic<int> g_background_frames{0};  // the loading thread is inside the game's frame

void __cdecl hkFrame(char update) {
  if (!update) {
    ++g_background_frames;
    oFrame(update);
    --g_background_frames;
    return;
  }
  DWORD before = g_update_thread.exchange(GetCurrentThreadId());
  oFrame(update);
  g_update_thread = before;
}

// The picture of a game frame. A PC that cannot draw 60 pictures a second would play the game slower
// than it is, and in a session every PC plays at the pace of the slowest. So while the game is behind
// its clock, a fight's frame is played without its picture: up to three in a row (the game's own draw
// function has such a rule for the consoles; it never fires with the mod's steady clock).
//
// Drawing takes random numbers too (sparks, smoke: a number more or less by what is in view), from the
// same generator the game's rules use. So the generator is put back after every picture: what is drawn,
// how wide, or whether at all never moves the numbers the rules get.
using Draw_t = void(__cdecl*)(int);
Draw_t oDraw = nullptr;
bool g_in_draw = false;

struct RandomState {
  uint32_t next = 0, tap = 0, size = 0;
  uint8_t table[0x400];
};

uint32_t* random_head() {
  uintptr_t globals = *reinterpret_cast<const uintptr_t*>(game::kRwGlobals);
  int off = *reinterpret_cast<const int*>(game::kRandomOffset);
  if (!globals || off <= 0) return nullptr;
  auto st = reinterpret_cast<uint32_t*>(globals + off);  // {table, next, tap, end}
  return st[0] && st[3] > st[0] && st[3] - st[0] <= 0x400 ? st : nullptr;
}

bool random_save(RandomState& s) {
  uint32_t* st = random_head();
  if (!st) return false;
  s.next = st[1], s.tap = st[2], s.size = st[3] - st[0];
  memcpy(s.table, reinterpret_cast<const void*>(st[0]), s.size);
  return true;
}

void random_load(const RandomState& s) {
  uint32_t* st = random_head();
  if (!st || st[3] - st[0] != s.size) return;
  st[1] = s.next, st[2] = s.tap;
  memcpy(reinterpret_cast<void*>(st[0]), s.table, s.size);
}

// The game's particles (TtPtxSystem's Update) take their numbers from the rules' generator as well, and
// which particle systems there are, and in what order, depends on what the game has shown before: a
// game that has played a fight keeps some of them. So the particles have a generator state of their
// own, swapped in for the call: sparks and smoke differ from PC to PC, the rules' numbers never.
using PtxUpdate_t = void(__fastcall*)(void*, void*);
PtxUpdate_t oPtxUpdate = nullptr;
bool g_in_ptx = false;

void __fastcall hkPtxUpdate(void* self, void* edx) {
  static RandomState rules, own;
  static bool have_own = false;
  if (g_in_draw || g_in_ptx || g_update_thread.load() != GetCurrentThreadId() || !random_save(rules)) {
    oPtxUpdate(self, edx);
    return;
  }
  if (have_own) random_load(own);
  g_in_ptx = true;
  oPtxUpdate(self, edx);
  g_in_ptx = false;
  have_own = random_save(own);
  random_load(rules);
}

void __cdecl hkDraw(int arg) {
  if (g_update_thread.load() != GetCurrentThreadId() || g_in_movie.load() != 0) {
    oDraw(arg);  // (a loading screen's picture, on the game's second thread)
    return;
  }
  if (*reinterpret_cast<void**>(game::kMode) == *reinterpret_cast<void**>(game::kModeMeleeGame)) {
    bool skip = g_cfg.lab_draw_every > 1 ? g_tick.load() % g_cfg.lab_draw_every != 0
                                         : g_cfg.frame_skip && g_late && g_skipped_row < 3;
    if (skip) {
      ++g_skipped_row;
      ++g_skipped;
      return;
    }
  }
  g_skipped_row = 0;
  static RandomState kept;
  bool have = random_save(kept);
  g_in_draw = true;
  oDraw(arg);
  g_in_draw = false;
  if (have) random_load(kept);
}

// A movie (the attract movie, the library's clips) is a loop inside one frame, as long as the movie.
void __cdecl hkMovie(const char* name) {
  if (netplay::active()) {
    // No two PCs would leave it in the same frame.
    LOGI("game: movie %s is not played in a network game", name ? name : "?");
    return;
  }
  if (!g_cfg.movies) return;
  ++g_in_movie;
  oMovie(name);
  --g_in_movie;
  g_next.QuadPart = 0;
}

// --- the game's stopwatches ------------------------------------------------------------------------------
// Besides its clock the game times things with stopwatches of real time (QueryPerformanceCounter):
// the fade between two modes, the waits of the start-up. A fade that takes 0.35 s of real time ends a
// frame earlier on one PC than on another. The mod's stopwatches run on the game frames: a frame is
// 1/60 s for them, whatever the real time was.
using Stamp_t = void(__cdecl*)(LARGE_INTEGER*);
using Since_t = double(__cdecl*)(const LARGE_INTEGER*);
Stamp_t oStamp = nullptr;
Since_t oSince = nullptr;

// (microseconds; a start-up frame of a session game counts for eight)
std::atomic<LONGLONG> g_frame_time{0};
LONGLONG g_frame_time_kept = 0;  // at the game's first mode, with the kept memory
LONGLONG frame_time() { return g_frame_time.load(); }

void __cdecl hkStamp(LARGE_INTEGER* out) { out->QuadPart = frame_time(); }

double __cdecl hkSince(const LARGE_INTEGER* stamp) {
  return static_cast<double>(frame_time() - stamp->QuadPart) / 1000000.0;
}

uint32_t __cdecl hkRandom() {
  uint32_t v = oRandom();
  if ((g_in_draw || g_in_ptx) && g_update_thread.load() == GetCurrentThreadId()) return v;  // (a picture's own: not the rules')
  ++g_rng_calls;
  if (g_rng_log && !(g_cfg.lab_epoch && !g_epochs)) {
    uint32_t f = since_epoch();
    if (f >= g_cfg.rng_trace_from && f <= g_cfg.rng_trace_to) {
      // the callers: code addresses on the stack
      const uint32_t* sp = reinterpret_cast<const uint32_t*>(_AddressOfReturnAddress());
      char line[160];
      int n = 0, hits = 0;
      for (int i = 0; i < 120 && hits < 6; ++i) {
        uint32_t a = sp[i];
        if (a >= 0x00401000 && a < 0x0055C000) {
          n += snprintf(line + n, sizeof(line) - n, " %08X", a);
          ++hits;
        }
      }
      fprintf(g_rng_log, "%u %u %08X t%lu%s\n", f, g_rng_calls, v, GetCurrentThreadId(), line);
    }
  }
  return v;
}

const char* g_last_mode = "";

// --- the epoch: a moment from which two games are the same ------------------------------------------
// A session (and the lab's determinism runs) begins by entering the main menu's mode anew on every PC.
// Right before the game calls that mode's Enter the mod resets what the game's history has left
// different: the clock and the random numbers. Frames are counted anew from there.
void (*g_on_epoch)() = nullptr;

// The modes are objects made once; each keeps what the player did there last (the main menu's cursor,
// the heroes picked, the options of a fight). A copy of every mode as it was made is put back at an
// epoch: no mode remembers this PC's own history.
struct ModeCopy {
  uintptr_t slot;
  uint8_t bytes[0x614];
  uint16_t size;
};
ModeCopy* g_modes = nullptr;
int g_mode_count = 0;
Void_t oMakeModes = nullptr;

void __cdecl hkMakeModes() {
  oMakeModes();
  const int n = static_cast<int>(sizeof(game::kModeSizes) / sizeof(game::kModeSizes[0]));
  g_modes = new ModeCopy[n];
  g_mode_count = 0;
  for (int i = 0; i < n; ++i) {
    uintptr_t slot = game::kModeTableBegin + 4 * i;
    void* m = *reinterpret_cast<void**>(slot);
    if (!m || !*class_name(m)) {
      LOGW("sim: no mode object in slot %d (0x%08X)", i, (unsigned)slot);
      continue;
    }
    ModeCopy& c = g_modes[g_mode_count++];
    c.slot = slot;
    c.size = game::kModeSizes[i];
    memcpy(c.bytes, m, c.size);
  }
  LOGI("sim: %d modes of the game kept as they were made", g_mode_count);
}

void restore_modes() {
  for (int i = 0; i < g_mode_count; ++i) {
    void* m = *reinterpret_cast<void**>(g_modes[i].slot);
    if (m) memcpy(m, g_modes[i].bytes, g_modes[i].size);
  }
}

void* find_mode(const char* name) {
  for (uintptr_t a = game::kModeTableBegin; a < game::kModeTableEnd; a += 4) {
    void* m = *reinterpret_cast<void**>(a);
    if (m && strcmp(class_name(m), name) == 0) return m;
  }
  return nullptr;
}

bool g_met = false;

void on_enter(void* mode) {
  if (g_cfg.mode_log) LOGI("game: enters %s (frame %u)", class_name(mode), g_tick.load());
  if (!g_met) {
    // The game's very first mode. The game's memory is kept as it is now - a process that has just started -
    // to be put back whenever the game is to begin anew; a game started for a session meets the other
    // players before the mode.
    g_met = true;
    anew::capture();
    g_frame_time_kept = g_frame_time.load();
    netplay::meet();
    g_next.QuadPart = 0;
  }
  if (strcmp(class_name(mode), "TtGameStartMode") == 0) {
    g_menu_seen = true;
    g_quick_boot = false;
  }
  if (!g_epoch_pending || strcmp(class_name(mode), g_epoch_boot ? kFirstMode : "TtGameStartMode") != 0) return;
  g_epoch_pending = false;
  bool rewound = false;
  if (g_epoch_boot) {
    g_menu_seen = false;
    g_quick_boot = true;
    // (the lab's two copies have no host to take a save from: both begin without one)
    if (g_cfg.lab_epoch) DeleteFileW((module_dir() + L"tmnt.sv").c_str());
    rewound = anew::restore();
    if (rewound) g_frame_time = g_frame_time_kept;
    menu::reset();
    story::reset();
  }
  if (rewound) {
    // (the game is a just started process again: its clock, its random numbers, its modes are that process's)
    sound::on_boot();
  } else {
    if (!g_cfg.lab_keep_modes) restore_modes();
    *reinterpret_cast<float*>(game::kElapsedTime) = 0.0f;
    reseed(g_epoch_seed);
    sound::on_epoch(g_epoch_seed);
  }
  g_epoch_tick = g_tick.load();
  ++g_epochs;
  LOGI("sim: epoch %u at frame %u (seed %08X): %s begins anew%s", g_epochs, g_epoch_tick, g_epoch_seed,
       g_epoch_boot ? "the game" : "the main menu", rewound ? ", its memory put back" : "");
  if (g_log) fprintf(g_log, "EPOCH %u\n", g_epochs);
  if (g_epoch_boot) {
    netplay::meet();  // (a session that begins here: the players meet before the first mode, as in a new process)
    g_next.QuadPart = 0;
  }
  if (g_on_epoch) g_on_epoch();
}

// In place of the game's "mode->Enter()" in its frame function (ecx = the mode).
void __fastcall hkEnter(void* mode) {
  on_enter(mode);
  using Enter_t = void(__thiscall*)(void*);
  (*reinterpret_cast<Enter_t*>(*reinterpret_cast<uintptr_t*>(mode) + 4))(mode);
}

// The frame gate: the game has read its devices into the pads.
void __cdecl hkReadDevices() {
  oReadDevices();
  ++g_frame;
  if (!is_tick()) {
    // A picture, not a game frame: the pads stay what the last game frame made them.
    netplay::hold();
    return;
  }
  uint32_t t = ++g_tick;
  g_frame_time += (quick_start() ? 8 : 1) * 1000000 / 60;
  if (g_cfg.mode_log) {
    const char* m = mode_name();
    if (m != g_last_mode && strcmp(m, g_last_mode) != 0) LOGI("game: mode %s (frame %u)", m, t);
    g_last_mode = m;
  }
  // [debug] epoch = 1: the first time the main menu is up, it is entered anew (as a session's start does);
  // the lab's script, shots and autoquit count their frames from there.
  if (g_cfg.lab_epoch && !g_epochs && !g_epoch_pending && t >= g_cfg.lab_epoch_at &&
      strcmp(mode_name(), "TtGameStartMode") == 0)
    request_epoch(0x4D454C45, nullptr, g_cfg.lab_epoch_boot);
  if (g_cfg.lab_epoch && g_cfg.lab_epoch_again && g_epochs && !g_epoch_pending && since_epoch() >= g_cfg.lab_epoch_again &&
      strcmp(mode_name(), "TtGameStartMode") == 0)
    request_epoch(0x4D454C45, nullptr, g_cfg.lab_epoch_boot);
  bool lab_waits = g_cfg.lab_epoch && !g_epochs;
  uint32_t lf = lab_waits ? 0 : since_epoch();  // the frame as the lab counts it
  if (g_epoch_pending) {
    // (until the menu begins anew nobody presses anything)
    for (int pl = 0; pl < game::kPlayers; ++pl) input::write(pl, input::Pad{});
  } else if (!netplay::frame(since_epoch())) {
    input::apply_script(lab_waits ? t : lf, lab_waits);
    // (a start-up played again with nobody in a session: its "press Enter" screen is pressed by the mod)
    if (g_quick_boot && !g_menu_seen && since_epoch() % 20 == 0 && strcmp(mode_name(), "TtTitleScreenMode") == 0)
      input::press(0, game::kEnter);
  }
  if (g_cfg.lab_story_open || g_cfg.lab_ko_enemies || g_cfg.lab_ko_hero || g_cfg.lab_ko_partners) story::lab_frame(lf);
  if (g_cfg.lab_desync_at && lf == g_cfg.lab_desync_at) reinterpret_cast<uint32_t(__cdecl*)()>(game::kRpRandom)();
  if (g_cfg.lab_notice && lf == g_cfg.lab_notice) notice::say(L"ИГРОК 2 ВЫШЕЛ ИЗ ИГРЫ\nДАЛЬШЕ ВЫ ИГРАЕТЕ ОДИН", 10);
  video::on_frame(lf);
  if (g_log) {
    uint32_t pads = 2166136261u;
    auto p = reinterpret_cast<const uint8_t*>(game::kPads);
    for (int pl = 0; pl < game::kPlayers; ++pl)
      for (int i = 0; i < game::kPadValues * 4; ++i) pads = (pads ^ p[pl * game::kPadSize + i]) * 16777619u;
    fprintf(g_log, "%u %s rng %u %08X pads %08X t %.4f\n", since_epoch(), mode_name(), g_rng_calls, state_hash(), pads,
            *reinterpret_cast<const float*>(game::kElapsedTime));
  }
  g_rng_calls = 0;
  if ((g_cfg.autoquit_frame && lf >= g_cfg.autoquit_frame && (!g_cfg.lab_page || (netplay::session_game() && !g_epoch_pending && netplay::players() >= g_cfg.lab_autoquit_players))) ||
      (g_cfg.autoquit_s && GetTickCount() - g_started_tick >= static_cast<DWORD>(g_cfg.autoquit_s) * 1000)) {
    LOGI("debug: autoquit at frame %u", t);
    flush();
    netplay::leave("this game is closing");
    TerminateProcess(GetCurrentProcess(), 0);  // (a lab run: no need for the game's own way out)
  }
}

}  // namespace

uint32_t frame() { return g_tick.load(); }
unsigned pictures_skipped() { return g_skipped.load(); }
int background_frames() { return g_background_frames.load(); }
uint32_t since_epoch() { return g_tick.load() - g_epoch_tick; }
void hurry_boot() {
  if (!g_menu_seen) g_quick_boot = true;
}
uint32_t epochs() { return g_epochs; }
bool epoch_pending() { return g_epoch_pending; }

bool request_epoch(uint32_t seed, void (*then)(), bool boot) {
  void* menu = find_mode(boot ? kFirstMode : "TtGameStartMode");
  if (!menu) {
    LOGE("sim: the mode an epoch begins with is not found: no epoch");
    return false;
  }
  g_epoch_seed = seed;
  g_epoch_boot = boot;
  g_epoch_pending = true;
  g_on_epoch = then;
  *reinterpret_cast<void**>(game::kNextMode) = menu;
  LOGI("sim: an epoch is asked for (frame %u)", g_tick.load());
  return true;
}

void flush() {
  if (g_log) fflush(g_log);
  if (g_rng_log) fflush(g_rng_log);
}

const char* class_name(const void* object) {
  if (!object || IsBadReadPtr(object, 4)) return "";
  uintptr_t vt = *reinterpret_cast<const uintptr_t*>(object);
  if (vt < 0x0055C000 || vt >= 0x00583000) return "";
  uintptr_t loc = *reinterpret_cast<const uintptr_t*>(vt - 4);
  if (loc < 0x0055C000 || loc >= 0x00583000) return "";
  uintptr_t td = *reinterpret_cast<const uintptr_t*>(loc + 12);
  if (td < 0x00583000 || td >= 0x0068A000) return "";
  const char* raw = reinterpret_cast<const char*>(td + 8);  // ".?AVTtBootMode@@"
  static char names[8][64];
  static std::atomic<int> slot{0};
  char* out = names[(slot++) & 7];
  if (strncmp(raw, ".?A", 3) != 0) return "";
  size_t n = 0;
  for (const char* c = raw + 4; *c && *c != '@' && n < 63; ++c) out[n++] = *c;
  out[n] = 0;
  return out;
}

const char* mode_name() { return class_name(*reinterpret_cast<void* const*>(game::kMode)); }

uint32_t state_hash() {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      h ^= (v >> (i * 8)) & 0xFF;
      h *= 16777619u;
    }
  };
  // RenderWare's random numbers: {table, next, tap, end} and the table itself.
  uintptr_t globals = *reinterpret_cast<const uintptr_t*>(game::kRwGlobals);
  int off = *reinterpret_cast<const int*>(game::kRandomOffset);
  if (globals && off > 0) {
    auto st = reinterpret_cast<const uint32_t*>(globals + off);
    uint32_t table = st[0];
    if (table && st[3] > table && st[3] - table <= 0x400) {
      mix(st[1] - table);
      mix(st[2] - table);
      for (uint32_t a = table; a < st[3]; a += 4) mix(*reinterpret_cast<const uint32_t*>(a));
    }
  }
  mix(*reinterpret_cast<const uint32_t*>(game::kElapsedTime));
  return h;
}

void reseed(uint32_t seed) {
  reinterpret_cast<void(__cdecl*)(uint32_t)>(game::kRpRandomSeed)(seed);
  *reinterpret_cast<uint32_t*>(game::kCrtRandSeed) = seed;
}

void install(const Config& cfg) {
  g_cfg = cfg;
  g_started_tick = GetTickCount();
  QueryPerformanceFrequency(&g_qpf);
  tlog::set_frame_counter(&g_tick);
  if (cfg.sim_log) g_log = _wfopen((module_dir() + L"meleenet-sim.log").c_str(), L"w");
  if (cfg.rng_trace_to) g_rng_log = _wfopen((module_dir() + L"meleenet-rng.log").c_str(), L"w");
  static const uint8_t clock_head[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};  // push ebp; mov ebp, esp; and esp, -8
  static const uint8_t read_head[] = {0xA1, 0xE0, 0x0E, 0x5B, 0x00};         // mov eax, [0x5B0EE0]
  static const uint8_t frame_head[] = {0xF6, 0x05, 0x90, 0x09, 0x5B, 0x00, 0x01};  // test byte [0x5B0990], 1
  static const uint8_t movie_head[] = {0x81, 0xEC, 0x0C, 0x02, 0x00, 0x00};  // sub esp, 0x20C
  static const uint8_t enter_call[] = {0x8B, 0x01, 0xFF, 0x50, 0x04};        // mov eax, [ecx]; call [eax+4]
  static const uint8_t draw_head[] = {0x83, 0xEC, 0x14, 0x8D, 0x44, 0x24, 0x04};  // sub esp, 0x14; lea eax, [esp+4]
  if (!patch::bytes_are(game::kClockStep, clock_head, sizeof(clock_head)) ||
      !patch::bytes_are(game::kReadDevices, read_head, sizeof(read_head)) ||
      !patch::bytes_are(game::kFrame, frame_head, sizeof(frame_head)) ||
      !patch::bytes_are(game::kPlayMovie, movie_head, sizeof(movie_head)) ||
      !patch::bytes_are(game::kEnterCall, enter_call, sizeof(enter_call))) {
    LOGE("sim: the game's frame functions are not where they should be; the mod's frame gate is off");
    return;
  }
  patch::hook("the game's modes", game::kMakeModes, (void*)&hkMakeModes, (void**)&oMakeModes);
  patch::hook("the game's frame", game::kFrame, (void*)&hkFrame, (void**)&oFrame);
  patch::hook("the game's clock", game::kClockStep, (void*)&hkClockStep, (void**)&oClockStep);
  patch::hook("the frame gate (devices read)", game::kReadDevices, (void*)&hkReadDevices, (void**)&oReadDevices);
  patch::hook("the game's movie player", game::kPlayMovie, (void*)&hkMovie, (void**)&oMovie);
  static const uint8_t ptx_head[] = {0x83, 0xEC, 0x4C, 0x55, 0x56, 0x8B, 0xF1};  // sub esp, 0x4C; push ebp; push esi; mov esi, ecx
  if (!cfg.free_clock && patch::bytes_are(game::kPtxUpdate, ptx_head, sizeof(ptx_head)))
    patch::hook("the game's particles (random numbers of their own)", game::kPtxUpdate, (void*)&hkPtxUpdate, (void**)&oPtxUpdate);
  if (!cfg.free_clock && patch::bytes_are(game::kDraw, draw_head, sizeof(draw_head)))
    patch::hook("the game's picture (frames without one when the PC is behind)", game::kDraw, (void*)&hkDraw, (void**)&oDraw);
  if (!cfg.free_clock) {
    patch::hook("the game's stopwatch (start)", game::kStopwatchStart, (void*)&hkStamp, (void**)&oStamp);
    patch::hook("the game's stopwatch (read)", game::kStopwatchRead, (void*)&hkSince, (void**)&oSince);
  }
  // The game's "mode->Enter()" becomes a call of the mod's.
  uint8_t call[5] = {0xE8};
  int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&hkEnter) - (game::kEnterCall + 5));
  memcpy(call + 1, &rel, 4);
  patch::replace("the game's mode entry", game::kEnterCall, enter_call, call, sizeof(call));
  if (cfg.sim_log || cfg.rng_trace_to)
    patch::hook("the game's random numbers", game::kRpRandom, (void*)&hkRandom, (void**)&oRandom);
  LOGI("sim: the game's clock is the mod's: %s", cfg.free_clock ? "left to the game ([debug] free_clock)" : "0.85 / 60 s a frame");
}

}  // namespace sim
