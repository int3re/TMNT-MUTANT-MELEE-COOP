// The game's settings the launcher's pages edit: see gameset.h.
#include "launcher/gameset.h"

#include <windows.h>

#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <utility>
#include <vector>

#include "launcher/launcher.h"

namespace lz {
namespace gset {
namespace {

const wchar_t kIni[] = L"meleenet.ini";

struct Choice {
  const wchar_t* name;
  const wchar_t* value;  // as meleenet.ini has it
};
struct Info {
  const wchar_t* label;
  const char* key;
  Kind kind;
  const wchar_t* section;
  const wchar_t* ini;
  const wchar_t* fallback;  // the value when the ini has none: the mod's default
  std::vector<Choice> choices;
  const wchar_t* since = nullptr;  // the first version of the mod that reads the key (null: every one)
};
const Info kInfo[kRows] = {
    {L"Окно игры", "mode", Kind::Choice, L"video", L"mode", L"borderless",
     {{L"На весь монитор", L"borderless"}, {L"В окне", L"window"}}},
    {L"Разрешение", "resolution", Kind::Choice, L"video", L"resolution", L"auto", {}},
    {L"Формат кадра", "aspect", Kind::Choice, L"video", L"aspect", L"wide",
     {{L"Под монитор", L"wide"}, {L"4:3 с полосами", L"4:3"}}},
    {L"Меню", "menus", Kind::Choice, L"video", L"menus", L"wide",
     {{L"На весь кадр", L"wide"}, {L"4:3 с полосами", L"bars"}}, L"0.1.1"},
    {L"Сглаживание", "antialiasing", Kind::Choice, L"video", L"antialiasing", L"4",
     {{L"Выкл", L"0"}, {L"2x", L"2"}, {L"4x", L"4"}, {L"8x", L"8"}}},
    {L"Фильтр текстур", "anisotropy", Kind::Choice, L"video", L"anisotropy", L"16",
     {{L"Выкл", L"0"}, {L"4x", L"4"}, {L"8x", L"8"}, {L"16x", L"16"}}},
    {L"Контуры героев", "toon", Kind::Pair, L"video", L"toon_shading", L"1", {{L"Рисованные", L"1"}, {L"Выкл", L"0"}}},
    {L"Тени", "shadows", Kind::Pair, L"video", L"shadows", L"1", {{L"Живые", L"1"}, {L"Простые", L"0"}}},
    {L"Частицы", "particles", Kind::Choice, L"video", L"particles", L"100",
     {{L"Все", L"100"}, {L"Меньше", L"70"}, {L"Мало", L"40"}}},
    {L"Задержка ввода", "delay", Kind::Choice, L"net", L"delay", L"2",
     {{L"1 кадр", L"1"}, {L"2 кадра", L"2"}, {L"3 кадра", L"3"}, {L"4 кадра", L"4"}, {L"5 кадров", L"5"}, {L"6 кадров", L"6"}}},
    {L"Видят в Steam", "lobby", Kind::Pair, L"net", L"lobby", L"friends", {{L"Друзья", L"friends"}, {L"Все", L"public"}}},
    {L"Кнопки мыши в игре", "mouse", Kind::Pair, L"input", L"mouse", L"1", {{L"Работают", L"1"}, {L"Выкл", L"0"}}},
};

// The bindings' actions (the page's order) and their places in a record of tmntwire.hks (a record is
// twelve numbers: pad, device, up, down, left, right, attack, alt attack, action, jump, special, block).
constexpr int kSlots = kKeyActions;
const struct {
  const wchar_t* label;
  const char* key;
  int slot;
} kActions[kKeyActions] = {
    {L"Вверх", "up", 0},        {L"Вниз", "down", 1},          {L"Влево", "left", 2},       {L"Вправо", "right", 3},
    {L"Прыжок", "jump", 7},     {L"Атака", "attack", 4},       {L"Вторая атака", "alt", 5}, {L"Действие", "action", 6},
    {L"Спецприём", "special", 8}, {L"Блок", "block", 9},
};
// The ready layouts: the game's own (W S A D, N M H J, Space, B), and WASD with the mouse.
constexpr int kLayouts[2][kSlots] = {{0x11, 0x1F, 0x1E, 0x20, 0x31, 0x32, 0x23, 0x24, 0x39, 0x30},
                                     {0x11, 0x1F, 0x1E, 0x20, kMouseLeft, kMouseRight, 0x12, 0x39, 0x10, 0x2A}};
// Keys an action can have: all but Esc, Enter, F1 and F11, which the game keeps for itself.
constexpr unsigned char kAllowed[] = {
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3C, 0x3D,
    0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52,
    0x53, 0x58, 0x9C, 0x9D, 0xB5, 0xB8, 0xC7, 0xC8, 0xC9, 0xCB, 0xCD, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3};
const struct {
  const wchar_t* label;
  const wchar_t* keys;
} kHalves[kHalfRows] = {};

struct Res {
  std::wstring name, value;
};

struct State {
  std::wstring dir, ini;  // the folder; meleenet.ini where the game reads it
  bool mod = false, canned = false;
  std::wstring version;  // of the installed mod (empty: not known)
  int cur[kRows] = {};
  std::vector<Res> res;
  int key[kSlots];
  bool own = false;  // own_key: the player's own keys, kept while a ready layout is on
  int own_key[kSlots];
  int players = 1;
  std::wstring error;
  bool kept = false;  // a copy of the ini was put aside before this run's first change
  bool kept_hks = false;
};
State g;

std::wstring env(const wchar_t* name) {
  wchar_t b[MAX_PATH * 2];
  const DWORD n = GetEnvironmentVariableW(name, b, MAX_PATH * 2);
  return n && n < MAX_PATH * 2 ? std::wstring(b, n) : std::wstring();
}

// --- the game's own view of its folder ---------------------------------------------------------------
// ztmnt.exe has no manifest, so in a folder its user may not write to (Program Files) Windows gives it
// copies of the files it writes, under <LocalAppData>\VirtualStore\<the path without its drive>, and
// shows it those copies from then on. The launcher reads and writes the file the game sees.

std::wstring store_root() {
  const std::wstring lab = env(L"MELEENET_LAUNCHER_VSTORE");
  if (!lab.empty()) return lab;
  PWSTR local = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) dir = local;
  CoTaskMemFree(local);
  return dir.empty() ? dir : core::join(dir, L"VirtualStore");
}

std::wstring stored(const std::wstring& dir) {
  const std::wstring root = store_root();
  if (root.empty() || dir.size() < 3 || dir[1] != L':') return L"";
  return core::join(core::join(root, dir.substr(3)), kIni);
}

bool is_protected(const std::wstring& dir) {
  if (!env(L"MELEENET_LAUNCHER_VSTORE").empty()) return true;  // (lab: any folder)
  for (const wchar_t* name : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432", L"ProgramData", L"SystemRoot"}) {
    const std::wstring p = env(name);
    if (p.empty() || dir.size() <= p.size() || _wcsnicmp(dir.c_str(), p.c_str(), p.size()) != 0) continue;
    if (dir[p.size()] == L'\\' || dir[p.size()] == L'/') return true;
  }
  return false;
}

bool writable(const std::wstring& file) {
  HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  CloseHandle(f);
  return true;
}

std::wstring seen(const std::wstring& dir) {
  const std::wstring copy = stored(dir);
  return !copy.empty() && core::exists(copy) ? copy : core::join(dir, kIni);
}

// Before a change: where it goes, and a copy of the file as it was (once a run).
bool ready() {
  const std::wstring own = core::join(g.dir, kIni);
  if (g.ini == own && is_protected(g.dir) && (!env(L"MELEENET_LAUNCHER_VSTORE").empty() || !writable(own))) {
    // The folder is closed to this user: the change goes where Windows would put the game's own.
    const std::wstring copy = stored(g.dir);
    const size_t slash = copy.find_last_of(L'\\');
    const wchar_t* closed = L"Папка игры закрыта для записи: запустите лаунчер от имени администратора.";
    if (copy.empty() || slash == std::wstring::npos) {
      g.error = closed;
      return false;
    }
    SHCreateDirectoryExW(nullptr, copy.substr(0, slash).c_str(), nullptr);
    if (!CopyFileW(own.c_str(), copy.c_str(), TRUE)) {
      g.error = closed;
      return false;
    }
    g.ini = copy;
  }
  if (!g.kept) {
    g.kept = true;
    CopyFileW(g.ini.c_str(), core::join(core::downloads_dir(), std::wstring(kIni) + L".before-launcher").c_str(), FALSE);
  }
  return true;
}

// --- the ini ---------------------------------------------------------------------------------------------

std::wstring get(const wchar_t* section, const wchar_t* key) {
  wchar_t buf[512] = {};
  GetPrivateProfileStringW(section, key, L"", buf, 512, g.ini.c_str());
  std::wstring v(buf);
  const size_t semi = v.find(L';');  // (an inline comment)
  if (semi != std::wstring::npos) v.resize(semi);
  while (!v.empty() && iswspace(v.back())) v.pop_back();
  size_t b = 0;
  while (b < v.size() && iswspace(v[b])) ++b;
  v = v.substr(b);
  for (auto& c : v) c = towlower(c);
  return v;
}

bool put(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
  if (g.canned) return true;
  if (!ready()) return false;
  if (WritePrivateProfileStringW(section, key, value.c_str(), g.ini.c_str())) return true;
  g.error = L"meleenet.ini не записался (ошибка " + std::to_wstring(GetLastError()) + L").";
  return false;
}

bool is_number(const std::wstring& v) { return !v.empty() && (iswdigit(v[0]) || v[0] == L'-' || v[0] == L'.'); }

// The choice a value of the ini means: the one that has it, the nearest for a number between the
// choices (an ini written by hand), else the mod's default.
int index_of(const Info& info, std::wstring v) {
  if (std::wstring(info.ini) == L"mode" && v == L"windowed") v = L"window";
  if (std::wstring(info.ini) == L"toon_shading" || std::wstring(info.ini) == L"shadows" || std::wstring(info.ini) == L"particles")
    if (v == L"-1") v = info.fallback;
  if (v.empty()) v = info.fallback;
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t i = 0; i < info.choices.size(); ++i)
      if (v == info.choices[i].value) return static_cast<int>(i);
    if (is_number(v) && is_number(info.choices[0].value)) {
      const double want = _wtof(v.c_str());
      int at = 0;
      for (size_t i = 1; i < info.choices.size(); ++i)
        if (std::fabs(_wtof(info.choices[i].value) - want) < std::fabs(_wtof(info.choices[at].value) - want)) at = static_cast<int>(i);
      return at;
    }
    v = info.fallback;  // (a word the mod does not know: it plays with its default)
  }
  return 0;
}

