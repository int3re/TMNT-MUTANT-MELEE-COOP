#include "mod/menu.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "common/log.h"
#include "common/version.h"
#include "mod/digits.h"
#include "mod/game.h"
#include "mod/gametext.h"
#include "mod/netlobby.h"
#include "mod/netplay.h"
#include "mod/notice.h"
#include "mod/patch.h"
#include "mod/uiwide.h"
#include "mod/video.h"

namespace menu {
namespace {

using gametext::T;
using gametext::say;

Config g_cfg;
constexpr int kRows = 6;

// The mod's own strings (gametext's places): six rows and the small line at the bottom.
constexpr int kSlotRow = 0, kSlotStatus = 6;

// --- the game's file reading: one script is the mod's ---------------------------------------------
using Open_t = void*(__cdecl*)(const char*, const char*);
using Gets_t = void(__cdecl*)(char*, int, void*);
using Eof_t = int(__cdecl*)(void*);
using Close_t = void(__cdecl*)(void*);
Open_t oOpen = nullptr;
Gets_t oGets = nullptr;
Eof_t oEof = nullptr;
Close_t oClose = nullptr;

struct VFile {
  std::string text;
  size_t pos = 0;
  bool open = false;
} g_vfile;

std::string element(const char* name, const char* body) {
  return std::string("UI_Element ") + name + "\n{\n" + body + "}\n";
}

// Six buttons in the place of the game's five: the game's own picture and movement, rows 59 apart.
std::string six_buttons() {
  std::string out;
  char name[64], body[512];
  for (int i = 0; i < kRows; ++i) {
    int y = 372 - 59 * i;
    int delay = 140 + 12 * i, out_ms = 70 + 6 * i;
    snprintf(name, sizeof(name), "MnButton%d", i);
    snprintf(body, sizeof(body),
             "event = enter\ndelay = %d\nduration = exit\nasset_name = window01\nstart_xywh = 64, %d, 512, 56\n"
             "start_uvwh = 0, 0, 512, 64\nsafe_zone = 0\n",
             delay, y);
    out += element(name, body);
    snprintf(name, sizeof(name), "MnButton%dSlideIn", i);
    snprintf(body, sizeof(body),
             "event = enter\nduration = %d\nasset_name = window01\nstart_xywh = -564, %d, 512, 56\n"
             "end_xywh = 64, %d, 512, 56\nstart_uvwh = 0, 0, 512, 64\nsafe_zone = 0\n",
             delay, y, y);
    out += element(name, body);
    snprintf(name, sizeof(name), "MnButton%dSlideOut", i);
    snprintf(body, sizeof(body),
             "event = exit\nduration = %d\nasset_name = window01\nstart_xywh = 64, %d, 512, 56\n"
             "end_xywh = 764, %d, 512, 56\nstart_uvwh = 0, 0, 512, 64\nsafe_zone = 0\n",
             out_ms, y, y);
    out += element(name, body);
    const char* font = "font_size = 0.062\ndefault_font_color = 128,64,64,255\nselected_font_color = 255,128,128,255\n";
    int ty = y - 27;
    snprintf(name, sizeof(name), "MnText%d", i);
    snprintf(body, sizeof(body), "event = enter\ndelay = %d\nduration = exit\nasset_name = $%d\n%sstart_xywh = 100, %d, 440, 80\nsafe_zone = 0\n",
             delay, gametext::slot_id(kSlotRow + i), font, ty);
    out += element(name, body);
    snprintf(name, sizeof(name), "MnText%dSlideIn", i);
    snprintf(body, sizeof(body),
             "event = enter\nduration = %d\nasset_name = $%d\n%sstart_xywh = -540, %d, 440, 80\nend_xywh = 100, %d, 440, 80\nsafe_zone = 0\n",
             delay, gametext::slot_id(kSlotRow + i), font, ty, ty);
    out += element(name, body);
    snprintf(name, sizeof(name), "MnText%dSlideOut", i);
    snprintf(body, sizeof(body),
             "event = exit\nduration = %d\nasset_name = $%d\n%sstart_xywh = 100, %d, 440, 80\nend_xywh = 700, %d, 440, 80\nsafe_zone = 0\n",
             out_ms, gametext::slot_id(kSlotRow + i), font, ty, ty);
    out += element(name, body);
  }
  snprintf(body, sizeof(body), "asset_name = $%d\nfont_size = 0.035\nstart_xywh = 30, 25, 470, 32\n", gametext::slot_id(kSlotStatus));
  out += element("NetStatusText", body);
  // (the game's parser cuts a line's last word at a carriage return only: lines must end as the game's own do)
  std::string dos;
  for (char c : out) {
    if (c == 10) dos += static_cast<char>(13);
    dos += c;
  }
  return dos;
}

// The game's script with its five buttons taken out and the six put in.
std::string make_script(const std::string& original) {
  if (gametext::slot_id(kSlotStatus) < 0) return std::string();
  size_t a = original.find("UI_Element HC_");
  size_t b = original.find("UI_Element NextBackground");
  if (a == std::string::npos || b == std::string::npos || b < a) {
    LOGE("menu: the main menu's script is not as expected: the menu stays the game's own");
    return std::string();
  }
  return original.substr(0, a) + six_buttons() + original.substr(b);
}

bool g_script_ok = false;

// A script of the game's interface: "UI_<screen>.txt".
bool is_ui_script(const char* name) {
  size_t n = name ? strlen(name) : 0;
  return n > 7 && _strnicmp(name, "UI_", 3) == 0 && _stricmp(name + n - 4, ".txt") == 0;
}

// Every script of the interface goes to the game's parser through the mod: the main menu's gets its
// six buttons, and in a wide picture every screen is made as wide as the picture (uiwide.h).
void* __cdecl hkOpen(const char* name, const char* mode) {
  if (!is_ui_script(name) || g_vfile.open) return oOpen(name, mode);
  void* h = oOpen(name, mode);
  if (!h) return h;
  std::string original;
  char line[512];
  for (;;) {
    line[0] = 0;
    oGets(line, 511, h);
    if (!line[0] && oEof(h)) break;
    original += line;
    if (original.size() > 400000) break;
  }
  oClose(h);
  std::string text = original;
  bool main_menu = _stricmp(name, "UI_ModeSel_PC.txt") == 0;
  if (main_menu) {
    std::string six = make_script(original);
    if (!six.empty()) {
      text = six;
      g_script_ok = true;
      LOGI("menu: the main menu's script is the mod's (six buttons; %u bytes from the game's %u)", (unsigned)text.size(),
           (unsigned)original.size());
    }
  }
  float extra = video::menu_extra();
  if (extra >= 1.0f) {
    text = uiwide::widen(text, extra);
    static bool said = false;
    if (!said) LOGI("menu: the screens are made %.0f units wider on each side (a wide picture)", extra);
    said = true;
  }
  if (tlog::enabled(tlog::Debug)) {
    std::wstring file = module_dir() + L"meleenet-ui-";
    for (const char* c = name; *c; ++c) file += static_cast<wchar_t>(*c);
    if (FILE* f = _wfopen(file.c_str(), L"wb")) {
      fwrite(text.data(), 1, text.size(), f);
      fclose(f);
    }
  }
  g_vfile.text = text;
  g_vfile.pos = 0;
  g_vfile.open = true;
  return &g_vfile;
}

void __cdecl hkGets(char* buf, int n, void* h) {
  if (h != &g_vfile) {
    oGets(buf, n, h);
    return;
  }
  int i = 0;
  while (g_vfile.pos < g_vfile.text.size() && i < n - 1) {
    char c = g_vfile.text[g_vfile.pos++];
    buf[i++] = c;
    if (c == '\n') break;
  }
  buf[i] = 0;
}

int __cdecl hkEof(void* h) {
  if (h != &g_vfile) return oEof(h);
  return g_vfile.pos >= g_vfile.text.size() ? 1 : 0;
}

void __cdecl hkClose(void* h) {
  if (h != &g_vfile) {
    oClose(h);
    return;
  }
  g_vfile.open = false;
}

// --- the game's interface -------------------------------------------------------------------------------
void* ui() { return *reinterpret_cast<void**>(game::kUi); }
void* menu_list() { return *reinterpret_cast<uint8_t**>(game::kUiScripts) + game::kUiListMainMenu; }

void* find(const char* name) { return reinterpret_cast<void*(__cdecl*)(void*, const char*)>(game::kUiFind)(ui(), name); }
bool animating() { return reinterpret_cast<uint8_t(__cdecl*)(void*)>(game::kUiAnimating)(ui()) != 0; }
void enter_list() { reinterpret_cast<void(__cdecl*)(void*, void*)>(game::kUiEnter)(ui(), menu_list()); }
void exit_list() { reinterpret_cast<void(__cdecl*)(void*, void*)>(game::kUiExit)(ui(), menu_list()); }
bool pressed_down() { return reinterpret_cast<uint8_t(__cdecl*)()>(game::kMenuDown)() != 0; }
bool pressed_up() { return reinterpret_cast<uint8_t(__cdecl*)()>(game::kMenuUp)() != 0; }
bool pressed_back() { return reinterpret_cast<uint8_t(__cdecl*)()>(game::kMenuBack)() != 0; }
void sound_move() { reinterpret_cast<void(__cdecl*)()>(game::kSoundMenuMove)(); }
void sound_ok() { reinterpret_cast<void(__cdecl*)(int)>(game::kSoundMenuOk)(1); }

// The pad that has just pressed "confirm" (-1: none), as the game's own menu code tells it.
int confirm_pad() {
  for (int p = 0; p < game::kPlayers; ++p) {
    auto base = game::kPads + p * game::kPadSize;
    if (!*reinterpret_cast<const uint8_t*>(base + game::kPadConnected)) continue;
    auto now = reinterpret_cast<const float*>(base);
    auto before = reinterpret_cast<const float*>(base + game::kPadPrev);
    if ((now[game::kEnter] > 0.5f && before[game::kEnter] < 0.5f) || (now[game::kStart] > 0.5f && before[game::kStart] < 0.5f)) return p;
  }
  return -1;
}

// --- pages ---------------------------------------------------------------------------------------------------
enum class Page { Main, Net, Friends, Lobby };
enum class Act { None, Adventure, Melee, Library, Options, Quit, ToNet, ToMain, ToFriends, ToLobby };

struct Row {
  std::wstring text;
  bool on = true;  // can be chosen
};

Page g_page = Page::Main;
Row g_rows[kRows];
std::wstring g_status;
Act g_act = Act::None;
int g_act_pad = 0;
std::string g_address = "192.168.0.";
bool g_typing = false;
int g_quiet = 0;  // frames in which a confirm is not taken (the Enter that ended the typing)
uint64_t g_shown_games[4] = {};

std::wstring slot_text(const netlobby::View& v, int i) {
  // (Roman numbers: in the Russian release's font the digits 4 and 6 are letters)
  static const wchar_t* const roman[4] = {L"I. ", L"II. ", L"III. ", L"IV. "};
  static const wchar_t* const arabic[4] = {L"1. ", L"2. ", L"3. ", L"4. "};
  const wchar_t* num = digits::ready() || !gametext::russian() ? arabic[i] : roman[i];
  if (i < v.players) return say(std::wstring(num)) + say(v.names[i]);
  return say(std::wstring(num)) + T(L"СВОБОДНО", L"FREE");
}

void fill_rows() {
  const netlobby::View& v = netlobby::view();
  for (Row& r : g_rows) r = Row{};
  g_status.clear();
  switch (g_page) {
    case Page::Main:
      g_rows[0].text = gametext::game_string(game::kStrStory);
      g_rows[1].text = gametext::game_string(game::kStrMelee);
      if (netplay::active()) {
        wchar_t t[64];
        swprintf(t, 64, gametext::russian() ? L"В СЕТИ: ИГРОКОВ %d" : L"ONLINE: %d PLAYERS", netplay::players());
        g_rows[2].text = say(std::wstring(t));
        g_rows[2].on = false;
      } else {
        g_rows[2].text = T(L"СЕТЕВАЯ ИГРА", L"NETWORK GAME");
      }
      g_rows[3].text = gametext::game_string(game::kStrLibrary);
      g_rows[4].text = gametext::game_string(game::kStrOptions);
      g_rows[5].text = gametext::game_string(game::kStrQuit);
      g_status = say(std::string("MELEENET " MELEENET_VERSION));
      break;
    case Page::Net:
      g_rows[0].text = T(L"СОЗДАТЬ ИГРУ", L"OPEN A GAME");
      g_rows[1].text = T(L"ИГРЫ ДРУЗЕЙ В STEAM", L"FRIENDS' GAMES ON STEAM");
      g_rows[1].on = v.steam;
      g_rows[2].text = T(L"ВОЙТИ ПО АДРЕСУ", L"JOIN BY ADDRESS");
      g_rows[3].text = T(L"АДРЕС: ", L"ADDRESS: ") + say(g_address) + (g_typing ? L"-" : L"");
      g_rows[4].text = v.steam ? T(L"STEAM ПОДКЛЮЧЕН", L"STEAM IS UP") : T(L"STEAM НЕ ЗАПУЩЕН", L"STEAM IS NOT RUNNING");
      g_rows[4].on = false;
      g_rows[5].text = T(L"НАЗАД", L"BACK");
      if (!v.error.empty())
        g_status = say(v.error);
      else if (g_typing)
        g_status = T(L"ЦИФРЫ И ТОЧКИ, ЗАТЕМ ВВОД. CTRL+V - ВСТАВИТЬ", L"DIGITS AND DOTS, THEN ENTER. CTRL+V PASTES");
      else if (v.state == netlobby::State::Joining)
        g_status = T(L"ПОДКЛЮЧЕНИЕ К ХОСТУ...", L"CONNECTING TO THE HOST...");
      break;
    case Page::Friends: {
      int n = 0;
      for (const netlobby::Game& g : v.games) {
        if (n >= 4) break;
        g_rows[n].text = say(g.host) + (g.why.empty() ? L"" : L" - " + say(g.why));
        g_rows[n].on = g.ok;
        g_shown_games[n] = g.lobby;
        ++n;
      }
      for (int i = n; i < 4; ++i) {
        g_rows[i].text = i == 0 ? T(L"ИГР ДРУЗЕЙ ПОКА НЕТ", L"NO FRIENDS' GAMES YET") : L"";
        g_rows[i].on = false;
        g_shown_games[i] = 0;
      }
      g_rows[4].text = T(L"ИДЕТ ПОИСК...", L"SEARCHING...");
      g_rows[4].on = false;
      g_rows[5].text = T(L"НАЗАД", L"BACK");
      if (!v.error.empty()) g_status = say(v.error);
      break;
    }
    case Page::Lobby: {
      bool host = v.state == netlobby::State::Hosting || (v.state == netlobby::State::Starting && v.me == 0);
      for (int i = 0; i < 4; ++i) {
        g_rows[i].text = slot_text(v, i);
        g_rows[i].on = false;
      }
      if (v.state == netlobby::State::Starting) {
        g_rows[4].text = T(L"ЗАПУСК ИГРЫ...", L"STARTING...");
        g_rows[4].on = false;
        g_rows[5].on = false;
      } else if (host) {
        g_rows[4].text = v.players >= 2 ? T(L"НАЧАТЬ ИГРУ", L"START THE GAME") : T(L"ЖДЕМ ИГРОКОВ", L"WAITING FOR PLAYERS");
        g_rows[4].on = v.players >= 2;
      } else {
        g_rows[4].text = v.playing ? T(L"ВОЙДЕТЕ ПОСЛЕ БОЯ", L"IN AFTER THE FIGHT") : T(L"ЖДЕМ ХОСТА", L"WAITING FOR THE HOST");
        g_rows[4].on = false;
        if (v.playing) g_status = T(L"У ХОСТА ИДЕТ ИГРА: МЕЖДУ БОЯМИ ОНА НАЧНЕТСЯ ЗАНОВО, УЖЕ С ВАМИ", L"THE HOST IS PLAYING: BETWEEN TWO FIGHTS THE GAME STARTS AGAIN WITH YOU IN");
      }
      g_rows[5].text = host ? T(L"ЗАКРЫТЬ ИГРУ", L"CLOSE THE GAME") : T(L"ВЫЙТИ", L"LEAVE");
      if (host && !v.addresses.empty())
        g_status = T(L"АДРЕС ДЛЯ ДРУЗЕЙ: ", L"ADDRESS FOR FRIENDS: ") + say(v.addresses) +
                   (v.steam ? T(L"  ИЛИ STEAM", L"  OR STEAM") : L"");
      else if (host && v.steam)
        g_status = T(L"ДРУЗЬЯ ВИДЯТ ИГРУ В STEAM", L"FRIENDS SEE THE GAME ON STEAM");
      break;
    }
  }
  for (int i = 0; i < kRows; ++i) gametext::set_slot(kSlotRow + i, g_rows[i].text);
  gametext::set_slot(kSlotStatus, g_status);
}

int step(int cursor, int dir) {
  for (int k = 0; k < kRows; ++k) {
    cursor = (cursor + dir + kRows) % kRows;
    if (g_rows[cursor].on) return cursor;
  }
  return cursor;
}

int first_on() {
  for (int i = 0; i < kRows; ++i)
    if (g_rows[i].on) return i;
  return kRows - 1;
}

// The page the lobby's state belongs on (a host that closed, a start, a refusal move the page by themselves).
void follow_lobby(uint8_t& leaving) {
  if (leaving || g_page == Page::Main) return;
  netlobby::State s = netlobby::view().state;
  bool in_lobby = s == netlobby::State::Hosting || s == netlobby::State::Joined || s == netlobby::State::Starting;
  if (in_lobby && g_page != Page::Lobby) {
    g_act = Act::ToLobby;
  } else if (!in_lobby && g_page == Page::Lobby) {
    g_act = Act::ToNet;
  } else {
    return;
  }
  exit_list();
  leaving = 1;
}

using Update_t = void(__thiscall*)(void*);
Update_t oUpdate = nullptr;

// In place of the main menu mode's Update (0x44A660).
void __fastcall hkUpdate(void* self) {
  if (!g_script_ok) {
    oUpdate(self);
    return;
  }
  auto& leaving = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(self) + 0x18);
  auto& cursor = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(self) + 0x1C);
  if (g_page != Page::Main) netlobby::tick();
  fill_rows();
  if (cursor < 0 || cursor >= kRows) cursor = 0;
  if (!g_rows[cursor].on) cursor = first_on();
  // The lobby's НАЧАТЬ ИГРУ has just become possible (a second player is in): the cursor goes to it.
  {
    static bool could = false;
    bool can = g_page == Page::Lobby && g_rows[4].on;
    if (can && !could) cursor = 4;
    could = can;
  }
  if (g_quiet > 0) --g_quiet;
  follow_lobby(leaving);

