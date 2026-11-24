#include "mod/config.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::string get(const std::wstring& path, const wchar_t* section, const wchar_t* key, const char* fallback) {
  wchar_t buf[512];
  DWORD n = GetPrivateProfileStringW(section, key, L"\x01", buf, 512, path.c_str());
  if (n == 1 && buf[0] == L'\x01') return fallback;
  std::string s;
  for (DWORD i = 0; i < n; ++i) s += static_cast<char>(buf[i] < 128 ? buf[i] : '?');
  // "value ; comment" -> "value"
  size_t c = s.find(';');
  if (c != std::string::npos) s.erase(c);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  for (char& ch : s)
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  return s;
}

int get_int(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
  std::string s = get(path, section, key, "");
  if (s.empty()) return fallback;
  return static_cast<int>(strtol(s.c_str(), nullptr, 0));
}

bool get_bool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool fallback) {
  std::string s = get(path, section, key, "");
  if (s.empty()) return fallback;
  return s == "1" || s == "true" || s == "yes" || s == "on";
}

// A value as its bytes are in the file, letters' case kept: a player's name may be Cyrillic. The launcher
// and a hand-edited file may differ in encoding: UTF-8 when the bytes are valid UTF-8, else Windows' own.
std::string get_text(const std::wstring& path, const char* section, const char* key, const char* fallback) {
  FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return fallback;
  std::string all;
  char buf[4096];
  for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) all.append(buf, n);
  fclose(f);
  if (all.size() >= 3 && static_cast<unsigned char>(all[0]) == 0xEF && static_cast<unsigned char>(all[1]) == 0xBB &&
      static_cast<unsigned char>(all[2]) == 0xBF)
    all.erase(0, 3);  // (a byte order mark)
  std::string now, value;
  bool found = false;
  for (size_t i = 0; i < all.size() && !found;) {
    size_t e = all.find(static_cast<char>(10), i);
    if (e == std::string::npos) e = all.size();
    std::string line = all.substr(i, e - i);
    i = e + 1;
    while (!line.empty() && (line.back() == 13 || line.back() == 32 || line.back() == 9)) line.pop_back();
    size_t a = 0;
    while (a < line.size() && (line[a] == 32 || line[a] == 9)) ++a;
    if (a >= line.size() || line[a] == ';' || line[a] == '#') continue;
    if (line[a] == '[') {
      size_t b = line.find(']', a);
      now = b == std::string::npos ? std::string() : line.substr(a + 1, b - a - 1);
      continue;
    }
    size_t eq = line.find('=', a);
    if (eq == std::string::npos || _stricmp(now.c_str(), section) != 0) continue;
    std::string k = line.substr(a, eq - a);
    while (!k.empty() && (k.back() == 32 || k.back() == 9)) k.pop_back();
    if (_stricmp(k.c_str(), key) != 0) continue;
    size_t v = eq + 1;
    while (v < line.size() && (line[v] == 32 || line[v] == 9)) ++v;
    value = line.substr(v);
    found = true;
  }
  if (!found || value.empty()) return fallback;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()), nullptr, 0) > 0) return value;
  // (not UTF-8: Windows' own code page, as a file edited by hand in Notepad would be)
  wchar_t wide[128];
  int n = MultiByteToWideChar(CP_ACP, 0, value.c_str(), static_cast<int>(value.size()), wide, 128);
  char utf8[400];
  int m = WideCharToMultiByte(CP_UTF8, 0, wide, n, utf8, sizeof(utf8), nullptr, nullptr);
  return std::string(utf8, static_cast<size_t>(m > 0 ? m : 0));
}

}  // namespace

std::wstring module_dir() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring s = path;
  size_t slash = s.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"" : s.substr(0, slash + 1);
}