// РАЗРЕШЕНИЕ: the monitor's modes after "as the monitor", and what the file has if it is none of them.
void make_resolutions(const std::wstring& now) {
  std::vector<std::pair<int, int>> sizes;
  if (g.canned) {
    sizes = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
  } else {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i)
      if (dm.dmBitsPerPel == 32 && dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480)
        sizes.push_back({static_cast<int>(dm.dmPelsWidth), static_cast<int>(dm.dmPelsHeight)});
  }
  int w = 0, h = 0;
  const bool own = swscanf_s(now.c_str(), L"%dx%d", &w, &h) == 2 && w >= 320 && h >= 240 && w <= 16384 && h <= 16384;
  if (own) sizes.push_back({w, h});
  std::sort(sizes.begin(), sizes.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
    return a.first * a.second < b.first * b.second || (a.first * a.second == b.first * b.second && a.first < b.first);
  });
  sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
  g.res.clear();
  g.res.push_back({L"Как монитор", L"auto"});
  g.cur[kResolution] = 0;
  for (const auto& s : sizes) {
    if (own && s.first == w && s.second == h) g.cur[kResolution] = static_cast<int>(g.res.size());
    g.res.push_back({std::to_wstring(s.first) + L" × " + std::to_wstring(s.second),
                     std::to_wstring(s.first) + L"x" + std::to_wstring(s.second)});
  }
}

