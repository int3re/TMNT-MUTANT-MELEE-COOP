// meleenet-launcher.exe: what its parts share. The launcher carries the mod inside itself: it finds the
// game, puts the mod in, asks GitHub for a newer release (a newer launcher, with a newer mod inside), gets
// it, checks it, installs from it, and starts the game.
//   model: what is on the screen (main.cpp changes it, view.cpp draws it)
//   core:  the game folder, the installed mod, settings, the release (core.cpp)
//   pkg:   the mod inside a launcher file, putting it into the game folder and taking it out (package.cpp)
//   net:   HTTP through WinHTTP, with or without a proxy (net.cpp)
//   gset:  the game's own settings on the ГРАФИКА, ЗВУК and УПРАВЛЕНИЕ pages (gameset.h, gameset.cpp)
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "launcher/gfx.h"

namespace lz {

constexpr int kW = 960, kH = 600;  // the window, in units of 1/96 inch
constexpr wchar_t kLauncherVersion[] = L"1.3.0";
constexpr wchar_t kRepoUrl[] = L"https://github.com/int3re/TMNT-MUTANT-MELEE-COOP";
constexpr wchar_t kRepoText[] = L"github.com/int3re/TMNT-MUTANT-MELEE-COOP";

enum class Screen { Checking, Update, Downloading, Ready, Install, NoGame, Offline, Running, Problem, Settings };

// A release of the mod: the newest on GitHub (by its meleenet-latest.json), or the one inside this launcher.
struct Release {
  bool known = false;   // the version is known
  bool full = false;    // GitHub's whole description came (the launcher file's name, size and checksum)
  std::wstring version;
  int protocol = 0;
  std::wstring date;    // "20 ноября 2026"
  std::wstring title;
  std::vector<std::wstring> notes;
  std::wstring pkg_name;          // the release's launcher file: the mod is inside it
  uint64_t pkg_size = 0;
  std::string pkg_sha256;
  std::wstring launcher_version;  // that launcher's own version
  std::vector<std::pair<std::wstring, int>> protocols;  // version -> protocol
};

struct Installed {
  bool present = false;   // the mod's files are in the game folder
  std::wstring version;   // empty: not known (put there by hand)
  int protocol = 0;       // 0: not known
};

struct Settings {
  std::wstring game_dir;
  bool auto_check = true;
  bool auto_self = true;     // a newer launcher on GitHub takes this one's place at the start, by itself
  bool auto_install = false;
  std::wstring self_skip;    // a launcher version that could not take this one's place: not tried by itself again
  bool close_on_play = true;
  int proxy = 0;  // 0 automatic, 1 none, 2 the address below
  std::wstring proxy_addr;
  int accent = 0;  // the bandana: 0 Leonardo's blue, 1 Raphael's red, 2 Donatello's purple, 3 Michelangelo's orange
  // the notes of the installed version (kept from the release it came from)
  std::wstring notes_version, notes_title, notes_date;
  std::vector<std::wstring> notes;
};

enum class Tab { Launcher, Graphics, Sound, Controls };  // the pages of the settings

enum class Pending { None, Install, Uninstall, Launcher };
enum class Fix { None, Retry, Back, Recheck };

struct Model {
  Screen screen = Screen::Checking;
  Screen back = Screen::Ready;  // under the settings
  Tab tab = Tab::Launcher;      // the settings' page
  int capture = -1;             // УПРАВЛЕНИЕ: the action that waits for a key (-1: none)
  Settings set;
  Installed inst;
  Release rel;   // what GitHub says
  Release pack;  // what is inside this launcher
  bool has_game = false;
  bool exe_ok = true;           // ztmnt.exe is the one the mod works with
  bool repo = false;            // the mod has a repository to ask (core::has_repo)
  bool unchecked = false;       // no answer asked of GitHub (the check at start is off)
  bool assets_blocked = false;  // github.com answers, its download host does not
  // the update
  Pending pending = Pending::None;
  int stage = 0;  // 0 the download, 1 the checksum, 2 the installer
  uint64_t got = 0, total = 0;
  // the footer's line
  std::wstring checked_at, via;  // when GitHub answered last (HH:MM), and by what way if not directly
  std::wstring net;
  int net_state = 0;  // 0 fine, 1 waiting, 2 bad
  std::wstring flash;  // a passing note in its place
  ULONGLONG flash_until = 0;
  // passing states of buttons
  bool launching = false, rechecking = false, confirm_uninstall = false;
  bool swapped = false;  // a newer launcher is in this one's place: it opens at the next start
  ULONGLONG copied_until = 0;
  // the Problem screen
  std::wstring problem_title, problem_text, problem_primary, problem_secondary;
  Fix fix = Fix::None, fix2 = Fix::None;
  // the pointer and the keyboard
  int hover = 0, pressed = 0;
  bool proxy_focus = false;
  double time = 0;  // seconds, for what moves
};

// What can be pressed. The view tells where each one is; main.cpp acts.
enum HitId {
  H_None = 0,
  H_Settings, H_Min, H_Close,
  H_Primary, H_Secondary, H_Folder, H_PaperLink, H_Copy, H_Cancel, H_Repo, H_NewLauncher,
  H_Back, H_Toggle1, H_Toggle2, H_Toggle3, H_Toggle4, H_ProxyAuto, H_ProxyNone, H_ProxyOwn, H_ProxyField,
  H_FolderSettings, H_OpenLog, H_DesyncLogs, H_Reinstall, H_Uninstall,
  H_Accent0, H_Accent1, H_Accent2, H_Accent3,
  H_Tab0, H_Tab1, H_Tab2, H_Tab3,                      // the settings' pages
  H_Reset,                                             // "По умолчанию" of the page shown
  H_KeysOne, H_KeysTwo, H_LayoutPrev, H_LayoutNext,    // УПРАВЛЕНИЕ
  H_Count,
  // A row of the game's settings (gset::Row r): H_Row + r * kRowParts + a part.
  H_Row = 1000,
  H_Key = 2000,  // + the key action: its cell
};
constexpr int kRowParts = 16;
enum RowPart { P_Prev, P_Next, P_Minus, P_Plus, P_Item };  // P_Item + i: a level's block i, or a pair's button i
struct Hit {
  int id;
  D2D1_RECT_F r;     // in the space of `inv`
  gfx::Matrix inv;   // window -> that space
};

namespace view {
// Draws the whole window; `hits` gets what can be pressed (null: not wanted).
void draw(gfx::Canvas& c, const Model& m, std::vector<Hit>* hits);
bool primary_enabled(const Model& m);
std::wstring link_url(const Model& m);  // where the banner's link leads
// The model of a screen as the design shows it (launcher --shot <screen>).
bool demo(const std::wstring& screen, Model& m);
}  // namespace view

namespace net {
struct Route {
  int kind = 0;        // 0 the system's settings, 1 no proxy, 2 a proxy
  std::wstring proxy;  // host:port
  std::wstring label;  // for the log and the footer
};
struct Response {
  bool sent = false;   // the server answered
  int status = 0;
  std::wstring location;  // of a redirect that was not followed
  std::string body;
  DWORD error = 0;
};
// GET. follow: redirects are followed. to_file: the body goes there (and `progress` hears of it).
Response get(const std::wstring& url, const Route& route, bool follow, const std::wstring& to_file = L"",
             const std::function<void(uint64_t got, uint64_t total)>& progress = nullptr,
             const std::atomic<bool>* cancel = nullptr);
bool port_open(const wchar_t* host, int port, int timeout_ms);
}  // namespace net

namespace core {
extern std::wstring g_repo_url;      // tests: the repository's page served from elsewhere (--repo)
std::wstring repo();                 // its address: github.com/..., unless a test says otherwise
extern std::wstring g_settings_path; // tests: another settings file (--settings)
extern std::wstring g_cache_dir;     // tests: another folder for downloads (--cache)

std::wstring downloads_dir();        // where downloads are kept (and a copy of the game's settings before a change)
std::wstring join(const std::wstring& dir, const std::wstring& name);
std::wstring trimmed(std::wstring dir);  // a folder without the slash at its end
bool exists(const std::wstring& path);
std::string narrow(const std::wstring& s);
std::wstring wide(const std::string& utf8);
int compare_versions(const std::wstring& a, const std::wstring& b);
int known_protocol(const Release& rel, const std::wstring& version);

void load_settings(Settings& s);
void save_settings(const Settings& s);

bool has_game(const std::wstring& dir);           // ztmnt.exe is there
bool has_repo();                                  // there is a repository to ask for updates
bool exe_supported(const std::wstring& dir);      // ... and it is the one the mod works with
std::wstring find_game_dir();                     // the registry, the launcher's own folder
Installed read_installed(const std::wstring& dir);
bool game_running(const std::wstring& dir);
int close_game(const std::wstring& dir);          // asks its windows to close; how many were asked
bool launch_game(const std::wstring& dir);
bool pick_folder(HWND owner, const std::wstring& initial, std::wstring& out);

// The newest release. Tries every route the settings allow; `route` gets the one that worked.
struct CheckResult {
  Release rel;
  bool answered = false;        // github.com answered at all
  bool assets_blocked = false;  // ... but the release's files did not come
  net::Route route;
  std::wstring log;
};
CheckResult check(const Settings& s, const std::atomic<bool>& cancel);
bool parse_manifest(const std::string& json, Release& rel);

// The mod inside this launcher (always there), and which version there is to install: GitHub's if it is
// newer than the one inside and its file can be had, else the one inside.
const Release& bundled();
bool newer_on_github(const Release& rel, bool assets_blocked);
const Release& target(const Release& rel, bool assets_blocked);

enum class Step { Ok, Cancelled, NetFail, BadSum, BadPackage, Failed, Denied };
struct UpdateHooks {
  std::function<void(int stage)> stage;
  std::function<void(uint64_t got, uint64_t total)> progress;
  std::function<void(bool waiting)> game_open;  // the game holds the mod's files: waiting for it to close
};
Step install(const Release& rel, bool download, const Settings& s, const net::Route& first, const UpdateHooks& hooks,
             const std::atomic<bool>& cancel, std::wstring* log, std::wstring* package_file);
Step uninstall(const Settings& s, const UpdateHooks& hooks, const std::atomic<bool>& cancel, std::wstring* log);
Step fetch_launcher(const Release& rel, const Settings& s, const net::Route& first, const UpdateHooks& hooks,
                    const std::atomic<bool>& cancel, std::wstring* log, std::wstring* package_file);
bool replace_self(const std::wstring& newer);
bool drop_old_self();  // true: nothing of the launcher this one replaced is left
std::wstring quoted(std::wstring path);  // a path as one argument of a command line
}  // namespace core

namespace pkg {
struct Entry {
  std::wstring name;
  int id = 0;              // the resource with its bytes (0: `text`)
  std::string text;
  bool loader = false, keep = false, if_missing = false;
};
class Package {
 public:
  Package() = default;
  ~Package();
  Package(const Package&) = delete;
  Package& operator=(const Package&) = delete;
  bool open(const std::wstring& file);  // a launcher file, read as data; empty: this launcher itself
  bool bytes(int id, const void*& data, DWORD& size) const;
  std::wstring version, launcher_version;
  int protocol = 0;
  std::vector<Entry> entries;
  std::string info;  // its release.json

 private:
  HMODULE mod_ = nullptr;
  bool owned_ = false;
};
enum class Result { Ok, Failed, Denied };
Result install(const Package& p, const std::wstring& dir, std::wstring& log);
Result uninstall(const std::wstring& dir, std::wstring& log);
}  // namespace pkg

}  // namespace lz