  if (!leaving && !g_typing) {
    if (pressed_down()) {
      cursor = step(cursor, 1);
      sound_move();
    }
    if (pressed_up()) {
      cursor = step(cursor, -1);
      sound_move();
    }
    int pad = g_quiet ? -1 : confirm_pad();
    Act act = Act::None;
    if (pad >= 0 && g_rows[cursor].on) {
      switch (g_page) {
        case Page::Main:
          act = cursor == 0 ? Act::Adventure : cursor == 1 ? Act::Melee : cursor == 2 ? Act::ToNet : cursor == 3 ? Act::Library
                : cursor == 4 ? Act::Options : Act::Quit;
          break;
        case Page::Net:
          if (cursor == 0) {
            netlobby::host();
          } else if (cursor == 1) {
            act = Act::ToFriends;
          } else if (cursor == 2) {
            netlobby::join_address(g_address);
          } else if (cursor == 3) {
            g_typing = true;
          } else if (cursor == 5) {
            act = Act::ToMain;
          }
          if (cursor != 5 && cursor != 1) sound_ok();
          break;
        case Page::Friends:
          if (cursor < 4 && g_shown_games[cursor]) {
            netlobby::join_game(g_shown_games[cursor]);
            sound_ok();
          } else if (cursor == 5) {
            act = Act::ToNet;
          }
          break;
        case Page::Lobby:
          if (cursor == 4) {
            netlobby::start();
            sound_ok();
          } else if (cursor == 5) {
            netlobby::leave();
            sound_ok();
          }
          break;
      }
    } else if (g_page != Page::Main && pressed_back()) {
      if (g_page == Page::Lobby) {
        netlobby::leave();
      } else {
        act = g_page == Page::Friends ? Act::ToNet : Act::ToMain;
        pad = 0;
      }
    }
    if (act != Act::None) {
      g_act = act;
      g_act_pad = pad;
      if (g_page == Page::Main && act != Act::ToNet) {
        // (as the game's own code does on a choice)
        *reinterpret_cast<int*>(game::kPrimaryPad) = pad;
        reinterpret_cast<void(__thiscall*)(void*)>(game::kPlayerSelectReset)(*reinterpret_cast<void**>(game::kModePlayerSelect));
      }
      exit_list();
      leaving = 1;
      sound_ok();
    }
  }

