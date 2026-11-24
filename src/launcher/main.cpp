// meleenet-launcher.exe: the window, the pointer and the keys, what each press does, the work in the
// background (asking GitHub, downloading, installing), and the modes without a window:
//   /install <game folder> [/quiet] [/package <launcher file>]  puts the mod in (from this launcher, or from
//                                                               another one's inside), exit code 0 or 2
//   /uninstall <game folder> [/quiet]                           takes it out
//   --shot <screen> <file.png> [--scale N] [--accent 0..3]   a screen of the design as a picture
//   --check [--out file]                                     ask for the newest release, write what came
//   --where [--out file]                                     where the game is found from here
//   --after-update <version>                                 this launcher has just taken that one's place and was
//                                                            started by it: it says so and updates itself no further
// Tests: --settings <ini>, --cache <dir>, --repo <url> (the repository served from elsewhere), --pick <dir>
// (the answer of the folder dialog), --drive "<script>" (the window stays hidden; wait:ms, click:name,
// until:screen:ms, type:text, shot:file, keydown:vk:scan:extended, rdown, xdown:n, state, quit), --out <file>
// (a log of what happened), --drive-next "<script>" (the script of the launcher this one starts in its place:
// without it a launcher run by a script starts no other).
#include "launcher/launcher.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <cstdio>
#include <cwctype>
#include <memory>
#include <thread>

#include "launcher/gameset.h"
#include "launcher/res.h"