std::wstring key_list(const int keys[kSlots]) {
  std::wstring out;
  for (int i = 0; i < kSlots; ++i) out += (i ? L"," : L"") + std::to_wstring(keys[i]);
  return out;
}

// --- tmntwire.hks: 100 records of twelve numbers; the first with pad < 4 and device 0 is the keyboard's ----
constexpr int kRecord = 0x30, kRecords = 100;

std::wstring hks_file() {
  // (the game reads it from its own folder; a copy Windows made for it stands in, as for the ini)
  const std::wstring own = core::join(g.dir, L"tmntwire.hks");
  if (g.ini.size() > wcslen(kIni) && _wcsicmp(g.ini.c_str(), core::join(g.dir, kIni).c_str()) != 0) {
    const std::wstring copy = g.ini.substr(0, g.ini.size() - wcslen(kIni)) + L"tmntwire.hks";
    if (core::exists(copy)) return copy;
  }
  return own;
}

std::vector<uint32_t> read_hks() {
  std::vector<uint32_t> v;
  HANDLE f = CreateFileW(hks_file().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return v;
  v.resize(kRecords * 12);
  DWORD got = 0;
  if (!ReadFile(f, v.data(), kRecords * kRecord, &got, nullptr) || got < kRecord) v.clear();
  CloseHandle(f);
  // (what a short file does not have reads as records of no player)
  if (!v.empty())
    for (DWORD r = got / kRecord; r < kRecords; ++r) v[r * 12] = v[r * 12 + 1] = 0x400;
  return v;
}

int keyboard_record(const std::vector<uint32_t>& v) {
  for (int r = 0; r < kRecords && static_cast<size_t>(r * 12 + 11) < v.size(); ++r)
    if (v[r * 12] < 4 && v[r * 12 + 1] == 0) return r;
  return -1;
}

bool write_hks() {
  if (g.canned) return true;
  if (!ready()) return false;
  std::vector<uint32_t> v = read_hks();
  if (v.empty()) {
    v.assign(kRecords * 12, 0);
    for (int r = 0; r < kRecords; ++r) v[r * 12] = v[r * 12 + 1] = 0x400;  // records of no player
  }
  int rec = keyboard_record(v);
  if (rec < 0) {
    // no keyboard among the bindings: it takes the first record that is nobody's
    for (int r = 0; r < kRecords && rec < 0; ++r)
      if (v[r * 12] >= 4) rec = r;
    if (rec < 0) rec = 0;
    v[rec * 12] = 0;
    v[rec * 12 + 1] = 0;
  }
  for (int i = 0; i < kSlots; ++i) v[rec * 12 + 2 + i] = static_cast<uint32_t>(g.key[i]);
  const std::wstring path = hks_file();
  if (!g.kept_hks) {
    g.kept_hks = true;
    CopyFileW(path.c_str(), core::join(core::downloads_dir(), L"tmntwire.hks.before-launcher").c_str(), FALSE);
  }
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  DWORD put_n = 0;
  const bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, v.data(), kRecords * kRecord, &put_n, nullptr) && put_n == kRecords * kRecord;
  if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  if (!ok) g.error = L"tmntwire.hks не записался (ошибка " + std::to_wstring(GetLastError()) + L").";
  return ok;
}

