// Lab mock of the Steam calls meleenet uses ([net] transport=mock), so the Steam
// code paths (transport_steam.cpp, the host and client logic) run with two or
// more players on one PC, with one Steam account or none. Pattern from
// coop-anything's Spacewar notes ("mock it in the mod, not the DLL"):
//   - fake SteamID per [net] mock_slot (account 900000000 + slot);
//   - messages are localhost UDP datagrams, port 47900 + slot;
//   - like Steam, a stranger's messages are held until Accept, or until we
//     have sent to them first;
//   - lobbies, members, presence and invites are small files in mock_dir with
//     a heartbeat, so a crashed process's lobby goes stale by itself;
//   - optional loss of unreliable messages and a fixed one-way latency.
// It tests meleenet's logic, not Steam itself: the real relay, NAT traversal and
// steam_api.dll still need two real accounts.
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <random>
#include <vector>

#include "common/log.h"
#include "common/net.h"
#include "common/protocol.h"
#include "common/steam_backend.h"
#include "common/steam_boot.h"

namespace steam {
namespace mock {
namespace {

constexpr uint64_t kIndividualBase = 76561197960265728ull;  // SteamID64 of account 0
constexpr uint64_t kAccountBase = 900000000ull;             // reserved range for fakes
constexpr uint64_t kLobbyBase = 0x0186000000000000ull;      // chat-type id, public universe
constexpr uint16_t kPortBase = 47900;
constexpr uint32_t kMagic = 0x4B534D54;  // 'TMSK'
constexpr uint64_t kStaleMs = 6000;      // heartbeat files older than this are gone
constexpr uint32_t kHeartbeatMs = 1500;
constexpr uint32_t kNoAnswerMs = 10000;  // outgoing session, nothing heard: failed

struct State {
  bool ready = false;
  int slot = 1;
  uint64_t id = 0;
  std::wstring dir;  // with trailing backslash
  int loss_pct = 0, latency_ms = 0, send_rate = 0;
  bool use_lobby = true;
  int member_delay_ms = 0;  // the host sees us in the lobby this much later
  uint32_t last_presence = 0;
} g;

uint64_t id_of_slot(int slot) { return kIndividualBase + kAccountBase + (uint64_t)slot; }
int slot_of(uint64_t id) {
  uint64_t a = id - kIndividualBase - kAccountBase;
  return a >= 1 && a <= 99 ? (int)a : 0;
}
std::string name_of(uint64_t id) {
  int s = slot_of(id);
  return s ? "mock" + std::to_string(s) : "";
}

uint64_t wall_ms() {  // shared by every process, unlike net::now_ms()
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  return ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000;
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// --- small key=value files with a heartbeat ----------------------------------------

using Kv = std::map<std::string, std::string>;

bool write_kv(const std::wstring& path, Kv kv) {
  kv["beat"] = std::to_string(wall_ms());
  std::string text;
  for (const auto& e : kv) text += e.first + "=" + e.second + "\n";
  std::wstring tmp = path + L".tmp" + std::to_wstring(GetCurrentProcessId());
  HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD n = 0;
  WriteFile(f, text.data(), (DWORD)text.size(), &n, nullptr);
  CloseHandle(f);
  return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool read_kv(const std::wstring& path, Kv& kv, bool need_fresh = true) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  char buf[4096];
  DWORD n = 0;
  ReadFile(f, buf, sizeof(buf) - 1, &n, nullptr);
  CloseHandle(f);
  buf[n] = '\0';
  kv.clear();
  char* ctx = nullptr;
  for (char* line = strtok_s(buf, "\n", &ctx); line; line = strtok_s(nullptr, "\n", &ctx)) {
    char* eq = strchr(line, '=');
    if (eq) kv[std::string(line, eq)] = eq + 1;
  }
  if (!need_fresh) return true;
  uint64_t beat = std::strtoull(kv["beat"].c_str(), nullptr, 10);
  return wall_ms() - beat < kStaleMs;
}

std::vector<std::wstring> list(const std::wstring& pattern) {
  std::vector<std::wstring> out;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((g.dir + pattern).c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    out.push_back(g.dir + fd.cFileName);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return out;
}

std::wstring lobby_path(uint64_t lobby) { return g.dir + L"lobby-" + std::to_wstring(lobby) + L".txt"; }
std::wstring member_path(uint64_t lobby, uint64_t who) {
  return g.dir + L"member-" + std::to_wstring(lobby) + L"-" + std::to_wstring(who) + L".txt";
}
std::wstring presence_path(uint64_t who) { return g.dir + L"presence-" + std::to_wstring(who) + L".txt"; }
std::wstring invite_path(uint64_t who) { return g.dir + L"invite-" + std::to_wstring(who) + L".txt"; }

std::vector<uint64_t> fresh_members(uint64_t lobby) {
  std::vector<uint64_t> out;
  std::wstring prefix = L"member-" + std::to_wstring(lobby) + L"-*.txt";
  for (const std::wstring& p : list(prefix)) {
    Kv kv;
    if (read_kv(p, kv)) out.push_back(std::strtoull(kv["id"].c_str(), nullptr, 10));
  }
  return out;
}

// --- messages ------------------------------------------------------------------------

#pragma pack(push, 1)
struct Wire {
  uint32_t magic;
  uint64_t from;
  uint8_t channel;
  uint8_t reliable;
};
#pragma pack(pop)

class MockMessages final : public steamnet::IMessages {
 public:
  MockMessages() : rng_((uint32_t)g.id ^ GetTickCount()) {}
  ~MockMessages() override {
    if (sock_ != INVALID_SOCKET) closesocket(sock_);
  }

  bool Open() {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) return false;
    u_long nb = 1;
    ioctlsocket(sock_, FIONBIO, &nb);
    BOOL off = FALSE;
    DWORD ret = 0;
    WSAIoctl(sock_, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &off, sizeof(off), nullptr, 0, &ret,
             nullptr, nullptr);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)(kPortBase + g.slot));
    if (bind(sock_, (sockaddr*)&a, sizeof(a)) != 0) {
      LOGE("Steam mock: UDP 127.0.0.1:%u is taken (another mock with slot %d?)", kPortBase + g.slot, g.slot);
      return false;
    }
    return true;
  }