namespace lz {
namespace {

const wchar_t kClass[] = L"meleenet-launcher";
const UINT WM_APP_CHECK = WM_APP + 1, WM_APP_STAGE = WM_APP + 2, WM_APP_PROGRESS = WM_APP + 3, WM_APP_GAME = WM_APP + 4,
           WM_APP_DONE = WM_APP + 5;

Model g;
HWND g_hwnd = nullptr;
ID2D1HwndRenderTarget* g_rt = nullptr;
std::unique_ptr<gfx::Canvas> g_canvas;
std::vector<Hit> g_hits;
std::atomic<bool> g_cancel{false};
std::atomic<int> g_gen{0};       // checks: only the newest one's answer counts
bool g_job = false;              // a download / install / removal runs
bool g_startup_check = false;    // the check of the start (it may install by itself)
bool g_self_auto = false;        // the job that runs is the launcher taking a newer one's place by itself, at the start
std::wstring g_after_update;     // the version of the launcher this one has just replaced (it started this one)
HANDLE g_once = nullptr;         // one launcher at a time
ULONGLONG g_drop_old_until = 0;  // the file of the launcher this one replaced is removed once that one has closed
net::Route g_route;              // the way to GitHub that worked last
Pending g_last_job = Pending::None;
ULONGLONG g_start = 0, g_confirm_until = 0, g_launch_until = 0, g_quit_at = 0;
bool g_tracking = false, g_dirty = true;

// tests
bool g_drive = false;
std::wstring g_out, g_pick, g_drive_next;
std::vector<std::wstring> g_script;
size_t g_script_at = 0;
ULONGLONG g_script_wait = 0, g_script_deadline = 0;

const wchar_t* screen_name(Screen s) {
  switch (s) {
    case Screen::Checking: return L"checking";
    case Screen::Update: return L"update";
    case Screen::Downloading: return L"downloading";
    case Screen::Ready: return L"ready";
    case Screen::Install: return L"install";
    case Screen::NoGame: return L"nogame";
    case Screen::Offline: return L"offline";
    case Screen::Running: return L"running";
    case Screen::Problem: return L"problem";
    default: return L"settings";
  }
}

void logf(const std::wstring& line) {
  if (g_out.empty()) return;
  FILE* f = nullptr;
  if (_wfopen_s(&f, g_out.c_str(), L"ab") || !f) return;
  std::string s = core::narrow(L"[" + std::to_wstring(GetTickCount64() - g_start) + L"] " + line + L"\n");
  fwrite(s.data(), 1, s.size(), f);
  fclose(f);
}

void dirty() {
  g_dirty = true;
  if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

void set_screen(Screen s) {
  if (g.screen != s) logf(std::wstring(L"screen ") + screen_name(g.screen) + L" -> " + screen_name(s));
  g.screen = s;
  g.net.clear();
  dirty();
}

void flash(const std::wstring& text, int ms = 4500) {
  g.flash = text;
  g.flash_until = GetTickCount64() + ms;
  logf(L"note: " + text);
  dirty();
}

// The game's settings of the ГРАФИКА, ЗВУК and УПРАВЛЕНИЕ pages, as the game's folder has them now.
void load_game_settings() {
  gset::load(g.has_game ? g.set.game_dir : std::wstring(), g.inst.present ? g.inst.version : std::wstring());
  g.capture = -1;
  logf(std::wstring(L"game settings: ") + (gset::has_mod() ? L"read" : L"no meleenet.ini"));
}

void refresh_installed() {
  g.has_game = core::has_game(g.set.game_dir);
  g.exe_ok = !g.has_game || core::exe_supported(g.set.game_dir);
  g.inst = g.has_game ? core::read_installed(g.set.game_dir) : Installed{};
  if (g.inst.present && !g.inst.protocol) g.inst.protocol = core::known_protocol(g.rel, g.inst.version);
  if (g.screen == Screen::Settings && g.tab != Tab::Launcher) load_game_settings();
}

// What there is to install: GitHub's version if it is newer than the one inside this launcher (and its
// file can be had), else the one inside.
const Release& offer() { return core::target(g.rel, g.assets_blocked); }

Screen decide() {
  if (!g.has_game) return Screen::NoGame;
  if (!g.inst.present) return Screen::Install;
  if (g.inst.version.empty() || core::compare_versions(offer().version, g.inst.version) > 0) return Screen::Update;
  if ((g.rel.known && !g.assets_blocked) || g.unchecked) return Screen::Ready;
  return Screen::Offline;  // (nothing newer inside, and GitHub did not say)
}

void go_decided() {
  Screen s = decide();
  if (g.screen == Screen::Settings) g.back = s;
  else set_screen(s);
  dirty();
}

// --- the work in the background -----------------------------------------------------------------

void start_check(bool quiet) {
  if (!core::has_repo()) {  // (nothing to ask: the mod inside this launcher is all there is)
    g.unchecked = true;
    g.rechecking = false;
    return;
  }
  int gen = ++g_gen;
  g.unchecked = false;
  if (quiet) {
    g.rechecking = true;
  } else if (g.has_game && g.screen != Screen::Settings) {
    set_screen(Screen::Checking);
  }
  logf(L"check: asking");
  dirty();
  Settings s = g.set;
  HWND hwnd = g_hwnd;
  std::thread([s, hwnd, gen]() {
    static std::atomic<bool> never{false};
    auto* r = new core::CheckResult(core::check(s, never));
    if (!PostMessageW(hwnd, WM_APP_CHECK, static_cast<WPARAM>(gen), reinterpret_cast<LPARAM>(r))) delete r;
  }).detach();
}

struct Done {
  core::Step step;
  Pending kind;
  std::wstring log;      // what the install or the removal said, line by line
  std::wstring package;  // the newer launcher file it came from (empty: this launcher)
  Release rel;           // what was being installed
};

void start_job(Pending kind) {
  if (g_job) return;
  Release rel = kind == Pending::Launcher ? g.rel : offer();
  bool download = kind == Pending::Launcher || (kind == Pending::Install && core::newer_on_github(g.rel, g.assets_blocked));
  if (kind == Pending::Install && !rel.known) {
    flash(L"В этом лаунчере нет мода: скачайте лаунчер заново");
    return;
  }
  g_job = true;
  g_last_job = kind;
  g_cancel = false;
  g.pending = kind;
  g.stage = kind == Pending::Uninstall ? 2 : download ? 0 : 1;
  g.got = 0;
  g.total = download ? rel.pkg_size : 0;
  g.confirm_uninstall = false;
  set_screen(Screen::Downloading);
  logf(kind == Pending::Uninstall ? std::wstring(L"job: uninstall")
       : kind == Pending::Launcher ? L"job: a newer launcher, " + rel.launcher_version
       : L"job: install " + rel.version + (download ? L" from GitHub" : L" from this launcher"));
  Settings s = g.set;
  net::Route route = g_route;
  HWND hwnd = g_hwnd;
  std::thread([rel, download, s, route, hwnd, kind]() {
    core::UpdateHooks hooks;
    hooks.stage = [hwnd](int stage) { PostMessageW(hwnd, WM_APP_STAGE, static_cast<WPARAM>(stage), 0); };
    hooks.progress = [hwnd](uint64_t got, uint64_t total) {
      static ULONGLONG last = 0;
      ULONGLONG now = GetTickCount64();
      if (got == total || now - last >= 40) {
        last = now;
        PostMessageW(hwnd, WM_APP_PROGRESS, static_cast<WPARAM>(got), static_cast<LPARAM>(total));
      }
    };
    hooks.game_open = [hwnd](bool waiting) { PostMessageW(hwnd, WM_APP_GAME, waiting ? 1 : 0, 0); };
    auto* d = new Done{core::Step::Ok, kind, L"", L"", rel};
    if (kind == Pending::Uninstall) d->step = core::uninstall(s, hooks, g_cancel, &d->log);
    else if (kind == Pending::Launcher) d->step = core::fetch_launcher(rel, s, route, hooks, g_cancel, &d->log, &d->package);
    else d->step = core::install(rel, download, s, route, hooks, g_cancel, &d->log, &d->package);
    if (!PostMessageW(hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(d))) delete d;
  }).detach();
}

void problem(const wchar_t* title, const std::wstring& text, const wchar_t* primary, Fix fix) {
  g.problem_title = title;
  g.problem_text = text;
  g.problem_primary = primary;
  g.problem_secondary = L"НАЗАД";
  g.fix = fix;
  g.fix2 = Fix::Back;
  set_screen(Screen::Problem);
  logf(std::wstring(L"problem: ") + title);
}

// The line of a log that says what went wrong: the last one that begins "не удалось" or has an error in it.
std::wstring reason(const std::wstring& log) {
  std::wstring last, why;
  for (size_t p = 0; p < log.size();) {
    size_t e = log.find(L'\n', p);
    if (e == std::wstring::npos) e = log.size();
    std::wstring line = log.substr(p, e - p);
    if (!line.empty()) last = line;
    if (line.rfind(L"не удалось", 0) == 0 || line.find(L"не уда") != std::wstring::npos || line.find(L"нет ") != std::wstring::npos ||
        line.find(L"запущена") != std::wstring::npos)
      why = line;
    p = e + 1;
  }
  std::wstring out = why.empty() ? last : why;
  if (!out.empty()) out[0] = static_cast<wchar_t>(towupper(out[0]));
  return out.empty() ? L"Причина не названа" : out;
}

// A newer launcher file is at hand (downloaded and checked): it takes this one's place. True: it has.
bool take_newer_launcher(const std::wstring& file, bool say) {
  pkg::Package p;
  if (file.empty() || !p.open(file) || core::compare_versions(p.launcher_version, kLauncherVersion) <= 0) return false;
  if (core::replace_self(file)) {
    g.swapped = true;
    logf(L"launcher replaced by " + p.launcher_version);
    if (say) flash(L"Лаунчер обновлён до " + p.launcher_version + L": новая версия откроется при следующем запуске", 7000);
    return true;
  }
  logf(L"launcher NOT replaced");
  if (say) flash(L"Лаунчер не смог заменить свой файл: скачайте новый со страницы релиза", 7000);
  if (say && g.set.self_skip != p.launcher_version) {  // (a folder closed to its user: not tried by itself at every start)
    g.set.self_skip = p.launcher_version;
    core::save_settings(g.set);
  }
  return false;
}

// The newer launcher is in this one's place: it is started, and this one closes. A launcher run by a
// script starts another only when the script says what that one is to do (--drive-next).
void restart_self() {
  if (g_drive && g_drive_next.empty()) {
    logf(L"restart: not in a script");
    return;
  }
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring cmd = core::quoted(self) + L" --after-update " + kLauncherVersion;
  if (!core::g_settings_path.empty()) cmd += L" --settings " + core::quoted(core::g_settings_path);
  if (!core::g_cache_dir.empty()) cmd += L" --cache " + core::quoted(core::g_cache_dir);
  if (!core::g_repo_url.empty()) cmd += L" --repo " + core::quoted(core::g_repo_url);
  if (!g_pick.empty()) cmd += L" --pick " + core::quoted(g_pick);
  if (!g_out.empty()) cmd += L" --out " + core::quoted(g_out);
  if (g_drive) cmd += L" --drive " + core::quoted(g_drive_next);
  if (g_once) {  // (the one that starts must not find this one and step back)
    ReleaseMutex(g_once);
    CloseHandle(g_once);
    g_once = nullptr;
  }
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    logf(L"restart: the newer launcher is started");
    g_cancel = true;
    DestroyWindow(g_hwnd);
    return;
  }
  logf(L"restart: FAILED, error " + std::to_wstring(GetLastError()));
  if (!g_drive) g_once = CreateMutexW(nullptr, TRUE, L"meleenet-launcher-running");
}

// A newer launcher on GitHub takes this one's place by itself: at the start, when nothing else goes on.
bool wants_self_update() {
  return g.set.auto_self && g_after_update.empty() && !g_job && !g.swapped && !g.launching && g.screen != Screen::Settings &&
         g.rel.full && !g.assets_blocked && !g.rel.launcher_version.empty() && g.rel.launcher_version != g.set.self_skip &&
         core::compare_versions(g.rel.launcher_version, kLauncherVersion) > 0;
}

// What the start goes on with once the launcher itself is settled: a newer mod installed without a question.
void after_self_update() {
  if (g.set.auto_install && g.screen == Screen::Update && !g_job) start_job(Pending::Install);
}

void on_done(Done* d) {
  g_job = false;
  g.pending = Pending::None;
  const bool self = d->kind == Pending::Launcher && g_self_auto;
  g_self_auto = false;
  logf(L"job done: step " + std::to_wstring(static_cast<int>(d->step)));
  for (size_t p = 0; p < d->log.size();) {
    size_t e = d->log.find(L'\n', p);
    if (e == std::wstring::npos) e = d->log.size();
    if (e > p) logf(L"  " + d->log.substr(p, e - p));
    p = e + 1;
  }
  if (self && d->step != core::Step::Ok) {  // (done by itself: no screen about it, the start goes on as it would)
    go_decided();
    if (d->step != core::Step::Cancelled) {
      flash(d->step == core::Step::NetFail ? std::wstring(L"Лаунчер не обновился: загрузка не удалась. Играть можно как раньше")
            : d->step == core::Step::BadSum ? std::wstring(L"Лаунчер не обновился: файл с GitHub повреждён. Играть можно как раньше")
            : L"Лаунчер не обновился: " + reason(d->log), 7000);
      after_self_update();
    }
    delete d;
    return;
  }
  switch (d->step) {
    case core::Step::Ok:
      refresh_installed();
      if (d->kind == Pending::Install) {
        if (!d->rel.notes.empty()) {  // the notes of what is installed now
          g.set.notes_version = d->rel.version;
          g.set.notes_title = d->rel.title;
          g.set.notes_date = d->rel.date;
          g.set.notes = d->rel.notes;
          core::save_settings(g.set);
        }
        go_decided();
        flash(L"Мод установлен: версия " + g.inst.version);
        take_newer_launcher(d->package, false);
      } else if (d->kind == Pending::Launcher) {
        go_decided();
        if (take_newer_launcher(d->package, true)) restart_self();
        if (g_hwnd && self) after_self_update();  // (this launcher goes on: its file could not be replaced, or a script runs it)
      } else {
        go_decided();
        flash(L"Мод удалён из папки игры");
      }
      break;
    case core::Step::Cancelled:
      go_decided();
      break;
    case core::Step::NetFail:
      problem(L"Загрузка не удалась",
              L"Сервер загрузок GitHub не ответил, или связь оборвалась. Проверьте интернет и прокси в настройках.",
              L"ПОВТОРИТЬ", Fix::Retry);
      break;
    case core::Step::BadSum:
      problem(L"Файл повреждён",
              L"Контрольная сумма скачанного файла не совпала с той, что указана в релизе. Файл удалён — скачайте его ещё раз.",
              L"СКАЧАТЬ ЗАНОВО", Fix::Retry);
      break;
    case core::Step::BadPackage:
      problem(L"В релизе не тот файл", reason(d->log) + L". Попробуйте позже или скачайте лаунчер со страницы релиза.",
              L"ПОВТОРИТЬ", Fix::Retry);
      break;
    case core::Step::Denied:
      problem(L"Нужно разрешение Windows",
              L"Игра лежит в папке, куда обычной программе писать нельзя. Windows спросит разрешение — без него мод не поставить и не убрать.",
              L"ПОВТОРИТЬ", Fix::Retry);
      break;
    default:
      problem(d->kind == Pending::Uninstall ? L"Удаление не удалось" : L"Установка не удалась", reason(d->log) + L".", L"ПОВТОРИТЬ",
              Fix::Retry);
      break;
  }
  delete d;
}

void on_check(int gen, core::CheckResult* r) {
  if (gen != g_gen.load()) {
    delete r;
    return;
  }
  logf(L"check: " + (r->rel.known ? L"version " + r->rel.version + (r->rel.full ? L" (described)" : L" (the version only)")
                                  : std::wstring(L"no answer")) +
       (r->assets_blocked ? L", files blocked" : L"") + (r->route.label.empty() ? L"" : L", " + r->route.label));
  for (size_t p = 0; p < r->log.size();) {
    size_t e = r->log.find(L'\n', p);
    logf(L"  " + r->log.substr(p, e - p));
    p = e == std::wstring::npos ? r->log.size() : e + 1;
  }
  g.rel = r->rel;
  g.assets_blocked = r->assets_blocked;
  if (r->rel.known) g_route = r->route;
  g.rechecking = false;
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t at[16];
  swprintf(at, 16, L"%02d:%02d", t.wHour, t.wMinute);
  g.checked_at = r->rel.known ? at : L"";
  g.via = r->rel.known && r->route.kind == 2 ? r->route.label : L"";
  refresh_installed();
  bool startup = g_startup_check;
  g_startup_check = false;
  delete r;
  if (g.screen == Screen::Downloading || g.screen == Screen::Running || g.screen == Screen::Problem) return;
  go_decided();
  if (startup && wants_self_update()) {
    g_self_auto = true;
    start_job(Pending::Launcher);
    return;
  }
  if (startup && g.set.auto_install && g.screen == Screen::Update) start_job(Pending::Install);
}

// --- what a press does -------------------------------------------------------------------------

void open_url(const std::wstring& url) {
  logf(L"open " + url);
  if (!g_drive) ShellExecuteW(g_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void copy_text(const std::wstring& text) {
  if (!OpenClipboard(g_hwnd)) return;
  EmptyClipboard();
  size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    memcpy(GlobalLock(h), text.c_str(), bytes);
    GlobalUnlock(h);
    if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
  }
  CloseClipboard();
}

void play() {
  if (!g.has_game || g.launching) return;
  logf(L"play");
  if (!g_drive && !core::launch_game(g.set.game_dir)) {
    flash(L"Не удалось запустить ztmnt.exe");
    return;
  }
  g.launching = true;
  g_launch_until = GetTickCount64() + 5000;
  if (g.set.close_on_play && !g_drive) g_quit_at = GetTickCount64() + 2500;
  dirty();
}

void choose_folder() {
  std::wstring dir;
  if (g_drive) dir = g_pick;
  else if (!core::pick_folder(g_hwnd, g.set.game_dir, dir)) return;
  if (dir.empty()) return;
  if (!core::has_game(dir)) {
    flash(L"В этой папке нет ztmnt.exe");
    return;
  }
  logf(L"folder chosen");
  g.set.game_dir = core::trimmed(dir);
  core::save_settings(g.set);
  refresh_installed();
  go_decided();
  if (!g.rel.known && !g.rechecking) start_check(true);
}

void fix(Fix f) {
  switch (f) {
    case Fix::Retry:
      start_job(g_last_job == Pending::None ? Pending::Install : g_last_job);
      break;
    case Fix::Recheck:
      start_check(false);
      break;
    default:
      go_decided();
      break;
  }
}

// A setting of the game was not written: why, in the footer.
void say_why() {
  flash(gset::error().empty() ? std::wstring(L"Настройка не записалась") : gset::error(), 8000);
}

// УПРАВЛЕНИЕ: the key (or mouse button) for the action that waits for one.
void bind_key(int code) {
  const int action = g.capture;
  if (action < 0) return;
  if (!gset::key_allowed(code)) {  // (the cell goes on waiting)
    flash(code == 0x1C || code == 0x9C ? std::wstring(L"Enter — это Start и «выбрать» в меню: игра его не отдаёт")
          : code == 0xC8 || code == 0xD0 || code == 0xCB || code == 0xCD ? std::wstring(L"Стрелки ходят и так, всегда: назначать их не нужно")
          : L"Эту клавишу игра не берёт: " + gset::key_name(code));
    return;
  }
  g.capture = -1;
  bool ok = gset::set_key(action, code);
  logf(std::wstring(L"key ") + core::wide(gset::action_key(action)) + L" = " + std::to_wstring(code) + (ok ? L"" : L": NOT WRITTEN"));
  if (!ok) say_why();
  dirty();
}

// A row of the game's settings: one of its parts was pressed.
void act_row(int id) {
  const gset::Row r = static_cast<gset::Row>((id - H_Row) / kRowParts);
  const int part = (id - H_Row) % kRowParts, v = gset::value(r);
  const int to = part == P_Prev || part == P_Minus ? v - 1 : part == P_Next || part == P_Plus ? v + 1 : part - P_Item;
  bool ok = gset::set(r, to);
  logf(std::wstring(L"set ") + core::wide(gset::key(r)) + L" = " + gset::name(r, gset::value(r)) + (ok ? L"" : L": NOT WRITTEN"));
  if (!ok) say_why();
}

void act(int id) {
  logf(L"press " + std::to_wstring(id));
  if (id != H_ProxyField && g.proxy_focus) {
    g.proxy_focus = false;
    core::save_settings(g.set);
  }
  if (id != H_Uninstall) g.confirm_uninstall = false;
  if (id != H_Key + g.capture) g.capture = -1;  // (a press elsewhere: the cell waits no more)
  if (id >= H_Row && id < H_Row + gset::kRows * kRowParts) {
    act_row(id);
    dirty();
    return;
  }
  if (id >= H_Key && id < H_Key + gset::kKeyActions) {
    if (g.capture == id - H_Key) bind_key(gset::kMouseLeft);  // (a second press on the waiting cell: the mouse's own button)
    else g.capture = id - H_Key;
    dirty();
    return;
  }
  switch (id) {
    case H_Close:
      g_cancel = true;
      DestroyWindow(g_hwnd);
      return;
    case H_Min:
      ShowWindow(g_hwnd, SW_MINIMIZE);
      return;
    case H_Settings:
      if (g.screen == Screen::Settings) {
        set_screen(g.back);
      } else {
        g.back = g.screen;
        set_screen(Screen::Settings);
        if (g.tab != Tab::Launcher) load_game_settings();
      }
      break;
    case H_Tab0:
    case H_Tab1:
    case H_Tab2:
    case H_Tab3:
      g.tab = static_cast<Tab>(id - H_Tab0);
      if (g.tab != Tab::Launcher) load_game_settings();
      break;
    case H_Reset: {
      bool ok = g.tab == Tab::Graphics ? gset::reset(1) : g.tab == Tab::Sound ? gset::reset(2) : g.tab == Tab::Controls ? gset::reset_keys() : true;
      logf(ok ? L"page: defaults" : L"page: defaults NOT WRITTEN");
      if (!ok) say_why();
      break;
    }
    case H_KeysOne:
    case H_KeysTwo:
      if (!gset::set_keyboard_players(id == H_KeysTwo ? 2 : 1)) say_why();
      logf(L"keyboard players = " + std::to_wstring(gset::keyboard_players()));
      break;
    case H_LayoutPrev:
    case H_LayoutNext: {
      const int now = gset::layout(), n = now == 2 || gset::has_own_layout() ? 3 : 2;
      if (!gset::set_layout((now + (id == H_LayoutNext ? 1 : n - 1)) % n)) say_why();
      logf(L"layout = " + std::to_wstring(gset::layout()));
      break;
    }
    case H_Back:
      if (g.screen == Screen::Settings) set_screen(g.back);
      break;
    case H_Primary:
      if (!view::primary_enabled(g)) break;
      switch (g.screen) {
        case Screen::Checking:
        case Screen::Ready: play(); break;
        case Screen::Update:
        case Screen::Install: start_job(Pending::Install); break;
        case Screen::Offline:
          if (g.inst.present) play();
          else start_check(false);
          break;
        case Screen::NoGame: choose_folder(); break;
        case Screen::Running:
          if (core::close_game(g.set.game_dir)) flash(L"Игру попросили закрыться");
          else flash(L"Окно игры не найдено: закройте её сами");
          break;
        case Screen::Problem: fix(g.fix); break;
        default: break;
      }
      break;
    case H_Secondary:
      switch (g.screen) {
        case Screen::Update: play(); break;
        case Screen::Ready:
          if (!g.rechecking) start_check(true);
          break;
        case Screen::Offline: start_check(false); break;
        case Screen::Running: g_cancel = true; break;
        case Screen::Problem: fix(g.fix2); break;
        default: break;
      }
      break;
    case H_Cancel:
      g_cancel = true;
      break;
    case H_Folder:
    case H_FolderSettings:
      if (!g_job) choose_folder();
      break;
    case H_PaperLink:
      open_url(view::link_url(g));
      break;
    case H_Copy:
      copy_text(std::wstring(kRepoUrl) + L"/releases/latest");
      g.copied_until = GetTickCount64() + 3000;
      break;
    case H_Repo:
      if (core::has_repo()) open_url(core::repo());
      break;
    case H_NewLauncher:
      if (g.rel.full && !g_job && !g.swapped) start_job(Pending::Launcher);
      else if (!g.swapped && core::has_repo()) open_url(core::repo() + L"/releases/latest");
      break;
    case H_Toggle1:
      g.set.auto_check = !g.set.auto_check;
      core::save_settings(g.set);
      break;
    case H_Toggle2:
      g.set.auto_install = !g.set.auto_install;
      core::save_settings(g.set);
      break;
    case H_Toggle4:
      g.set.auto_self = !g.set.auto_self;
      g.set.self_skip.clear();  // (asked for again: tried again)
      core::save_settings(g.set);
      break;
    case H_Toggle3:
      g.set.close_on_play = !g.set.close_on_play;
      core::save_settings(g.set);
      break;
    case H_ProxyAuto:
    case H_ProxyNone:
    case H_ProxyOwn:
      g.set.proxy = id - H_ProxyAuto;
      g.proxy_focus = id == H_ProxyOwn && g.set.proxy_addr.empty();
      core::save_settings(g.set);
      break;
    case H_ProxyField:
      g.set.proxy = 2;
      g.proxy_focus = true;
      break;
    case H_OpenLog: {
      std::wstring log = core::join(g.set.game_dir, L"meleenet.log");
      if (g.has_game && core::exists(log)) open_url(log);
      else flash(L"Журнала пока нет: игра с модом ещё не запускалась");
      break;
    }
    case H_DesyncLogs: {
      // (this mod keeps no logs of desyncs; the second log worth reading is the one of the game that
      // stood on the network page before it started itself again into the session)
      std::wstring log = core::join(g.set.game_dir, L"meleenet-menu.log");
      if (g.has_game && core::exists(log)) open_url(log);
      else flash(L"Этого журнала пока нет: он появляется после сетевой игры");
      break;
    }
    case H_Reinstall:
      if (!g.has_game) flash(L"Сначала укажите папку игры");
      else if (!g_job) start_job(Pending::Install);
      break;
    case H_Uninstall:
      if (!g.inst.present) {
        flash(L"Мод не установлен");
      } else if (!g.confirm_uninstall) {
        g.confirm_uninstall = true;
        g_confirm_until = GetTickCount64() + 5000;
      } else if (!g_job) {
        start_job(Pending::Uninstall);
      }
      break;
    case H_Accent0:
    case H_Accent1:
    case H_Accent2:
    case H_Accent3:
      g.set.accent = id - H_Accent0;
      core::save_settings(g.set);
      break;
    default:
      break;
  }
  dirty();
}

// --- the window -----------------------------------------------------------------------------

float scale_of(HWND hwnd) { return GetDpiForWindow(hwnd) / 96.0f; }

void discard_target() {
  g_canvas.reset();
  if (g_rt) g_rt->Release();
  g_rt = nullptr;
}

void paint(HWND hwnd) {
  if (!g_rt) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    float dpi = static_cast<float>(GetDpiForWindow(hwnd));
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    if (FAILED(gfx::d2d()->CreateHwndRenderTarget(
            props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)), &g_rt)))
      return;
    g_canvas = std::make_unique<gfx::Canvas>(g_rt);
  }
  g.time = (GetTickCount64() - g_start) / 1000.0;
  g_rt->BeginDraw();
  g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
  view::draw(*g_canvas, g, &g_hits);
  HRESULT hr = g_rt->EndDraw();
  if (g_drive) {
    static bool said = false;
    if (!said || FAILED(hr)) logf(FAILED(hr) ? L"window: drawing FAILED" : L"window: drawn");
    said = true;
  }
  if (hr == D2DERR_RECREATE_TARGET) discard_target();
  g_dirty = false;
}

