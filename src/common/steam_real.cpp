// Real Steam: start-up (AppID 480, delay-loaded steam_api.dll) and the
// ISteamNetworkingMessages backend of the Steam transport. Built only with the
// Steamworks SDK (steam_stub.cpp otherwise).
#include <windows.h>

#include <steam/steam_api.h>
#include <steam/isteamnetworkingmessages.h>
#include <steam/isteamnetworkingutils.h>

#include <cstddef>
#include <cstdio>
#include <deque>

#include "common/log.h"
#include "common/steam_backend.h"
#include "common/steam_boot.h"

// coop-anything's Spacewar notes: in the real 32-bit steam_api.dll the sender
// identity sits at offset 12; code that assumed 16 dropped every message as
// "from nobody". The SDK headers (SteamNetworkingIdentity is pack(1)) agree.
static_assert(sizeof(void*) != 4 || offsetof(SteamNetworkingMessage_t, m_identityPeer) == 12,
              "SteamNetworkingMessage_t layout differs from the 32-bit steam_api.dll");

namespace steam {
namespace real {
namespace {

bool g_ready = false;
std::string g_reason = "Steam ещё не запускался";

// Folder of a module, with a trailing backslash.
std::wstring dir_of(HMODULE m) {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(m, path, MAX_PATH);
  std::wstring s(path);
  size_t slash = s.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : s.substr(0, slash + 1);
}

std::wstring own_dir() {
  HMODULE self = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&own_dir), &self);
  return dir_of(self);
}

void fail(const char* log_line, const char* reason) {
  g_reason = reason;
  LOGW("Steam недоступен (Steam unavailable): %s; using ENet (LAN / IP) only", log_line);
}

// steam_api.dll is delay-loaded: load it ourselves first, from our own folder,
// so a missing or wrong DLL is a log line instead of a crash at first call.
bool load_dll() {
  std::wstring path = own_dir() + L"steam_api.dll";
  HMODULE h = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  DWORD err = h ? 0 : GetLastError();
  if (!h) h = LoadLibraryW(L"steam_api.dll");  // the usual search path
  if (!h) {
    if (err == ERROR_BAD_EXE_FORMAT)
      fail("steam_api.dll is not a 32-bit DLL (take redistributable_bin/steam_api.dll, not win64)",
           "steam_api.dll не той разрядности (нужна 32-битная)");
    else
      fail("steam_api.dll not found next to meleenet", "нет steam_api.dll рядом с meleenet");
    return false;
  }
  bool init_export = GetProcAddress(h, "SteamInternal_SteamAPI_Init") ||
                     GetProcAddress(h, "SteamAPI_Init");
  if (!init_export || !GetProcAddress(h, "SteamAPI_RunCallbacks") ||
      !GetProcAddress(h, "SteamInternal_FindOrCreateUserInterface")) {
    fail("steam_api.dll is too old for this build", "steam_api.dll слишком старая");
    return false;
  }
  return true;
}

// Steam reads the AppID from the environment first, steam_appid.txt second.
void set_env() {
  char id[16];
  snprintf(id, sizeof(id), "%u", kAppId);
  SetEnvironmentVariableA("SteamAppId", id);
  SetEnvironmentVariableA("SteamGameId", id);
  // SteamAPI_Init pulls GameOverlayRenderer.dll into the process (it hooks
  // D3D9, DirectInput and XInput regardless); with this set it draws nothing:
  // no Shift+Tab overlay, no popups over the game.
  SetEnvironmentVariableA("SteamNoOverlayUIDrawing", "1");
  std::wstring txt = dir_of(nullptr) + L"steam_appid.txt";
  if (GetFileAttributesW(txt.c_str()) != INVALID_FILE_ATTRIBUTES) return;
  HANDLE f = CreateFileW(txt.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;  // read-only folder: the variables suffice
  DWORD written = 0;
  WriteFile(f, id, (DWORD)strlen(id), &written, nullptr);
  CloseHandle(f);
}

#ifdef MELEENET_STEAM_INITEX
// No C++ objects here: __try needs a frame without unwinding.
int init_seh(SteamErrMsg* msg) {
  __try {
    return SteamAPI_InitEx(msg) == k_ESteamAPIInitResult_OK ? 1 : 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return -1;  // a delay-load failure (missing export)
  }
}
#else
int init_seh(char* msg) {
  msg[0] = '\0';
  __try {
    return SteamAPI_Init() ? 1 : 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return -1;
  }
}
#endif

bool running_seh() {
  __try {
    return SteamAPI_IsSteamRunning();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

SteamNetworkingIdentity ident(uint64_t id) {
  SteamNetworkingIdentity i;
  i.Clear();
  i.SetSteamID64(id);
  return i;
}

const char* state_name(ESteamNetworkingConnectionState s) {
  switch (s) {
    case k_ESteamNetworkingConnectionState_None: return "none";
    case k_ESteamNetworkingConnectionState_Connecting: return "connecting";
    case k_ESteamNetworkingConnectionState_FindingRoute: return "finding route";
    case k_ESteamNetworkingConnectionState_Connected: return "connected";
    case k_ESteamNetworkingConnectionState_ClosedByPeer: return "closed by peer";
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: return "problem detected locally";
    default: return "?";
  }
}

class RealMessages final : public steamnet::IMessages {
 public:
  RealMessages(ISteamNetworkingMessages* m, ISteamNetworkingUtils* u) : msgs_(m), utils_(u) {
    // Fetch the relay network config now, not on the first connection.
    utils_->InitRelayNetworkAccess();
    // A player who joins a running game gets its whole history (megabytes after half an hour): Steam's
    // 512 KB of pending reliable bytes answered k_EResultLimitExceeded and the player never got in; its
    // 256 KB/s would make them wait. The receiving side: room for the same.
    utils_->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, 8 * 1024 * 1024);
    utils_->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMin, 1024 * 1024);
    utils_->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMax, 1024 * 1024);
    utils_->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_RecvBufferSize, 8 * 1024 * 1024);
    utils_->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_RecvBufferMessages, 20000);
  }

  bool Send(uint64_t to, const void* data, uint32_t size, bool reliable, int channel, int& error) override {
    int flags = reliable ? k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_NoNagle |
                               k_nSteamNetworkingSend_AutoRestartBrokenSession
                         : k_nSteamNetworkingSend_UnreliableNoNagle;
    EResult r = msgs_->SendMessageToUser(ident(to), data, size, flags, channel);
    error = (int)r;
    return r == k_EResultOK;
  }

  int Receive(int channel, steamnet::Msg* out, int max) override {
    SteamNetworkingMessage_t* in[32];
    int n = msgs_->ReceiveMessagesOnChannel(channel, in, max < 32 ? max : 32);
    for (int i = 0; i < n; ++i) {
      const uint8_t* p = static_cast<const uint8_t*>(in[i]->m_pData);
      out[i].from = in[i]->m_identityPeer.GetSteamID64();
      out[i].data.assign(p, p + in[i]->m_cbSize);
      in[i]->Release();
    }
    return n > 0 ? n : 0;
  }

  void Accept(uint64_t peer) override { msgs_->AcceptSessionWithUser(ident(peer)); }
  void Close(uint64_t peer) override { msgs_->CloseSessionWithUser(ident(peer)); }

  bool NextSessionEvent(steamnet::SessionEvent& ev) override {
    if (events_.empty()) return false;
    ev = events_.front();
    events_.pop_front();
    return true;
  }

  steamnet::LinkStatus Status(uint64_t peer) override {
    steamnet::LinkStatus out;
    SteamNetConnectionInfo_t info{};
    SteamNetConnectionRealTimeStatus_t st{};
    ESteamNetworkingConnectionState s = msgs_->GetSessionConnectionInfo(ident(peer), &info, &st);
    out.connected = s == k_ESteamNetworkingConnectionState_Connected;
    out.relayed = (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) != 0;
    out.ping_ms = st.m_nPing;
    out.quality_local = st.m_flConnectionQualityLocal;
    out.quality_remote = st.m_flConnectionQualityRemote;
    out.out_packets_per_s = st.m_flOutPacketsPerSec;
    out.pending_reliable = st.m_cbPendingReliable;
    out.pending_unreliable = st.m_cbPendingUnreliable;
    out.unacked_reliable = st.m_cbSentUnackedReliable;
    out.state = state_name(s);
    return out;
  }

  std::string Persona(uint64_t id) override {
    const char* n = SteamFriends() ? SteamFriends()->GetFriendPersonaName(CSteamID(id)) : nullptr;
    return n && *n && std::string(n) != "[unknown]" ? n : "";
  }

  void LogNetworkState() override {
    SteamRelayNetworkStatus_t relay{};
    ESteamNetworkingAvailability avail = utils_->GetRelayNetworkStatus(&relay);
    if (avail == last_relay_) return;
    last_relay_ = avail;
    LOGI("Steam relay network: availability %d%s%s", (int)avail, relay.m_debugMsg[0] ? ", " : "",
         relay.m_debugMsg);
  }

 private:
  STEAM_CALLBACK(RealMessages, OnSessionRequest, SteamNetworkingMessagesSessionRequest_t);
  STEAM_CALLBACK(RealMessages, OnSessionFailed, SteamNetworkingMessagesSessionFailed_t);

  ISteamNetworkingMessages* msgs_;
  ISteamNetworkingUtils* utils_;
  std::deque<steamnet::SessionEvent> events_;
  ESteamNetworkingAvailability last_relay_ = k_ESteamNetworkingAvailability_Unknown;
};