  bool Send(uint64_t to, const void* data, uint32_t size, bool reliable, int channel, int& error) override {
    error = 1;  // k_EResultOK
    int slot = slot_of(to);
    if (!slot) {
      error = 8;  // k_EResultInvalidParam
      return false;
    }
    Session& s = sessions_[to];
    if (!s.accepted) {  // sending first opens the session from our side
      s.accepted = true;
      s.outgoing = true;
      s.first_sent = net::now_ms();
      release_held(to, s);
    }
    if (!reliable && g.loss_pct && (int)(rng_() % 100) < g.loss_pct) return true;  // lost on the way
    if (reliable && g.send_rate > 0) {  // Steam's send buffer: full means k_EResultLimitExceeded, nothing queued
      uint32_t now = net::now_ms();
      // (a small rate: a small buffer, for short tests; and never more at once than the mock's UDP socket
      // holds, 64 KB: what does not fit there is lost, which real Steam's reliable messages never are)
      const double burst = std::min(32.0 * 1024, std::max(8.0 * 1024, (double)g.send_rate));
      if (!bucket_at_) {
        bucket_at_ = now;
        bucket_ = burst;
      }
      bucket_ = std::min(burst, bucket_ + g.send_rate * (double)(now - bucket_at_) / 1000.0);
      bucket_at_ = now;
      if (bucket_ < (double)size) {
        error = 25;  // k_EResultLimitExceeded
        return false;
      }
      bucket_ -= (double)size;
    }
    Outgoing o;
    o.due = net::now_ms() + (uint32_t)g.latency_ms;
    o.port = (uint16_t)(kPortBase + slot);
    o.bytes.resize(sizeof(Wire) + size);
    Wire w{kMagic, g.id, (uint8_t)channel, (uint8_t)(reliable ? 1 : 0)};
    std::memcpy(o.bytes.data(), &w, sizeof(w));
    std::memcpy(o.bytes.data() + sizeof(w), data, size);
    out_.push_back(std::move(o));
    flush();
    return true;
  }