bool parse_keys(const std::wstring& text, int out[kSlots]) {
  int n = 0;
  const wchar_t* p = text.c_str();
  while (*p && n < kSlots) {
    wchar_t* end = nullptr;
    const long v = wcstol(p, &end, 10);
    if (end == p) return false;
    if (v <= 0 || v > 255) return false;
    out[n++] = static_cast<int>(v);
    p = end;
    while (*p == L',' || *p == L' ') ++p;
  }
  return n == kSlots;
}

bool is_layout(int which) { return std::memcmp(g.key, kLayouts[which], sizeof(g.key)) == 0; }

void read_file() {
  g.mod = g.canned || (!g.dir.empty() && core::exists(g.ini));
  for (int r = 0; r < kRows; ++r) {
    const Info& info = kInfo[r];
    const std::wstring v = g.mod && !g.canned ? get(info.section, info.ini) : std::wstring();
    if (r == kResolution) {
      make_resolutions(v);
    } else if (info.kind == Kind::Level) {
      g.cur[r] = std::clamp(is_number(v) ? _wtoi(v.c_str()) : _wtoi(info.fallback), 0, kLevelMax);
    } else {
      g.cur[r] = index_of(info, v);
    }
  }
  std::memcpy(g.key, kLayouts[0], sizeof(g.key));
  g.players = 1;
  if (g.mod && !g.canned) {
    const std::vector<uint32_t> v = read_hks();
    const int rec = keyboard_record(v);
    if (rec >= 0)
      for (int i = 0; i < kSlots; ++i) {
        const uint32_t code = v[rec * 12 + 2 + i];
        if (code > 0 && code < 256) g.key[i] = static_cast<int>(code);
      }
    int k[kSlots];
    if (parse_keys(get(L"input", L"keys_own"), k)) {
      std::memcpy(g.own_key, k, sizeof(g.own_key));
      g.own = true;
    }
  }
}

bool failed() {  // (the page shows what the file has)
  if (g.canned) return false;
  const std::wstring why = g.error;
  read_file();
  g.error = why;
  return false;
}