void RealMessages::OnSessionRequest(SteamNetworkingMessagesSessionRequest_t* p) {
  steamnet::SessionEvent ev;
  ev.peer = p->m_identityRemote.GetSteamID64();
  events_.push_back(ev);
}

void RealMessages::OnSessionFailed(SteamNetworkingMessagesSessionFailed_t* p) {
  steamnet::SessionEvent ev;
  ev.failed = true;
  ev.peer = p->m_info.m_identityRemote.GetSteamID64();
  ev.end_reason = (int)p->m_info.m_eEndReason;
  ev.debug = p->m_info.m_szEndDebug;
  events_.push_back(ev);
}

}  // namespace

bool boot() {
  if (g_ready) return true;
  if (!load_dll()) return false;
  if (!running_seh()) {
    fail("the Steam client is not running", "Steam не запущен");
    return false;
  }
  set_env();
#ifdef MELEENET_STEAM_INITEX
  SteamErrMsg msg = {};
  int r = init_seh(&msg);
#else
  char msg[1] = {};
  int r = init_seh(msg);
#endif
  if (r != 1) {
    char line[1200];
    snprintf(line, sizeof(line), "SteamAPI_Init failed%s%s", msg[0] ? ": " : "", msg);
    fail(line, "Steam не дал подключиться (см. meleenet.log)");
    return false;
  }
  g_ready = true;
  g_reason.clear();
  LOGI("Steam ready: '%s' (SteamID64 %llu), AppID %u", my_name().c_str(),
       (unsigned long long)my_id(), SteamUtils() ? SteamUtils()->GetAppID() : 0);
  if (GetModuleHandleW(L"GameOverlayRenderer.dll"))
    LOGI("Steam overlay DLL loaded by Steam, UI drawing disabled (SteamNoOverlayUIDrawing)");
  return true;
}

void pump() {
  if (g_ready) SteamAPI_RunCallbacks();
}

void shutdown() {
  if (!g_ready) return;
  g_ready = false;
  SteamAPI_Shutdown();
  LOGI("Steam shut down");
}

std::string unavailable_reason() { return g_reason; }

uint64_t my_id() {
  return g_ready && SteamUser() ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
}

std::string my_name() {
  const char* n = g_ready && SteamFriends() ? SteamFriends()->GetPersonaName() : nullptr;
  return n ? n : "";
}

std::unique_ptr<steamnet::IMessages> messages() {
  ISteamNetworkingMessages* m = g_ready ? SteamNetworkingMessages() : nullptr;
  ISteamNetworkingUtils* u = g_ready ? SteamNetworkingUtils() : nullptr;
  if (!m || !u) {
    LOGE("Steam networking interfaces unavailable (steam_api.dll too old?)");
    return nullptr;
  }
  return std::make_unique<RealMessages>(m, u);
}

}  // namespace real
}  // namespace steam