  // The chosen row's words are bright; the other rows' buttons are dimmed (the game's own two marks).
  char name[64];
  static const char* const kinds[3] = {"", "SlideIn", "SlideOut"};
  for (int i = 0; i < kRows; ++i)
    for (const char* kind : kinds) {
      if (i == cursor && g_rows[i].on) {
        snprintf(name, sizeof(name), "MnText%d%s", i, kind);
        if (void* e = find(name)) *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(e) + 0x30) = 1;
      } else {
        snprintf(name, sizeof(name), "MnButton%d%s", i, kind);
        if (void* e = find(name)) *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(e) + 0x31) = 1;
      }
    }

  if (tlog::enabled(tlog::Debug)) {
    static int said = 0;
    if (said++ % 120 == 0)
      LOGD("menu: page %d cursor %d leaving %d; button0 %p text0 %p; row0 '%ls' row2 '%ls'", (int)g_page, cursor, (int)leaving,
           find("MnButton0"), find("MnText0"), g_rows[0].text.c_str(), g_rows[2].text.c_str());
  }
  if (animating() || !leaving) return;
  // The buttons have slid out: what was chosen happens.
  auto next = [](uintptr_t mode_slot) { *reinterpret_cast<void**>(game::kNextMode) = *reinterpret_cast<void**>(mode_slot); };
  auto player0 = *reinterpret_cast<uint8_t**>(game::kPlayerObjects);
  Act act = g_act;
  g_act = Act::None;
  switch (act) {
    case Act::Adventure:
      next(game::kModeAdventureSelect);
      *reinterpret_cast<int*>(game::kPlayerCount) = 1;
      if (player0) player0[0x10B] = *reinterpret_cast<uint8_t*>(game::kPrimaryPad);
      return;
    case Act::Melee:
      next(game::kModePlayerSelect);
      return;
    case Act::Library:
      next(game::kModeLibrary);
      return;
    case Act::Options:
      next(game::kModeOptions);
      if (player0) player0[0x10B] = *reinterpret_cast<uint8_t*>(game::kPrimaryPad);
      return;
    case Act::Quit:
      *reinterpret_cast<uint8_t*>(game::kQuit) = 1;
      return;
    case Act::ToNet:
      if (g_page == Page::Main) netlobby::open();
      g_page = Page::Net;
      break;
    case Act::ToFriends:
      g_page = Page::Friends;
      break;
    case Act::ToLobby:
      g_page = Page::Lobby;
      break;
    case Act::ToMain:
      netlobby::close();
      g_page = Page::Main;
      break;
    default:
      return;
  }
  // Another page: the same buttons slide in with its words.
  g_typing = false;
  fill_rows();
  cursor = g_page == Page::Main ? 2 : first_on();
  leaving = 0;
  enter_list();
}