  int Receive(int channel, steamnet::Msg* out, int max) override {
    pump();
    std::deque<steamnet::Msg>& q = queues_[channel];
    int n = 0;
    while (n < max && !q.empty()) {
      out[n++] = std::move(q.front());
      q.pop_front();
    }
    return n;
  }

  void Accept(uint64_t peer) override {
    Session& s = sessions_[peer];
    s.accepted = true;
    release_held(peer, s);
  }

  void Close(uint64_t peer) override { sessions_.erase(peer); }

  bool NextSessionEvent(steamnet::SessionEvent& ev) override {
    pump();
    if (events_.empty()) return false;
    ev = events_.front();
    events_.pop_front();
    return true;
  }

  steamnet::LinkStatus Status(uint64_t peer) override {
    steamnet::LinkStatus st;
    auto it = sessions_.find(peer);
    st.connected = it != sessions_.end() && it->second.heard && net::now_ms() - it->second.last_heard < 3000;
    st.ping_ms = st.connected ? 2 * g.latency_ms : -1;
    st.state = st.connected ? "connected (mock)" : "none (mock)";
    return st;
  }

  std::string Persona(uint64_t id) override { return name_of(id); }

 private:
  struct Session {
    bool accepted = false, outgoing = false, requested = false, heard = false, failed = false;
    uint32_t first_sent = 0, last_heard = 0;
    std::vector<std::pair<int, std::vector<uint8_t>>> held;  // channel, payload
  };
  struct Outgoing {
    uint32_t due = 0;
    uint16_t port = 0;
    std::vector<uint8_t> bytes;
  };

  void release_held(uint64_t peer, Session& s) {
    for (auto& h : s.held) queues_[h.first].push_back(steamnet::Msg{peer, std::move(h.second)});
    s.held.clear();
  }

  void flush() {
    uint32_t now = net::now_ms();
    while (!out_.empty() && (int32_t)(now - out_.front().due) >= 0) {
      sockaddr_in a{};
      a.sin_family = AF_INET;
      a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      a.sin_port = htons(out_.front().port);
      sendto(sock_, (const char*)out_.front().bytes.data(), (int)out_.front().bytes.size(), 0, (sockaddr*)&a,
             sizeof(a));
      out_.pop_front();
    }
  }

  void pump() {
    flush();
    uint8_t buf[65536];  // (a reliable message may be bigger than a datagram of real Steam: the progress record)
    for (;;) {
      sockaddr_in from{};
      int len = sizeof(from);
      int n = recvfrom(sock_, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &len);
      if (n < (int)sizeof(Wire)) break;
      Wire w;
      std::memcpy(&w, buf, sizeof(w));
      if (w.magic != kMagic) continue;
      Session& s = sessions_[w.from];
      s.heard = true;
      s.last_heard = net::now_ms();
      std::vector<uint8_t> payload(buf + sizeof(w), buf + n);
      if (s.accepted) {
        queues_[w.channel].push_back(steamnet::Msg{w.from, std::move(payload)});
      } else {
        s.held.emplace_back((int)w.channel, std::move(payload));
        if (!s.requested) {
          s.requested = true;
          steamnet::SessionEvent ev;
          ev.peer = w.from;
          events_.push_back(ev);
        }
      }
    }
    // An outgoing session that never got an answer fails, as on Steam.
    uint32_t now = net::now_ms();
    for (auto& e : sessions_) {
      Session& s = e.second;
      if (s.outgoing && !s.heard && !s.failed && now - s.first_sent > kNoAnswerMs) {
        s.failed = true;
        steamnet::SessionEvent ev;
        ev.failed = true;
        ev.peer = e.first;
        ev.end_reason = 4001;  // k_ESteamNetConnectionEnd_Remote_Timeout
        ev.debug = "mock: no answer";
        events_.push_back(ev);
      }
    }
  }