// A key of the keyboard as DirectInput numbers it (what the game's key table holds): its scan code, with
// 0x80 for the keys of the extended set. Pause and Num Lock come the other way round.
int dik_of(WPARAM vk, LPARAM lp) {
  if (vk == VK_PAUSE) return 0xC5;
  if (vk == VK_NUMLOCK) return 0x45;
  int scan = static_cast<int>((lp >> 16) & 0xFF);
  if (!scan) scan = static_cast<int>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC)) & 0xFF;
  return scan | ((lp >> 24) & 1 ? 0x80 : 0);
}

int hit_at(float x, float y) {
  for (size_t i = g_hits.size(); i-- > 0;) {
    const Hit& h = g_hits[i];
    D2D1_POINT_2F p = h.inv.TransformPoint(D2D1::Point2F(x, y));
    if (p.x >= h.r.left && p.x < h.r.right && p.y >= h.r.top && p.y < h.r.bottom) return h.id;
  }
  return H_None;
}

bool moving() {
  return g.screen == Screen::Checking || g.screen == Screen::Downloading || g.rechecking || g.proxy_focus;
}

int hit_by_name(const std::wstring& n) {
  static const struct {
    const wchar_t* name;
    int id;
  } names[] = {{L"settings", H_Settings}, {L"min", H_Min}, {L"close", H_Close}, {L"primary", H_Primary},
               {L"secondary", H_Secondary}, {L"folder", H_Folder}, {L"link", H_PaperLink}, {L"copy", H_Copy},
               {L"cancel", H_Cancel}, {L"repo", H_Repo}, {L"back", H_Back}, {L"t1", H_Toggle1}, {L"t2", H_Toggle2},
               {L"t3", H_Toggle3}, {L"t4", H_Toggle4}, {L"proxy_auto", H_ProxyAuto}, {L"proxy_none", H_ProxyNone}, {L"proxy_own", H_ProxyOwn},
               {L"proxy_field", H_ProxyField}, {L"folder2", H_FolderSettings}, {L"log", H_OpenLog}, {L"desync", H_DesyncLogs},
               {L"reinstall", H_Reinstall}, {L"uninstall", H_Uninstall}, {L"accent0", H_Accent0}, {L"accent1", H_Accent1},
               {L"accent2", H_Accent2}, {L"accent3", H_Accent3}};
  for (auto& x : names)
    if (n == x.name) return x.id;
  // the settings' pages: tab.graphics, reset, keys.one, layout.next, key.jump, row.mode.next, row.music.b7, row.intro.s1
  static const struct {
    const wchar_t* name;
    int id;
  } more[] = {{L"tab.launcher", H_Tab0}, {L"tab.graphics", H_Tab1}, {L"tab.sound", H_Tab2}, {L"tab.controls", H_Tab3},
              {L"reset", H_Reset}, {L"keys.one", H_KeysOne}, {L"keys.two", H_KeysTwo}, {L"layout.prev", H_LayoutPrev},
              {L"layout.next", H_LayoutNext}};
  for (auto& x : more)
    if (n == x.name) return x.id;
  if (n.rfind(L"key.", 0) == 0) {
    for (int a = 0; a < gset::kKeyActions; ++a)
      if (n.substr(4) == core::wide(gset::action_key(a))) return H_Key + a;
  } else if (n.rfind(L"row.", 0) == 0) {
    const size_t dot = n.find(L'.', 4);
    if (dot == std::wstring::npos) return H_None;
    const std::wstring row = n.substr(4, dot - 4), part = n.substr(dot + 1);
    for (int r = 0; r < gset::kRows; ++r) {
      if (row != core::wide(gset::key(static_cast<gset::Row>(r)))) continue;
      const int base = H_Row + r * kRowParts;
      if (part == L"prev") return base + P_Prev;
      if (part == L"next") return base + P_Next;
      if (part == L"minus") return base + P_Minus;
      if (part == L"plus") return base + P_Plus;
      if (part.size() > 1 && (part[0] == L'b' || part[0] == L's')) return base + P_Item + _wtoi(part.c_str() + 1);
    }
  }
  return H_None;
}