// --- notices: the game's own text drawing, after the mode has drawn (0x4037E0) -------------------------
using Void_t = void(__cdecl*)();
Void_t oOverlay = nullptr;

void __cdecl hkOverlay() {
  oOverlay();
  const wchar_t* text = notice::current();
  if (!text || !*text) return;
  std::wstring shown = say(std::wstring(text));
  int lines = 1;
  for (wchar_t c : shown) lines += c == L'\n';
  // The top of the picture (y is counted from the bottom), white letters with the game's own shadow.
  float h = 0.05f * lines;
  float rect[4] = {0.08f, 0.985f - h, 0.84f, h};
  uint32_t white = 0xFFFFFFFF;
  reinterpret_cast<void(__cdecl*)(int)>(game::kDraw2dBegin)(1);
  void* brush = *reinterpret_cast<void**>(game::kBrushWhite);
  reinterpret_cast<void(__cdecl*)(void*, uint32_t*, uint32_t*, uint32_t*, uint32_t*)>(game::kBrushColors)(brush, &white, &white, &white,
                                                                                                         &white);
  reinterpret_cast<void(__cdecl*)(void*, const wchar_t*, float, float*, int, void*, int, int)>(game::kDrawText)(
      *reinterpret_cast<void**>(game::kFont), shown.c_str(), 0.04f, rect, 1, brush, 1, 0xFF);
  // (the font's texture is left set: what the game draws next without a texture of its own - the bars at
  // the sides of a menu - would be drawn with a letter's empty corner)
  reinterpret_cast<void(__cdecl*)(int, void*)>(game::kRwRenderStateSet)(1, nullptr);
  reinterpret_cast<void(__cdecl*)()>(game::kDraw2dEnd)();
}