bool load_config(const std::wstring& path, Config& cfg) {
  bool have = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
  cfg.log_level = tlog::parse_level(get(path, L"log", L"level", "info"), tlog::Info);
  std::string mode = get(path, L"video", L"mode", "borderless");
  cfg.mode = mode == "game" ? Config::Mode::Game : mode == "window" ? Config::Mode::Window : Config::Mode::Borderless;
  {
    std::string res = get(path, L"video", L"resolution", "auto");
    int w = 0, h = 0;
    if (sscanf(res.c_str(), "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240) cfg.res_w = w, cfg.res_h = h;
  }
  cfg.wide = get(path, L"video", L"aspect", "wide") != "4:3";
  cfg.menu_bars = get(path, L"video", L"menus", "wide") == "bars";
  cfg.window_w = get_int(path, L"video", L"window_width", 0);
  cfg.window_h = get_int(path, L"video", L"window_height", 0);
  cfg.frame_skip = get_bool(path, L"video", L"frame_skip", true);
  cfg.antialiasing = get_int(path, L"video", L"antialiasing", 4);
  cfg.anisotropy = get_int(path, L"video", L"anisotropy", 16);
  cfg.toon = get_int(path, L"video", L"toon_shading", -1);
  cfg.shadows = get_int(path, L"video", L"shadows", -1);
  cfg.particles = get_int(path, L"video", L"particles", -1);

  cfg.mouse = get_bool(path, L"input", L"mouse", true);
  cfg.keys_in_background = get_bool(path, L"debug", L"keys_in_background", false);

  std::string net = get(path, L"net", L"mode", "off");
  cfg.net_mode = net == "host" ? Config::Net::Host : net == "join" ? Config::Net::Join : Config::Net::Off;
  cfg.net_port = get_int(path, L"net", L"port", 47830);
  cfg.net_connect = get(path, L"net", L"connect", "127.0.0.1");
  cfg.net_name = get_text(path, "net", "name", "player");
  if (cfg.net_name.size() > 31) cfg.net_name.resize(31);
  cfg.net_delay = get_int(path, L"net", L"delay", 2);
  if (cfg.net_delay < 1) cfg.net_delay = 1;
  if (cfg.net_delay > 12) cfg.net_delay = 12;
  cfg.net_players = get_int(path, L"debug", L"net_players", 2);
  cfg.net_wait_s = get_int(path, L"net", L"wait", 120);
  cfg.net_transport = get(path, L"net", L"transport", "auto");
  cfg.net_lobby = get(path, L"net", L"lobby", "friends");
  cfg.mock_slot = get_int(path, L"debug", L"mock_slot", 1);
  cfg.mock_dir = get(path, L"debug", L"mock_dir", "");
  cfg.mock_loss_pct = get_int(path, L"debug", L"mock_loss", 0);
  cfg.mock_latency_ms = get_int(path, L"debug", L"mock_latency", 0);
  cfg.mock_member_delay_ms = get_int(path, L"debug", L"mock_member_delay", 0);
  cfg.net_loss = get_int(path, L"debug", L"net_loss", 0);
  {
    std::string t = get(path, L"debug", L"net_outage", "");
    float a = 0, b = 0;
    if (sscanf(t.c_str(), "%f,%f", &a, &b) == 2)
      cfg.net_outage_at_ms = static_cast<unsigned>(a * 1000), cfg.net_outage_ms = static_cast<unsigned>(b * 1000);
  }
  {
    std::string t = get(path, L"debug", L"net_stall", "");
    float a = 0, b = 0;
    if (sscanf(t.c_str(), "%f,%f", &a, &b) == 2)
      cfg.net_stall_at_ms = static_cast<unsigned>(a * 1000), cfg.net_stall_ms = static_cast<unsigned>(b * 1000);
  }
  cfg.net_outage_once = get_bool(path, L"debug", L"net_outage_once", false);
  cfg.net_quit_ms = static_cast<unsigned>(get_int(path, L"debug", L"net_quit", 0)) * 1000;

  cfg.autoquit_s = get_int(path, L"debug", L"autoquit", 0);
  cfg.autoquit_frame = static_cast<unsigned>(get_int(path, L"debug", L"autoquit_frame", 0));
  cfg.guard = get_bool(path, L"debug", L"guard", false);
  cfg.no_activate = get_bool(path, L"debug", L"no_activate", false);
  cfg.offscreen = get_bool(path, L"debug", L"offscreen", false);
  cfg.script = get(path, L"debug", L"script", "");
  {
    std::string t = get(path, L"debug", L"rng_trace", "");
    unsigned a = 0, b = 0;
    if (sscanf(t.c_str(), "%u-%u", &a, &b) == 2) cfg.rng_trace_from = a, cfg.rng_trace_to = b;
  }
  cfg.mute = get_bool(path, L"debug", L"mute", false);
  cfg.fast = get_bool(path, L"debug", L"fast", false);
  cfg.free_clock = get_bool(path, L"debug", L"free_clock", false);
  cfg.unlock = get_bool(path, L"debug", L"unlock", false);
  cfg.movies = get_bool(path, L"debug", L"movies", true);
  cfg.mode_log = get_bool(path, L"debug", L"mode_log", true);
  cfg.lab_draw_every = static_cast<unsigned>(get_int(path, L"debug", L"draw_every", 0));
  cfg.lab_story_open = get_bool(path, L"debug", L"story_open", false);
  cfg.lab_ko_enemies = static_cast<unsigned>(get_int(path, L"debug", L"ko_enemies", 0));
  cfg.lab_ko_hero = static_cast<unsigned>(get_int(path, L"debug", L"ko_hero", 0));
  cfg.lab_ko_partners = static_cast<unsigned>(get_int(path, L"debug", L"ko_partners", 0));
  cfg.lab_desync_at = static_cast<unsigned>(get_int(path, L"debug", L"desync_at", 0));
  cfg.lab_draw_cost_ms = static_cast<unsigned>(get_int(path, L"debug", L"draw_cost", 0));
  cfg.lab_epoch = get_bool(path, L"debug", L"epoch", false);
  cfg.lab_epoch_boot = get_bool(path, L"debug", L"epoch_boot", false);
  cfg.lab_notice = static_cast<unsigned>(get_int(path, L"debug", L"notice", 0));
  cfg.lab_page = get_bool(path, L"debug", L"page", false);
  cfg.lab_autoquit_players = get_int(path, L"debug", L"autoquit_players", 0);
  cfg.lab_epoch_again = static_cast<unsigned>(get_int(path, L"debug", L"epoch_again", 0));
  cfg.lab_no_rewind = get_bool(path, L"debug", L"no_rewind", false);
  cfg.lab_inplace = get_bool(path, L"debug", L"inplace", false);
  cfg.lab_address = get_bool(path, L"debug", L"address", false);
  cfg.lab_keep_modes = get_bool(path, L"debug", L"keep_modes", false);
  cfg.lab_epoch_at = static_cast<unsigned>(get_int(path, L"debug", L"epoch_at", 0));
  cfg.args = get(path, L"debug", L"args", "");
  cfg.sim_log = get_bool(path, L"debug", L"sim_log", false);
  cfg.shot_every = static_cast<unsigned>(get_int(path, L"debug", L"shot_every", 0));
  std::string shots = get(path, L"debug", L"shots", "");
  for (size_t i = 0; i < shots.size();) {
    size_t j = shots.find(',', i);
    if (j == std::string::npos) j = shots.size();
    unsigned v = static_cast<unsigned>(strtoul(shots.substr(i, j - i).c_str(), nullptr, 10));
    if (v) cfg.shots.push_back(v);
    i = j + 1;
  }
  return have;
}