// What the game's settings pages hold, for the log of a script.
void log_state() {
  std::wstring line = std::wstring(L"state: mod ") + (gset::has_mod() ? L"1" : L"0") + L" levels " +
                      L"1";
  for (int r = 0; r < gset::kRows; ++r) {
    const gset::Row row = static_cast<gset::Row>(r);
    line += L" | " + core::wide(gset::key(row)) + L"=" + gset::name(row, gset::value(row));
  }
  logf(line);
  line = L"state: players " + std::to_wstring(gset::keyboard_players()) + L" layout " + std::to_wstring(gset::layout()) +
         (gset::has_own_layout() ? L" (own kept)" : L"") + L" capture " + std::to_wstring(g.capture) + L" keys";
  for (int a = 0; a < gset::kKeyActions; ++a) line += L" " + core::wide(gset::action_key(a)) + L"=" + gset::key_name(gset::key_code(a));
  logf(line);
  logf(L"state: file " + gset::ini_file());
}

bool save_shot(const std::wstring& file, float scale) {
  g.time = (GetTickCount64() - g_start) / 1000.0;
  return gfx::save_png(file.c_str(), kW, kH, scale, [](gfx::Canvas& c) { view::draw(c, g, nullptr); });
}

std::vector<std::wstring> split(const std::wstring& s, wchar_t sep) {
  std::vector<std::wstring> out;
  size_t p = 0;
  while (p <= s.size()) {
    size_t e = s.find(sep, p);
    if (e == std::wstring::npos) e = s.size();
    if (e > p) out.push_back(s.substr(p, e - p));
    p = e + 1;
  }
  return out;
}