// The language table is loaded (0x438510): the mod's strings can be set.
Void_t oLoadLanguage = nullptr;
void __cdecl hkLoadLanguage() {
  oLoadLanguage();
  gametext::init();
  digits::on_fonts_loaded();
}

std::string clipboard_text() {
  std::string out;
  if (!OpenClipboard(nullptr)) return out;
  if (HANDLE h = GetClipboardData(CF_TEXT)) {
    if (auto p = static_cast<const char*>(GlobalLock(h))) {
      out = p;
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return out;
}

void type(char c) {
  if (!((c >= '0' && c <= '9') || c == '.' || c == ':' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-')) return;
  if (g_address.size() < 40) g_address += c;
}

}  // namespace

bool on_key(unsigned vk) {
  if (!g_typing) return false;
  if (vk == VK_RETURN || vk == VK_ESCAPE) {
    g_typing = false;
    g_quiet = 12;
  } else if (vk == VK_BACK) {
    if (!g_address.empty()) g_address.pop_back();
  } else if (vk == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
    g_address.clear();
    for (char c : clipboard_text()) type(c);
  }
  return true;
}

bool on_char(wchar_t ch) {
  if (!g_typing) return false;
  if (ch < 128) type(static_cast<char>(ch));
  return true;
}

void install(const Config& cfg) {
  g_cfg = cfg;
  if (!cfg.net_connect.empty() && cfg.net_connect != "127.0.0.1") g_address = cfg.net_connect;
  if (cfg.lab_address) g_address = cfg.net_connect;
  static const uint8_t open_head[] = {0xA0, 0x7C, 0x53, 0x58, 0x00};
  static const uint8_t gets_head[] = {0x56, 0x8B, 0x74, 0x24, 0x10};
  static const uint8_t eof_head[] = {0x56, 0x8B, 0x74, 0x24, 0x08};
  static const uint8_t update_head[] = {0x57, 0x8B, 0xF9, 0x8A, 0x47, 0x18};
  static const uint8_t overlay_head[] = {0x81, 0xEC, 0xF4, 0x00, 0x00, 0x00};
  if (!patch::bytes_are(game::kFileOpen, open_head, sizeof(open_head)) || !patch::bytes_are(game::kFileGets, gets_head, sizeof(gets_head)) ||
      !patch::bytes_are(game::kFileEof, eof_head, sizeof(eof_head)) || !patch::bytes_are(game::kFileClose, eof_head, sizeof(eof_head)) ||
      !patch::bytes_are(game::kMainMenuUpdate, update_head, sizeof(update_head)) ||
      !patch::bytes_are(game::kDrawOverlay, overlay_head, sizeof(overlay_head))) {
    LOGE("menu: the game's menu functions are not where they should be: no network page, no notices");
    return;
  }
  patch::hook("the game's language table", game::kLoadLanguage, (void*)&hkLoadLanguage, (void**)&oLoadLanguage);
  patch::hook("the game's file open", game::kFileOpen, (void*)&hkOpen, (void**)&oOpen);
  patch::hook("the game's file line", game::kFileGets, (void*)&hkGets, (void**)&oGets);
  patch::hook("the game's file end", game::kFileEof, (void*)&hkEof, (void**)&oEof);
  patch::hook("the game's file close", game::kFileClose, (void*)&hkClose, (void**)&oClose);
  patch::hook("the main menu", game::kMainMenuUpdate, (void*)&hkUpdate, (void**)&oUpdate);
  patch::hook("the game's overlay (notices)", game::kDrawOverlay, (void*)&hkOverlay, (void**)&oOverlay);
}

}  // namespace menu