  SOCKET sock_ = INVALID_SOCKET;
  std::mt19937 rng_;
  uint32_t bucket_at_ = 0;  // send_rate: the reliable bytes that may go now
  double bucket_ = 0;
  std::map<uint64_t, Session> sessions_;
  std::map<int, std::deque<steamnet::Msg>> queues_;
  std::deque<steamnet::SessionEvent> events_;
  std::deque<Outgoing> out_;
};

// --- lobbies -------------------------------------------------------------------------

class Host final : public lobby::IHost {
 public:
  Host(const Config& cfg, const std::string& exe16) {
    static int counter = 0;
    lobby_ = kLobbyBase + kAccountBase + (uint64_t)g.slot * 100 + (uint64_t)(++counter % 100);
    kv_["meleenet"] = "1";
    kv_["proto"] = std::to_string(mmp::kVersion);
    kv_["exe"] = exe16;
    kv_["host"] = name_of(g.id);
    kv_["hostid"] = std::to_string(g.id);
    kv_["name"] = cfg.net_name;
    kv_["type"] = cfg.net_lobby;
    kv_["state"] = "lobby";
    write_kv(lobby_path(lobby_), kv_);
    last_beat_ = net::now_ms();
    LOGI("Steam lobby %llu created (mock, %s, 2 places); host SteamID64 %llu", (unsigned long long)lobby_,
         cfg.net_lobby.c_str(), (unsigned long long)g.id);
  }
  ~Host() override {
    DeleteFileW(lobby_path(lobby_).c_str());
    for (const std::wstring& p : list(L"member-" + std::to_wstring(lobby_) + L"-*.txt")) DeleteFileW(p.c_str());
    LOGI("Steam lobby %llu closed (mock)", (unsigned long long)lobby_);
  }

  void Tick(uint32_t now) override {
    if (now - last_beat_ < kHeartbeatMs) return;
    last_beat_ = now;
    write_kv(lobby_path(lobby_), kv_);
    std::vector<uint64_t> now_in = fresh_members(lobby_);
    for (uint64_t m : members_)
      if (std::find(now_in.begin(), now_in.end(), m) == now_in.end()) {
        LOGI("Steam lobby: '%s' (%llu) left (mock)", name_of(m).c_str(), (unsigned long long)m);
        left_.push_back(m);
      }
    for (uint64_t m : now_in)
      if (std::find(members_.begin(), members_.end(), m) == members_.end())
        LOGI("Steam lobby: '%s' (%llu) joined (mock)", name_of(m).c_str(), (unsigned long long)m);
    members_ = now_in;
  }

  bool IsMember(uint64_t id) const override {
    // Read the files now: a member that joined a moment ago counts at once.
    for (uint64_t m : fresh_members(lobby_))
      if (m == id) return true;
    return false;
  }

  void SetInGame(bool has_p2) override {
    kv_["state"] = has_p2 ? "ingame" : "lobby";
    write_kv(lobby_path(lobby_), kv_);
  }

  uint64_t LobbyId() const override { return lobby_; }

  bool TakeLeft(uint64_t& id) override {
    if (left_.empty()) return false;
    id = left_.front();
    left_.erase(left_.begin());
    return true;
  }

  std::vector<lobby::Friend> OnlineFriends() override {
    std::vector<lobby::Friend> out;
    for (const std::wstring& p : list(L"presence-*.txt")) {
      Kv kv;
      if (!read_kv(p, kv)) continue;
      uint64_t id = std::strtoull(kv["id"].c_str(), nullptr, 10);
      if (id == g.id) continue;
      out.push_back(lobby::Friend{id, name_of(id), true});
    }
    return out;
  }

  void Invite(uint64_t friend_id) override {
    write_kv(invite_path(friend_id), Kv{{"lobby", std::to_string(lobby_)}, {"from", std::to_string(g.id)}});
    LOGI("Steam: invite to '%s' (%llu) sent (mock)", name_of(friend_id).c_str(), (unsigned long long)friend_id);
  }

 private:
  uint64_t lobby_ = 0;
  Kv kv_;
  uint32_t last_beat_ = 0;
  std::vector<uint64_t> members_, left_;
};

class Browser final : public lobby::IBrowser {
 public:
  explicit Browser(const std::string& my_exe16) : my_exe16_(my_exe16) {}
  ~Browser() override { Leave(); }