// The test script: one step at a time, from the timer.
void drive() {
  ULONGLONG now = GetTickCount64();
  while (g_script_at < g_script.size()) {
    if (now < g_script_wait) return;
    const std::wstring& cmd = g_script[g_script_at];
    size_t colon = cmd.find(L':');
    std::wstring op = cmd.substr(0, colon), arg = colon == std::wstring::npos ? L"" : cmd.substr(colon + 1);
    if (op == L"wait") {
      g_script_wait = now + _wtoi(arg.c_str());
      ++g_script_at;
      return;
    }
    if (op == L"until") {  // until:<screen>:<ms>
      size_t c2 = arg.find(L':');
      std::wstring want = arg.substr(0, c2);
      if (!g_script_deadline) g_script_deadline = now + (c2 == std::wstring::npos ? 20000 : _wtoi(arg.c_str() + c2 + 1));
      if (want == screen_name(g.screen)) {
        logf(L"until " + want + L": reached");
        g_script_deadline = 0;
        ++g_script_at;
        continue;
      }
      if (now > g_script_deadline) {
        logf(L"until " + want + L": TIMEOUT on " + screen_name(g.screen));
        g_script_deadline = 0;
        ++g_script_at;
        continue;
      }
      return;
    }
    ++g_script_at;
    if (op == L"click") {
      int id = hit_by_name(arg);
      bool there = false;
      std::vector<Hit> hits;  // (what is on the screen now: drawn aside)
      gfx::save_png(nullptr, kW, kH, 1, [&](gfx::Canvas& c) { view::draw(c, g, &hits); });
      for (auto& h : hits) there = there || h.id == id;
      logf(L"click " + arg + (there ? L"" : L": NOT ON THIS SCREEN"));
      if (there) act(id);
      if (!g_hwnd) return;
    } else if (op == L"mouse") {  // mouse:x,y - a real press at that place of the window
      paint(g_hwnd);
      float s = scale_of(g_hwnd);
      int mx = static_cast<int>(_wtof(arg.c_str()) * s), my = static_cast<int>(_wtof(arg.c_str() + arg.find(L',') + 1) * s);
      LPARAM at = MAKELPARAM(mx, my);
      SendMessageW(g_hwnd, WM_MOUSEMOVE, 0, at);
      logf(L"mouse " + arg + L": over " + std::to_wstring(g.hover) + L" of " + std::to_wstring(g_hits.size()));
      SendMessageW(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at);
      SendMessageW(g_hwnd, WM_LBUTTONUP, 0, at);
      if (!g_hwnd) return;
    } else if (op == L"type") {
      for (wchar_t ch : arg) SendMessageW(g_hwnd, WM_CHAR, ch, 0);
    } else if (op == L"keydown") {  // keydown:<virtual key>:<scan code>:<extended 0 | 1>, as the keyboard sends it
      std::vector<std::wstring> f = split(arg, L':');
      if (f.size() == 3) {
        LPARAM lp = 1 | (static_cast<LPARAM>(wcstol(f[1].c_str(), nullptr, 0) & 0xFF) << 16) | (static_cast<LPARAM>(_wtoi(f[2].c_str()) & 1) << 24);
        SendMessageW(g_hwnd, WM_KEYDOWN, static_cast<WPARAM>(wcstol(f[0].c_str(), nullptr, 0)), lp);
      }
    } else if (op == L"rdown") {
      SendMessageW(g_hwnd, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(4, 60));
    } else if (op == L"xdown") {
      SendMessageW(g_hwnd, WM_XBUTTONDOWN, MAKEWPARAM(0, _wtoi(arg.c_str()) == 2 ? XBUTTON2 : XBUTTON1), MAKELPARAM(4, 60));
    } else if (op == L"state") {
      log_state();
    } else if (op == L"shot") {
      logf(L"shot " + arg + (save_shot(arg, 1) ? L"" : L": FAILED"));
    } else if (op == L"quit") {
      logf(L"quit");
      DestroyWindow(g_hwnd);
      return;
    }
  }
}

