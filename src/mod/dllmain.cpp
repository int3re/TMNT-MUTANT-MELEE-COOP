// meleenet.asi entry points.
// - InitializeASI(): called by Ultimate ASI Loader (shipped as dinput8.dll, which ztmnt.exe imports)
//   right after LoadLibrary, outside the loader lock and before the game's own code runs. The exe is
//   not packed: every patch goes in here.
// - DllMain only starts a fallback thread for loaders that never call InitializeASI: no hooks under
//   the loader lock.
#include <windows.h>

#include <cstring>
#include <string>

#include "common/log.h"
#include "common/sha256.h"
#include "common/version.h"
#include "mod/config.h"
#include "mod/diag.h"
#include "mod/game.h"
#include "mod/input.h"
#include "mod/menu.h"
#include "mod/netlobby.h"
#include "mod/netplay.h"
#include "mod/notice.h"
#include "mod/sim.h"
#include "mod/sound.h"
#include "mod/video.h"

namespace {

volatile LONG g_started = 0;
HANDLE g_init_called = nullptr;
Config g_cfg;

bool running_in_game() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  const wchar_t* name = wcsrchr(path, L'\\');
  return _wcsicmp(name ? name + 1 : path, L"ztmnt.exe") == 0;
}

void init(const char* how) {
  if (InterlockedExchange(&g_started, 1) != 0) return;

  std::wstring dir = module_dir();
  bool have_ini = load_config(dir + L"meleenet.ini", g_cfg);
  // A game started again from the network page: the log of the game before it (the page, the lobby) is kept.
  bool restarted = netplay::prepare(g_cfg);
  if (restarted) MoveFileExW((dir + L"meleenet.log").c_str(), (dir + L"meleenet-menu.log").c_str(), MOVEFILE_REPLACE_EXISTING);
  tlog::open(dir + L"meleenet.log", g_cfg.log_level, false);
  LOGI("meleenet " MELEENET_VERSION " in %ls, started by %s", dir.c_str(), how);
  if (restarted)
    LOGI("started again from the network page: %s over %s", g_cfg.net_mode == Config::Net::Host ? "the host" : "a guest",
         g_cfg.net_via.c_str());
  if (!have_ini) LOGW("meleenet.ini not found next to ztmnt.exe, using defaults");

  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  uint8_t hash[32] = {};
  bool known = false;
  if (!sha256_file(exe, hash)) {
    LOGE("cannot read %ls; game patches disabled", exe);
  } else {
    known = memcmp(hash, game::kExeSha256, 32) == 0;
    LOGI("ztmnt.exe sha256 %s (%s)", to_hex(hash, 32).c_str(), known ? "supported" : "unknown build");
    if (!known)
      LOGE("this ztmnt.exe is not the one meleenet was made for; game patches disabled (the game runs unmodified)");
  }
  diag::install(g_cfg);
  if (!known) return;
  notice::install(g_cfg);
  sound::install(g_cfg);
  netplay::install(g_cfg);
  netlobby::install(g_cfg);
  menu::install(g_cfg);
  video::install(g_cfg);
  input::install(g_cfg);
  sim::install(g_cfg);
  LOGI("meleenet ready");
}

DWORD WINAPI fallback_thread(void*) {
  if (WaitForSingleObject(g_init_called, 3000) == WAIT_TIMEOUT)
    init("DllMain fallback (the loader did not call InitializeASI; start-up patches may be late)");
  return 0;
}

}  // namespace

extern "C" __declspec(dllexport) void InitializeASI() {
  if (!running_in_game()) return;
  if (g_init_called) SetEvent(g_init_called);
  init("InitializeASI");
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    if (running_in_game()) {
      g_init_called = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      HANDLE t = CreateThread(nullptr, 0, fallback_thread, nullptr, 0, nullptr);
      if (t) CloseHandle(t);
    }
  }
  return TRUE;
}