  void Tick(uint32_t now, bool searching) override {
    // Real Steam makes no promise which comes first at the host: our lobby
    // membership or our first message. mock_member_delay_ms tests the latter.
    if (joined_ && (int32_t)(now - member_at_) >= 0 && now - last_beat_ >= kHeartbeatMs) {
      last_beat_ = now;
      write_kv(member_path(joined_, g.id), Kv{{"id", std::to_string(g.id)}});
    }
    if (now - last_scan_ < 1000) return;
    last_scan_ = now;
    Kv inv;  // an invite works whether or not the list is shown, like Steam's
    if (read_kv(invite_path(g.id), inv)) {
      DeleteFileW(invite_path(g.id).c_str());
      uint64_t lobby = std::strtoull(inv["lobby"].c_str(), nullptr, 10);
      LOGI("Steam: lobby %llu from '%s' (invite, mock)", (unsigned long long)lobby,
           name_of(std::strtoull(inv["from"].c_str(), nullptr, 10)).c_str());
      Join(lobby);
    }
    if (!searching) return;
    list_.clear();
    for (const std::wstring& p : list(L"lobby-*.txt")) {
      Kv kv;
      if (!read_kv(p, kv) || kv["meleenet"] != "1") continue;
      lobby::Found f;
      f.lobby = lobby_from_path(p);
      f.host_id = std::strtoull(kv["hostid"].c_str(), nullptr, 10);
      f.host = kv["host"];
      f.name = kv["name"];
      f.state = kv["state"];
      f.is_friend = kv["type"] != "public";
      f.seen_ms = now;
      if (kv["proto"] != std::to_string(mmp::kVersion))
        f.why_not = "другая версия meleenet (протокол " + kv["proto"] + ")";
      else if (!my_exe16_.empty() && !kv["exe"].empty() && kv["exe"] != my_exe16_)
        f.why_not = "другой ztmnt.exe";
      else if (f.host_id == g.id)
        f.why_not = "это вы";
      else
        f.compatible = true;
      list_.push_back(f);
    }
  }

  std::vector<lobby::Found> List() const override { return list_; }

  void Join(uint64_t lobby) override {
    Leave();
    LOGI("Steam: joining lobby %llu (mock)", (unsigned long long)lobby);
    Kv kv;
    if (!read_kv(lobby_path(lobby), kv)) {
      finish(false, lobby, 0, "Лобби больше нет: хост вышел из игры.");
      return;
    }
    if (kv["meleenet"] != "1") {
      finish(false, lobby, 0, "Это не лобби meleenet: игрок в Steam запустил что-то другое.");
      return;
    }
    std::vector<uint64_t> members = fresh_members(lobby);
    size_t others = 0;  // (the host is not a member file: three guests, as a real lobby of four)
    for (uint64_t m : members) others += m != g.id ? 1 : 0;
    if (others >= 3) {
      finish(false, lobby, 0, "В лобби уже четыре игрока.");
      return;
    }
    joined_ = lobby;
    member_at_ = net::now_ms() + (uint32_t)g.member_delay_ms;
    last_beat_ = member_at_ - kHeartbeatMs;  // first beat as soon as member_at_ passes
    if (!g.member_delay_ms) write_kv(member_path(lobby, g.id), Kv{{"id", std::to_string(g.id)}});
    uint64_t host = std::strtoull(kv["hostid"].c_str(), nullptr, 10);
    LOGI("Steam: in lobby %llu, host '%s' (%llu) (mock)", (unsigned long long)lobby, name_of(host).c_str(),
         (unsigned long long)host);
    finish(true, lobby, host, "");
  }

  void JoinHost(uint64_t id) override {
    if (g.use_lobby) {
      for (const std::wstring& p : list(L"lobby-*.txt")) {
        Kv kv;
        if (read_kv(p, kv) && std::strtoull(kv["hostid"].c_str(), nullptr, 10) == id) {
          LOGI("Steam: host %llu is in lobby %llu (mock)", (unsigned long long)id,
               (unsigned long long)lobby_from_path(p));
          Join(lobby_from_path(p));
          return;
        }
      }
    }
    LOGI("Steam: host %llu has no visible lobby; direct session (the host needs allow_steamid)",
         (unsigned long long)id);
    Leave();
    finish(true, 0, id, "");
  }