void tick(HWND hwnd) {
  ULONGLONG now = GetTickCount64();
  if (!g.flash.empty() && now > g.flash_until) g.flash.clear(), dirty();
  if (g.copied_until && now > g.copied_until) g.copied_until = 0, dirty();
  if (g.confirm_uninstall && now > g_confirm_until) g.confirm_uninstall = false, dirty();
  if (g.launching && now > g_launch_until) g.launching = false, dirty();
  if (g_quit_at && now > g_quit_at) {
    DestroyWindow(hwnd);
    return;
  }
  if (g_drop_old_until && (core::drop_old_self() || now > g_drop_old_until)) g_drop_old_until = 0;
  if (g_drive) drive();
  if (g_hwnd && (moving() || g_dirty) && !IsIconic(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      paint(hwnd);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      if (g_rt) g_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
      return 0;
    case WM_DPICHANGED: {
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      discard_target();
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_TIMER:
      tick(hwnd);
      return 0;
    case WM_NCHITTEST: {
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      float s = scale_of(hwnd);
      if (p.y / s < 48 && p.x / s < 824) return HTCAPTION;  // the header drags the window
      return HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK:
      return 0;  // (no maximizing)
    case WM_SYSCOMMAND:
      if ((wp & 0xFFF0) == SC_MAXIMIZE) return 0;
      if ((wp & 0xFFF0) == SC_KEYMENU) return 0;  // (Alt opens no menu: it is a key a cell of УПРАВЛЕНИЕ may take)
      break;
    case WM_MOUSEMOVE: {
      float s = scale_of(hwnd);
      int id = hit_at(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
      if (id != g.hover) {
        g.hover = id;
        dirty();
      }
      if (!g_tracking) {
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
        g_tracking = TrackMouseEvent(&t) != FALSE;
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      g_tracking = false;
      if (g.hover) g.hover = 0, dirty();
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr, g.hover == H_ProxyField ? IDC_IBEAM : g.hover ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN: {
      float s = scale_of(hwnd);
      g.pressed = hit_at(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
      if (g.proxy_focus && g.pressed != H_ProxyField) {
        g.proxy_focus = false;
        core::save_settings(g.set);
      }
      SetCapture(hwnd);
      dirty();
      return 0;
    }
    case WM_LBUTTONUP: {
      float s = scale_of(hwnd);
      int id = hit_at(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s), was = g.pressed;
      g.pressed = 0;
      ReleaseCapture();
      dirty();
      if (was && id == was) act(id);
      return 0;
    }
    case WM_KILLFOCUS:
      if (g.capture >= 0) g.capture = -1, dirty();
      break;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
      if (g.capture < 0) break;
      bind_key(msg == WM_RBUTTONDOWN ? gset::kMouseRight : msg == WM_MBUTTONDOWN ? gset::kMouseMiddle
               : HIWORD(wp) == XBUTTON1 ? gset::kMouse4 : gset::kMouse5);
      return msg == WM_XBUTTONDOWN ? TRUE : 0;
    case WM_SYSKEYDOWN:
      if (g.capture < 0) break;  // (Alt and F10 are keys like any other for a cell that waits)
      [[fallthrough]];
    case WM_KEYDOWN:
      if (g.capture >= 0) {
        if (wp == VK_ESCAPE) {
          g.capture = -1;
          dirty();
        } else if (!(lp & (1 << 30))) {  // (not a held key's repeat)
          bind_key(dik_of(wp, lp));
        }
        return 0;
      }
      if (g.proxy_focus) {
        if (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB) {
          g.proxy_focus = false;
          core::save_settings(g.set);
          dirty();
        }
        return 0;
      }
      if (wp == VK_ESCAPE && g.screen == Screen::Settings) act(H_Back);
      else if (wp == VK_RETURN && g.screen != Screen::Settings && g.screen != Screen::Downloading) act(H_Primary);
      return 0;
    case WM_CHAR:
      if (g.proxy_focus) {
        if (wp == 8) {
          if (!g.set.proxy_addr.empty()) g.set.proxy_addr.pop_back();
        } else if (wp == 22) {  // Ctrl+V
          if (OpenClipboard(hwnd)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
              if (const wchar_t* t = static_cast<const wchar_t*>(GlobalLock(h))) {
                for (; *t && g.set.proxy_addr.size() < 64; ++t)
                  if (*t > 0x20 && *t < 0x7F) g.set.proxy_addr += *t;
                GlobalUnlock(h);
              }
            }
            CloseClipboard();
          }
        } else if (wp > 0x20 && wp < 0x7F && g.set.proxy_addr.size() < 64) {
          g.set.proxy_addr += static_cast<wchar_t>(wp);
        }
        dirty();
      }
      return 0;
    case WM_APP + 1:
      on_check(static_cast<int>(wp), reinterpret_cast<core::CheckResult*>(lp));
      return 0;
    case WM_APP + 2:
      g.stage = static_cast<int>(wp);
      if (g.screen == Screen::Running && g.stage == 2) set_screen(Screen::Downloading);
      logf(L"stage " + std::to_wstring(g.stage));
      dirty();
      return 0;
    case WM_APP + 3: {
      uint64_t got = static_cast<uint64_t>(wp), total = static_cast<uint64_t>(lp);
      if (g_drive && total && got * 4 / total != g.got * 4 / total) logf(L"downloaded " + std::to_wstring(got * 100 / total) + L"%");
      g.got = got;
      g.total = total;
    }
      dirty();
      return 0;
    case WM_APP + 4:
      if (g_job) set_screen(wp ? Screen::Running : Screen::Downloading);
      return 0;
    case WM_APP + 5:
      on_done(reinterpret_cast<Done*>(lp));
      return 0;
    case WM_DESTROY:
      g_cancel = true;
      discard_target();
      g_hwnd = nullptr;
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// The mod put into a game folder or taken out of it with no window of the launcher: for scripts, for
// this launcher asking Windows for administrator rights, and for launcher 1.0.0, which runs what it
// downloads as an installer. quiet: the report goes to the console the launcher was started from, if
// there is one; else it is shown in a box.
int headless(int verb, const std::wstring& dir, bool quiet, const std::wstring& package) {
  std::wstring log;
  pkg::Result r = pkg::Result::Failed;
  if (dir.empty()) {
    log = L"не указана папка игры\n";
  } else if (verb == 1) {
    pkg::Package p;
    if (p.open(package)) {
      r = pkg::install(p, dir, log);
      if (r == pkg::Result::Ok) log = L"Мод meleenet " + p.version + L" установлен в:\n" + dir + L"\n\n" + log;
    } else {
      log = package.empty() ? L"в этом лаунчере нет мода\n" : L"в файле " + package + L" нет мода\n";
    }
  } else {
    r = pkg::uninstall(dir, log);
    if (r == pkg::Result::Ok) log = L"Мод meleenet удалён из:\n" + dir + L"\n\n" + log;
  }
  if (r != pkg::Result::Ok) log = L"Не получилось.\n\n" + log;
  if (!quiet) {
    MessageBoxW(nullptr, log.c_str(), L"meleenet", MB_OK | (r == pkg::Result::Ok ? MB_ICONINFORMATION : MB_ICONERROR));
  } else if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    HANDLE out = GetStdHandle(r == pkg::Result::Ok ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    DWORD n = 0;
    std::wstring text = L"\n" + log;
    if (out && out != INVALID_HANDLE_VALUE) WriteConsoleW(out, text.c_str(), static_cast<DWORD>(text.size()), &n, nullptr);
  }
  if (!g_out.empty()) logf(log);
  return r == pkg::Result::Ok ? 0 : 2;
}

int run(int argc, wchar_t** argv) {
  std::wstring shot_screen, shot_file;
  float shot_scale = 1;
  int accent = -1;
  bool check_only = false, where_only = false, quiet = false;
  int verb = 0;  // 1 /install, 2 /uninstall
  std::wstring verb_dir, package;
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
    if (a == L"--shot") {
      shot_screen = next();
      shot_file = next();
    } else if (a == L"--scale") {
      shot_scale = static_cast<float>(_wtof(next().c_str()));
    } else if (a == L"--accent") {
      accent = _wtoi(next().c_str()) & 3;
    } else if (a == L"/install" || a == L"/uninstall") {
      verb = a == L"/install" ? 1 : 2;
    } else if (a == L"/quiet") {
      quiet = true;
    } else if (a == L"/package") {
      package = next();
    } else if (verb && verb_dir.empty() && !a.empty() && a[0] != L'-' && a[0] != L'/') {
      verb_dir = a;
    } else if (a == L"--check") {
      check_only = true;
    } else if (a == L"--where") {
      where_only = true;
    } else if (a == L"--out") {
      g_out = next();
    } else if (a == L"--settings") {
      core::g_settings_path = next();
    } else if (a == L"--cache") {
      core::g_cache_dir = next();
    } else if (a == L"--repo") {
      core::g_repo_url = next();
    } else if (a == L"--pick") {
      g_pick = next();
    } else if (a == L"--drive") {
      g_drive = true;
      g_script = split(next(), L';');
    } else if (a == L"--drive-next") {
      g_drive_next = next();
    } else if (a == L"--after-update") {
      g_after_update = next();
    }
  }
  g_start = GetTickCount64();
  if (verb) return headless(verb, core::trimmed(verb_dir), quiet, package);
  if (!gfx::init()) {
    MessageBoxW(nullptr, L"Не удалось включить Direct2D: лаунчеру нужна Windows 10 или новее.", L"meleenet", MB_ICONERROR);
    return 3;
  }

  if (!shot_file.empty()) {  // a screen of the design as a picture
    if (!view::demo(shot_screen, g)) return 2;
    if (accent >= 0) g.set.accent = accent;
    bool ok = gfx::save_png(shot_file.c_str(), kW, kH, shot_scale <= 0 ? 1 : shot_scale,
                            [](gfx::Canvas& c) { view::draw(c, g, nullptr); });
    if (!ok) {
      wchar_t why[80];
      swprintf(why, 80, L"shot failed at step %d, hr %08lX", gfx::g_last_step, static_cast<unsigned long>(gfx::g_last_hr));
      logf(why);
    }
    gfx::shutdown();
    return ok ? 0 : 1;
  }

  if (where_only) {  // where the game would be found from here, and what is in it
    std::wstring dir = core::find_game_dir();
    Installed in = core::read_installed(dir);
    logf(L"game: " + (dir.empty() ? std::wstring(L"(not found)") : dir));
    logf(std::wstring(L"mod: ") + (in.present ? (in.version.empty() ? L"of an unknown version" : in.version.c_str()) : L"not installed") +
         (dir.empty() ? L"" : core::exe_supported(dir) ? L", the exe is the supported one" : L", ANOTHER exe"));
    gfx::shutdown();
    return dir.empty() ? 1 : 0;
  }
  core::load_settings(g.set);
  if (check_only) {
    std::atomic<bool> never{false};
    core::CheckResult r = core::check(g.set, never);
    logf(std::wstring(L"known ") + (r.rel.known ? L"1" : L"0") + L" full " + (r.rel.full ? L"1" : L"0") + L" blocked " +
         (r.assets_blocked ? L"1" : L"0") + L" version " + r.rel.version + L" protocol " + std::to_wstring(r.rel.protocol) +
         L" route " + r.route.label);
    logf(L"date " + r.rel.date + L" title " + r.rel.title + L" notes " + std::to_wstring(r.rel.notes.size()) + L" file " +
         r.rel.pkg_name + L" " + std::to_wstring(r.rel.pkg_size) + L" " + core::wide(r.rel.pkg_sha256) + L" launcher " +
         r.rel.launcher_version);
    logf(L"inside this launcher: mod " + core::bundled().version + L", protocol " + std::to_wstring(core::bundled().protocol));
    logf(r.log);
    gfx::shutdown();
    return r.rel.known ? 0 : 1;
  }

  // One launcher at a time: a second start shows the first one.
  g_once = g_drive ? nullptr : CreateMutexW(nullptr, TRUE, L"meleenet-launcher-running");
  if (g_once && GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(kClass, nullptr)) {
      ShowWindow(other, SW_RESTORE);
      SetForegroundWindow(other);
    }
    return 0;
  }

  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_DROPSHADOW;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = inst;
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
  wc.hIconSm = wc.hIcon;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);

  // In the middle of the screen the pointer is on, at that screen's scale.
  POINT cursor{};
  GetCursorPos(&cursor);
  HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  GetMonitorInfoW(mon, &mi);
  UINT dpi = 96;
  if (HDC dc = GetDC(nullptr)) {
    dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
  }
  int w = MulDiv(kW, dpi, 96), h = MulDiv(kH, dpi, 96);
  int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2, y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
  g_hwnd = CreateWindowExW(0, kClass, L"meleenet — лаунчер", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, x, y, w, h, nullptr, nullptr,
                           inst, nullptr);
  if (!g_hwnd) return 3;
  UINT real = GetDpiForWindow(g_hwnd);  // (the screen it landed on may have another scale)
  if (real != dpi) SetWindowPos(g_hwnd, nullptr, 0, 0, MulDiv(kW, real, 96), MulDiv(kH, real, 96), SWP_NOMOVE | SWP_NOZORDER);
  int round = 2;  // DWMWCP_ROUND: round corners where Windows has them (11)
  DwmSetWindowAttribute(g_hwnd, 33, &round, sizeof(round));

  // Where things stand at the start.
  if (!core::has_game(g.set.game_dir) && !g_drive) {  // (a test never goes looking for the real game)
    std::wstring found = core::trimmed(core::find_game_dir());
    if (!found.empty()) {
      g.set.game_dir = found;
      core::save_settings(g.set);
    }
  }
  g.pack = core::bundled();
  g.repo = core::has_repo();
  // What an update of the launcher itself left behind. The launcher this one has just replaced may still be
  // closing: its file is removed as soon as it lets go of it.
  if (!core::drop_old_self()) g_drop_old_until = GetTickCount64() + 15000;
  if (!g_after_update.empty()) {
    logf(L"started by launcher " + g_after_update + L" in its place");
    flash(L"Лаунчер обновлён: " + g_after_update + L" → " + kLauncherVersion, 7000);
  }
  refresh_installed();
  logf(std::wstring(L"start: game ") + (g.has_game ? L"found" : L"not found") + L", mod " +
       (g.inst.present ? (g.inst.version.empty() ? L"of an unknown version" : g.inst.version) : L"not installed") +
       L"; inside the launcher: " + (g.pack.known ? g.pack.version : L"NOTHING"));
  if (!g.has_game) {
    g.screen = Screen::NoGame;
    start_check(true);  // (the newest version is known by the time a folder is picked)
  } else if (!g.inst.present) {
    g.screen = Screen::Install;  // (the mod is inside: it can go in at once; GitHub may still name a newer one)
    g_startup_check = true;
    start_check(true);
  } else if (g.set.auto_check && core::has_repo()) {
    g.screen = Screen::Checking;
    g_startup_check = true;
    start_check(false);
  } else {
    g.unchecked = true;
    g.screen = decide();
  }

  SetTimer(g_hwnd, 1, 33, nullptr);
  if (!g_drive) {
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
  }
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  logf(L"exit");
  gfx::shutdown();
  if (g_once) CloseHandle(g_once);
  return 0;
}

}  // namespace
}  // namespace lz

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  int rc = lz::run(argc, argv);
  LocalFree(argv);
  CoUninitialize();
  return rc;
}
