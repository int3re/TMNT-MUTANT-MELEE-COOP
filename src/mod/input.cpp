#include "mod/input.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "common/log.h"
#include "mod/game.h"
#include "mod/netplay.h"
#include "mod/patch.h"
#include "mod/sim.h"
#include "mod/video.h"

namespace input {
namespace {

Config g_cfg;

float* pad_values(int player) { return reinterpret_cast<float*>(game::kPads + player * game::kPadSize); }

// --- the lab's script ------------------------------------------------------------------------------
// Lines of "<frame>[-<frame>] [p<N>.]<control>[+<control>...]"; '#' starts a comment.
struct Press {
  unsigned from, to;
  int player;  // 0..3
  Pad pad;
  bool pre;    // "pre <frame> ...": frames of the run before the lab's epoch
  int where;   // 0: any game; 1 "menu <line>": before any session; 2 "game <line>": in a session;
               // 3 "solo <line>": alone again after a session (the game has begun anew for its own player)
};
std::vector<Press> g_script;

bool control(const std::string& name, Pad& p) {
  struct {
    const char* name;
    int value;
  } static const buttons[] = {{"special", game::kSpecial}, {"block", game::kBlock},   {"attack", game::kAttack},
                              {"alt", game::kAltAttack},   {"jump", game::kJump},     {"action", game::kAction},
                              {"esc", game::kEscape},      {"enter", game::kEnter},   {"space", game::kSpaceKey},
                              {"back", game::kBack},       {"start", game::kStart},
                              {"f11", game::kF11}};
  if (name == "up") p.y = 127;
  else if (name == "down") p.y = -127;
  else if (name == "left") p.x = -127;
  else if (name == "right") p.x = 127;
  else {
    for (const auto& b : buttons)
      if (name == b.name) {
        p.buttons |= bit(b.value);
        return true;
      }
    return false;
  }
  return true;
}

void load_script(const std::wstring& path) {
  FILE* f = _wfopen(path.c_str(), L"r");
  if (!f) {
    LOGW("debug: script %ls not found", path.c_str());
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    if (char* c = strchr(line, '#')) *c = 0;
    unsigned a = 0, b = 0;
    char what[128] = {};
    const char* text = line;
    while (*text == ' ') ++text;
    bool pre = strncmp(text, "pre ", 4) == 0;
    if (pre) text += 4;
    // "menu <line>": only in a game that is not a session's; "game <line>": only in a session's game
    // (a run through the network page is two games of one copy, one after the other).
    int where = 0;
    if (strncmp(text, "menu ", 5) == 0) {
      where = 1;
      text += 5;
    } else if (strncmp(text, "game ", 5) == 0) {
      where = 2;
      text += 5;
    } else if (strncmp(text, "solo ", 5) == 0) {
      where = 3;
      text += 5;
    }
    if (sscanf(text, "%u-%u %127s", &a, &b, what) != 3) {
      if (sscanf(text, "%u %127s", &a, what) != 2) continue;
      b = a + 3;  // a tap: four frames
    }
    Press p{a, b, 0, {}, pre, where};
    std::string s = what;
    if (s.size() > 3 && s[0] == 'p' && s[2] == '.' && s[1] >= '1' && s[1] <= '4') {
      p.player = s[1] - '1';
      s.erase(0, 3);
    }
    bool ok = true;
    for (size_t i = 0; i <= s.size() && ok;) {
      size_t j = s.find('+', i);
      if (j == std::string::npos) j = s.size();
      ok = control(s.substr(i, j - i), p.pad);
      i = j + 1;
    }
    if (ok)
      g_script.push_back(p);
    else
      LOGW("debug: script: unknown control in '%s'", what);
  }
  fclose(f);
  LOGI("debug: script %ls: %u presses", path.c_str(), (unsigned)g_script.size());
}

bool g_pre = false;  // the run is before the lab's epoch

bool scripted(unsigned frame, int player, Pad& out) {
  bool any = false;
  for (const Press& p : g_script) {
    if (p.player != player || p.pre != g_pre || frame < p.from || frame > p.to) continue;
    if (p.where) {
      int now = netplay::session_game() ? 2 : sim::epochs() ? 3 : 1;
      if (p.where != now) continue;
    }
    if (p.pad.x) out.x = p.pad.x;
    if (p.pad.y) out.y = p.pad.y;
    out.buttons |= p.pad.buttons;
    any = true;
  }
  return any;
}

int8_t to_axis(float v) {
  if (v > 1.0f) v = 1.0f;
  if (v < -1.0f) v = -1.0f;
  return static_cast<int8_t>(v * 127.0f + (v >= 0 ? 0.5f : -0.5f));
}

// --- the mouse's buttons as keys ----------------------------------------------------------------------
// The game turns the keyboard into a pad in one function (0x4068F0), right after it read the keyboard.
using KeyboardToPad_t = void(__cdecl*)(int, int);
KeyboardToPad_t oKeyboardToPad = nullptr;

void __cdecl hkKeyboardToPad(int pad, int player) {
  auto keys = reinterpret_cast<uint8_t*>(game::kKeyboardState);
  static const int vk[5] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
  bool live = g_cfg.mouse && video::in_front();
  if (live) {
    // only while the pointer is over the game's picture: a click on another window is not a punch
    POINT pt;
    RECT rc;
    HWND hwnd = video::window();
    live = GetCursorPos(&pt) && GetClientRect(hwnd, &rc) && ScreenToClient(hwnd, &pt) && PtInRect(&rc, pt);
  }
  for (int i = 0; i < 5; ++i) keys[kMouseLeft + i] = live && (GetAsyncKeyState(vk[i]) & 0x8000) ? 0x80 : 0;
  oKeyboardToPad(pad, player);
}

}  // namespace

Pad read_local(unsigned frame) {
  Pad p;
  // This PC's own player is whoever the bindings call player 1 (the keyboard unless the player chose otherwise).
  const float* v = pad_values(0);
  p.x = to_axis(v[game::kStickX]);
  p.y = to_axis(v[game::kStickY]);
  for (int i = game::kSpecial; i <= game::kF11; ++i) {
    if (i == game::kDebugX || i == game::kDebugY || i == game::kDebugMenu) continue;  // the developers' menu stays this PC's own
    if (v[i] > 0.5f) p.buttons |= bit(i);
  }
  scripted(frame, 0, p);
  return p;
}

void write(int player, const Pad& pad) {
  float* v = pad_values(player);
  v[game::kStickX] = pad.x / 127.0f;
  v[game::kStickY] = pad.y / 127.0f;
  for (int i = game::kSpecial; i <= game::kF11; ++i) {
    if (i == game::kDebugX || i == game::kDebugY || i == game::kDebugMenu) continue;
    v[i] = (pad.buttons & bit(i)) ? 1.0f : 0.0f;
  }
}

void press(int player, int value) { pad_values(player)[value] = 1.0f; }

void set_connected(int player, bool on) {
  *reinterpret_cast<uint8_t*>(game::kPads + player * game::kPadSize + game::kPadConnected) = on ? 1 : 0;
}

bool connected(int player) {
  return *reinterpret_cast<const uint8_t*>(game::kPads + player * game::kPadSize + game::kPadConnected) != 0;
}

void apply_script(unsigned frame, bool pre) {
  g_pre = pre;
  if (g_script.empty()) return;
  for (int pl = 0; pl < game::kPlayers; ++pl) {
    Pad p;
    if (pl == 0) {
      p = read_local(frame);
      write(0, p);
      continue;
    }
    if (!scripted(frame, pl, p)) continue;
    set_connected(pl, true);
    write(pl, p);
  }
}

void install(const Config& cfg) {
  g_cfg = cfg;
  if (!cfg.script.empty()) {
    std::wstring name;
    for (char c : cfg.script) name += static_cast<wchar_t>(c);
    load_script(module_dir() + name);
  }
  static const uint8_t head[] = {0x8B, 0x4C, 0x24, 0x04};  // mov ecx, [esp+4]
  if (patch::bytes_are(0x004068F0, head, sizeof(head)))
    patch::hook("the keyboard's pad (mouse buttons as keys)", 0x004068F0, (void*)&hkKeyboardToPad, (void**)&oKeyboardToPad);
  else
    LOGW("input: the keyboard's pad function is not where it should be: no mouse buttons");
}

}  // namespace input