bool put_keys() { return write_hks() || failed(); }

}  // namespace

void load(const std::wstring& game_dir, const std::wstring& mod_version) {
  if (g.dir != game_dir) g.kept = g.kept_hks = g.own = false;
  g.canned = false;
  g.dir = game_dir;
  g.ini = game_dir.empty() ? std::wstring() : seen(game_dir);
  g.version = mod_version;
  g.error.clear();
  if (!g.ini.empty()) WritePrivateProfileStringW(nullptr, nullptr, nullptr, g.ini.c_str());  // (Windows' copy of it: let go)
  read_file();
}

void load_canned(bool two_players) {
  g = State();
  g.canned = true;
  read_file();
  g.players = two_players ? 2 : 1;
}

bool has_mod() { return g.mod; }
const wchar_t* needs_mod(Row r) {
  const wchar_t* since = kInfo[r].since;
  return since && !g.version.empty() && core::compare_versions(g.version, since) < 0 ? since : nullptr;
}
const std::wstring& error() { return g.error; }
std::wstring ini_file() { return g.ini; }

Kind kind(Row r) { return kInfo[r].kind; }
const wchar_t* label(Row r) { return kInfo[r].label; }
const char* key(Row r) { return kInfo[r].key; }
bool usable(Row r) {
  if (!g.mod || needs_mod(r)) return false;
  // (a 4:3 frame has no sides for the menus to fill: the row has nothing to change)
  if (r == kMenus && g.cur[kAspect] != 0) return false;
  return true;
}

int count(Row r) {
  if (r == kResolution) return static_cast<int>(g.res.size());
  if (kInfo[r].kind == Kind::Level) return kLevelMax + 1;
  return static_cast<int>(kInfo[r].choices.size());
}

int value(Row r) { return g.cur[r]; }

std::wstring name(Row r, int v) {
  if (v < 0 || v >= count(r)) return L"";
  if (r == kResolution) return g.res[v].name;
  if (kInfo[r].kind == Kind::Level) return v == 0 ? std::wstring(L"без звука") : std::to_wstring(v) + L" из 10";
  return kInfo[r].choices[v].name;
}

bool set(Row r, int v) {
  const int n = count(r);
  if (n <= 0 || !usable(r)) return false;
  const Info& info = kInfo[r];
  v = info.kind == Kind::Level ? std::clamp(v, 0, kLevelMax) : ((v % n) + n) % n;
  g.error.clear();
  if (v == g.cur[r]) return true;
  g.cur[r] = v;
  const std::wstring text = r == kResolution ? g.res[v].value : info.kind == Kind::Level ? std::to_wstring(v) : info.choices[v].value;
  return put(info.section, info.ini, text) || failed();
}

bool reset(int page) {
  const int from = page == 2 ? kFirstSound : 0, to = page == 2 ? kRows : kFirstSound;
  for (int i = from; i < to; ++i) {
    const Row r = static_cast<Row>(i);
    if (!usable(r)) continue;
    const Info& info = kInfo[i];
    const int v = r == kResolution ? 0 : info.kind == Kind::Level ? _wtoi(info.fallback) : index_of(info, info.fallback);
    if (!set(r, v)) return false;
  }
  return true;
}

// --- УПРАВЛЕНИЕ -----------------------------------------------------------------------------------------

const wchar_t* action_label(int action) { return kActions[action].label; }
const char* action_key(int action) { return kActions[action].key; }
int key_code(int action) { return g.key[kActions[action].slot]; }