  void Leave() override {
    if (!joined_) return;
    DeleteFileW(member_path(joined_, g.id).c_str());
    LOGI("Steam: left lobby %llu (mock)", (unsigned long long)joined_);
    joined_ = 0;
  }

  bool TakeJoinResult(lobby::JoinResult& out) override {
    if (!have_result_) return false;
    out = result_;
    have_result_ = false;
    return true;
  }

 private:
  static uint64_t lobby_from_path(const std::wstring& p) {
    size_t a = p.rfind(L"lobby-");
    return a == std::wstring::npos ? 0 : std::wcstoull(p.c_str() + a + 6, nullptr, 10);
  }

  void finish(bool ok, uint64_t lobby, uint64_t host_id, const std::string& error) {
    result_ = lobby::JoinResult{};
    result_.ok = ok;
    result_.lobby = lobby;
    result_.host_id = host_id;
    result_.host = name_of(host_id);
    result_.error = error;
    have_result_ = true;
    if (!ok) LOGW("Steam: cannot join lobby %llu: %s", (unsigned long long)lobby, error.c_str());
  }

  std::string my_exe16_;
  std::vector<lobby::Found> list_;
  uint64_t joined_ = 0;
  uint32_t last_beat_ = 0, last_scan_ = 0, member_at_ = 0;
  lobby::JoinResult result_;
  bool have_result_ = false;
};

}  // namespace

bool boot(const Config& cfg) {
  if (g.ready) return true;
  g.slot = cfg.mock_slot;
  g.id = id_of_slot(g.slot);
  g.loss_pct = cfg.mock_loss_pct;
  g.latency_ms = cfg.mock_latency_ms;
  g.send_rate = cfg.mock_send_rate;
  g.use_lobby = cfg.mock_lobby;
  g.member_delay_ms = cfg.mock_member_delay_ms;
  std::wstring dir = cfg.mock_dir.empty() ? std::wstring() : widen(cfg.mock_dir);
  if (dir.empty()) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    dir = std::wstring(tmp) + L"meleenet-mock-steam";
  }
  if (dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
  CreateDirectoryW(dir.c_str(), nullptr);
  g.dir = dir;
  g.ready = true;
  write_kv(presence_path(g.id), Kv{{"id", std::to_string(g.id)}});
  g.last_presence = net::now_ms();
  LOGW("Steam MOCK (lab only): '%s' (SteamID64 %llu), UDP 127.0.0.1:%u, loss %d%%, latency %d ms, dir %ls",
       name_of(g.id).c_str(), (unsigned long long)g.id, kPortBase + g.slot, g.loss_pct, g.latency_ms,
       g.dir.c_str());
  return true;
}

void pump() {
  if (!g.ready || net::now_ms() - g.last_presence < kHeartbeatMs) return;
  g.last_presence = net::now_ms();
  write_kv(presence_path(g.id), Kv{{"id", std::to_string(g.id)}});
}

void shutdown() {
  if (!g.ready) return;
  DeleteFileW(presence_path(g.id).c_str());
  g.ready = false;
  LOGI("Steam mock shut down");
}

uint64_t my_id() { return g.ready ? g.id : 0; }
std::string my_name() { return g.ready ? name_of(g.id) : ""; }

std::unique_ptr<steamnet::IMessages> messages() {
  auto m = std::make_unique<MockMessages>();
  if (!m->Open()) return nullptr;
  return m;
}

std::unique_ptr<lobby::IHost> host(const Config& cfg, const std::string& exe16) {
  return std::make_unique<Host>(cfg, exe16);
}

std::unique_ptr<lobby::IBrowser> browser(const std::string& my_exe16) {
  return std::make_unique<Browser>(my_exe16);
}

}  // namespace mock
}  // namespace steam