std::wstring key_name(int code) {
  static const struct {
    int code;
    const wchar_t* name;
  } kNames[] = {
      {0x01, L"Esc"},         {0x0C, L"-"},            {0x0D, L"="},          {0x0E, L"Backspace"},
      {0x0F, L"Tab"},         {0x1A, L"["},            {0x1B, L"]"},          {0x1C, L"Enter"},
      {0x1D, L"Левый Ctrl"},  {0x27, L";"},            {0x28, L"'"},          {0x29, L"`"},
      {0x2A, L"Левый Shift"}, {0x2B, L"\\"},           {0x33, L","},          {0x34, L"."},
      {0x35, L"/"},           {0x36, L"Правый Shift"}, {0x37, L"Num *"},      {0x38, L"Левый Alt"},
      {0x39, L"Пробел"},      {0x3A, L"Caps Lock"},    {0x45, L"Num Lock"},   {0x46, L"Scroll Lock"},
      {0x47, L"Num 7"},       {0x48, L"Num 8"},        {0x49, L"Num 9"},      {0x4A, L"Num -"},
      {0x4B, L"Num 4"},       {0x4C, L"Num 5"},        {0x4D, L"Num 6"},      {0x4E, L"Num +"},
      {0x4F, L"Num 1"},       {0x50, L"Num 2"},        {0x51, L"Num 3"},      {0x52, L"Num 0"},
      {0x53, L"Num ."},       {0x57, L"F11"},          {0x58, L"F12"},        {0x9C, L"Num Enter"},
      {0x9D, L"Правый Ctrl"}, {0xB5, L"Num /"},        {0xB8, L"Правый Alt"}, {0xC5, L"Pause"},
      {0xC7, L"Home"},        {0xC8, L"Стрелка вверх"}, {0xC9, L"Page Up"},   {0xCB, L"Стрелка влево"},
      {0xCD, L"Стрелка вправо"}, {0xCF, L"End"},       {0xD0, L"Стрелка вниз"}, {0xD1, L"Page Down"},
      {0xD2, L"Insert"},      {0xD3, L"Delete"},       {0xDB, L"Левый Win"},  {0xDC, L"Правый Win"},
      {kMouseLeft, L"ЛКМ"},   {kMouseRight, L"ПКМ"},   {kMouseMiddle, L"Колесо"}, {kMouse4, L"Мышь 4"},  {kMouse5, L"Мышь 5"}};
  for (const auto& k : kNames)
    if (k.code == code) return k.name;
  static const wchar_t kLetters[] = L"QWERTYUIOP\0\0\0\0ASDFGHJKL";  // 0x10..0x26
  if (code >= 0x02 && code <= 0x0B) return std::wstring(1, L"1234567890"[code - 0x02]);
  if (code >= 0x10 && code <= 0x26 && kLetters[code - 0x10]) return std::wstring(1, kLetters[code - 0x10]);
  if (code >= 0x2C && code <= 0x32) return std::wstring(1, L"ZXCVBNM"[code - 0x2C]);
  if (code >= 0x3B && code <= 0x44) return L"F" + std::to_wstring(code - 0x3B + 1);
  if (code <= 0) return L"—";
  wchar_t buf[16];
  swprintf(buf, 16, L"код 0x%02X", code);
  return buf;
}

bool key_allowed(int code) {
  if (code >= kMouseLeft && code <= kMouse5) return true;
  return std::find(std::begin(kAllowed), std::end(kAllowed), code) != std::end(kAllowed);
}

bool set_key(int action, int code) {
  if (!g.mod || action < 0 || action >= kKeyActions || !key_allowed(code)) return false;
  g.error.clear();
  const int slot = kActions[action].slot, old = g.key[slot];
  if (old == code) return true;
  for (int& k : g.key)
    if (k == code) k = old;
  g.key[slot] = code;
  return put_keys();
}

int layout() { return is_layout(1) ? 1 : is_layout(0) ? 0 : 2; }
bool has_own_layout() { return g.own; }

bool set_layout(int v) {
  const int now = layout();
  if (!g.mod || v < 0 || v > 2) return false;
  if (v == now) return true;
  if (v == 2 && !g.own) return false;
  g.error.clear();
  if (now == 2) {  // the player's own keys are kept: they come back with СВОЯ
    g.own = true;
    std::memcpy(g.own_key, g.key, sizeof(g.key));
    put(L"input", L"keys_own", key_list(g.own_key));
    g.error.clear();  // (not kept in the file: kept until the launcher is closed)
  }
  std::memcpy(g.key, v == 2 ? g.own_key : kLayouts[v], sizeof(g.key));
  return put_keys();
}

bool reset_keys() {
  if (!g.mod) return false;
  return set_layout(0) && set_keyboard_players(1);
}

int keyboard_players() { return g.players; }

bool set_keyboard_players(int n) {
  if (!g.mod || n != 1) return false;  // (one player at the keyboard in this game)
  g.error.clear();
  if (n == g.players) return true;
  g.players = n;
  return put(L"input", L"keyboard_players", std::to_wstring(n)) || failed();
}

const wchar_t* half_label(int row) { return kHalves[row].label ? kHalves[row].label : L""; }
const wchar_t* half_keys(int row) { return kHalves[row].keys ? kHalves[row].keys : L""; }

}  // namespace gset
}  // namespace lz
