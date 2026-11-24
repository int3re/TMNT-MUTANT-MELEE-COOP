#include "mod/netplay.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/log.h"
#include "common/net.h"
#include "common/protocol.h"
#include "common/sha256.h"
#include "common/steam_boot.h"
#include "common/transport.h"
#include "common/transport_enet.h"
#include "common/transport_steam.h"
#include "common/version.h"
#include "mod/game.h"
#include "mod/input.h"
#include "mod/multi_transport.h"
#include "mod/notice.h"
#include "mod/patch.h"
#include "mod/sim.h"

namespace netplay {
namespace {

using transport::Channel;
using transport::Event;
using transport::EventType;
using transport::PeerId;

enum class Phase { Off, Meeting, Ready, Playing, Ended };

constexpr int kMax = mmp::kPlayers;

Config g_cfg;
Phase g_phase = Phase::Off;
bool g_session_game = false;
std::unique_ptr<transport::ITransport> g_t;
bool g_host = false;
int g_me = 0;
int g_players = 1;
int g_delay = 2;
uint32_t g_first = 1;  // the first frame of the session
uint32_t g_frame = 0;  // the frame being played
uint8_t g_exe_hash[8] = {};
uint8_t g_data_hash[8] = {};
std::string g_names[kMax];
PeerId g_peer[kMax];      // host: the guests by player number; guest: [0] = the host
bool g_gone[kMax] = {};   // the player has left the session
uint32_t g_last_rx[kMax] = {};

// Pads by frame: g_pads[player][frame - g_first].
std::vector<input::Pad> g_pads[kMax];
uint32_t g_newest[kMax] = {};   // host: the last frame known of each player; guest: [g_me] = the last frame read here
uint32_t g_bundle = 0;          // guest: the last frame whose pads (of everybody) the host has sent
uint32_t g_host_has = 0;        // guest: the newest frame of this PC's pads the host has confirmed
uint32_t g_guest_has[kMax] = {}; // host: the newest bundle frame each guest has confirmed
input::Pad g_held[kMax];        // the pads of the last frame played
uint32_t g_started_ms = 0;

std::map<uint32_t, uint32_t> g_hash_mine;
bool g_desync = false;
uint32_t g_waited_ms = 0, g_waits = 0;

// The link's keeper. The game's thread answers the network once a frame; a frame that loads a level
// takes seconds, and so does a window being dragged. A second thread then answers in its place: it
// runs the transport (pings are answered, packets are taken into its queue) and says "here" twice a
// second. Everything that touches the transport holds g_net_lock.
CRITICAL_SECTION g_net_lock;
std::atomic<uint32_t> g_pumped_ms{0};
std::atomic<bool> g_keeper_on{false};
std::atomic<uint32_t> g_kept{0};

struct NetLock {
  NetLock() { EnterCriticalSection(&g_net_lock); }
  ~NetLock() { LeaveCriticalSection(&g_net_lock); }
};

// --- small helpers --------------------------------------------------------------------
void set_pad(int player, uint32_t frame, const input::Pad& pad) {
  if (frame < g_first) return;
  size_t i = frame - g_first;
  if (g_pads[player].size() <= i) g_pads[player].resize(i + 1);
  g_pads[player][i] = pad;
}

input::Pad get_pad(int player, uint32_t frame) {
  if (frame < g_first) return {};
  size_t i = frame - g_first;
  return i < g_pads[player].size() ? g_pads[player][i] : input::Pad{};
}

mmp::PadWire to_wire(const input::Pad& p) { return {p.x, p.y, p.buttons}; }

input::Pad from_wire(const mmp::PadWire& w) {
  input::Pad p;
  p.x = w.x, p.y = w.y, p.buttons = w.buttons;
  return p;
}

bool send_to(const PeerId& peer, const void* data, size_t size, Channel ch) {
  NetLock lock;
  return g_t && peer.valid() && g_t->Send(peer, data, size, ch);
}

template <typename T>
bool send(const PeerId& peer, const T& packet, Channel ch) {
  return send_to(peer, &packet, sizeof(packet), ch);
}

void copy_name(char* to, size_t n, const std::string& s) {
  memset(to, 0, n);
  memcpy(to, s.data(), s.size() < n - 1 ? s.size() : n - 1);
}

int player_of(const PeerId& peer) {
  if (!g_host) return peer == g_peer[0] ? 0 : -1;
  for (int p = 1; p < g_players; ++p)
    if (g_peer[p] == peer) return p;
  return -1;
}

// The window's messages while the game's own loop is held.
bool pump_messages() {
  MSG msg;
  while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      PostQuitMessage(static_cast<int>(msg.wParam));
      return false;
    }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  return *reinterpret_cast<const uint8_t*>(game::kQuit) == 0;
}

void say_bye() {
  if (!g_t) return;
  auto bye = mmp::make<mmp::Start>(mmp::Type::Bye);
  for (int p = 0; p < kMax; ++p)
    if (g_peer[p].valid() && !g_gone[p]) send(g_peer[p], bye, Channel::Control);
  NetLock lock;
  g_t->Poll(20);
}

void end_session(const char* why) {
  if (g_phase == Phase::Off || g_phase == Phase::Ended) return;
  LOGW("net: the session is over: %s (frame %u; waited %u ms in %u waits; the keeper stood in %u times)", why, g_frame,
       g_waited_ms, g_waits, g_kept.load());
  g_keeper_on = false;
  if (strcmp(why, "this game is closing") != 0 && g_phase == Phase::Playing)
    notice::say(g_host ? L"СЕТЕВАЯ ИГРА ОКОНЧЕНА" : L"СВЯЗЬ С ХОСТОМ ПОТЕРЯНА\nДАЛЬШЕ ВЫ ИГРАЕТЕ ОДИН", 10);
  say_bye();
  g_phase = Phase::Ended;
}

// A guest has left (host): the session goes on without it, its pad is empty from now on.
void drop(int player, const char* why) {
  if (g_gone[player]) return;
  g_gone[player] = true;
  LOGW("net: %s (player %d) has left: %s (frame %u)", g_names[player].c_str(), player + 1, why, g_frame);
  wchar_t text[96];
  swprintf(text, 96, L"ИГРОК %d ВЫШЕЛ ИЗ ИГРЫ", player + 1);
  notice::say(text, 8);
  bool anybody = false;
  for (int p = 1; p < g_players; ++p) anybody = anybody || !g_gone[p];
  if (!anybody) end_session("every guest has left");
}

// [debug] net_loss / net_outage: a bad link for the lab.
bool lab_drops() {
  if (g_cfg.net_outage_ms) {
    uint32_t t = net::now_ms() - g_started_ms;
    if (t >= g_cfg.net_outage_at_ms && t < g_cfg.net_outage_at_ms + g_cfg.net_outage_ms) return true;
  }
  if (!g_cfg.net_loss) return false;
  static uint32_t seed = GetTickCount() | 1;
  seed = seed * 1664525u + 1013904223u;
  return (seed >> 16) % 100 < static_cast<uint32_t>(g_cfg.net_loss);
}

// --- the host's save: every game of the session plays on it ------------------------------------------
char g_save_name[] = "tmnt-net.sv";

std::vector<uint8_t> read_file(const std::wstring& path, bool& exists) {
  std::vector<uint8_t> data;
  exists = false;
  FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return data;
  exists = true;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n > 0 && n <= mmp::kSaveMax) {
    data.resize(static_cast<size_t>(n));
    if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
  }
  fclose(f);
  return data;
}

// A guest's game reads and writes tmnt-net.sv where the game says tmnt.sv: its own save is never touched.
void use_session_save() {
  static const uintptr_t sites[] = {game::kSaveName1, game::kSaveName2, game::kSaveName3, game::kSaveName4};
  uint32_t old_name = static_cast<uint32_t>(game::kSaveNameText);
  uint32_t mine = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_save_name));
  int done = 0;
  for (uintptr_t site : sites)
    if (patch::bytes_are(site, &old_name, 4) && patch::write(site, &mine, 4)) ++done;
  if (done != 4) LOGE("net: the game's save file name is not where it should be (%d of 4 places)", done);
}

// --- packets ------------------------------------------------------------------------------
// Guest -> host: every pad of this PC the host has not confirmed.
void send_inputs() {
  if (g_phase != Phase::Playing || g_host) return;
  uint8_t buf[sizeof(mmp::Input) + mmp::kInputMax * sizeof(mmp::PadWire)];
  auto in = mmp::make<mmp::Input>(mmp::Type::Input);
  uint32_t newest = g_newest[g_me];
  if (newest < g_first) return;
  uint32_t from = g_host_has >= g_first ? g_host_has + 1 : g_first;
  if (from > newest) from = newest;  // (at least the newest pad: it carries the acknowledgement)
  uint32_t count = newest - from + 1;
  if (count > mmp::kInputMax) {
    newest = from + mmp::kInputMax - 1;  // the oldest unconfirmed ones first
    count = mmp::kInputMax;
  }
  in.player = static_cast<uint8_t>(g_me);
  in.count = static_cast<uint8_t>(count);
  in.newest = newest;
  in.ack = g_bundle;
  memcpy(buf, &in, sizeof(in));
  for (uint32_t i = 0; i < count; ++i) {
    mmp::PadWire w = to_wire(get_pad(g_me, newest - count + 1 + i));
    memcpy(buf + sizeof(in) + i * sizeof(w), &w, sizeof(w));
  }
  if (!lab_drops()) send_to(g_peer[0], buf, sizeof(in) + count * sizeof(mmp::PadWire), Channel::Input);
}

// Host: the last frame for which every player's pad is known.
uint32_t complete() {
  uint32_t c = 0xFFFFFFFFu;
  for (int p = 0; p < g_players; ++p)
    if (!g_gone[p] && g_newest[p] < c) c = g_newest[p];
  return c == 0xFFFFFFFFu ? g_newest[0] : c;
}

// Host -> each guest: every frame's pads (of all players) that guest has not confirmed.
void send_bundles() {
  if (g_phase != Phase::Playing || !g_host) return;
  uint32_t last = complete();
  if (last < g_first) return;
  uint8_t gone = 0;
  for (int p = 0; p < g_players; ++p)
    if (g_gone[p]) gone |= static_cast<uint8_t>(1 << p);
  static uint8_t buf[sizeof(mmp::Bundle) + mmp::kInputMax * kMax * sizeof(mmp::PadWire)];
  for (int c = 1; c < g_players; ++c) {
    if (g_gone[c] || !g_peer[c].valid()) continue;
    uint32_t from = g_guest_has[c] >= g_first ? g_guest_has[c] + 1 : g_first;
    uint32_t newest = last;
    if (from > newest) from = newest;
    uint32_t count = newest - from + 1;
    if (count > mmp::kInputMax) {
      newest = from + mmp::kInputMax - 1;
      count = mmp::kInputMax;
    }
    auto b = mmp::make<mmp::Bundle>(mmp::Type::Bundle);
    b.players = static_cast<uint8_t>(g_players);
    b.count = static_cast<uint8_t>(count);
    b.gone = gone;
    b.newest = newest;
    b.ack = g_newest[c];
    memcpy(buf, &b, sizeof(b));
    size_t at = sizeof(b);
    for (uint32_t i = 0; i < count; ++i)
      for (int p = 0; p < g_players; ++p) {
        mmp::PadWire w = to_wire(get_pad(p, newest - count + 1 + i));
        memcpy(buf + at, &w, sizeof(w));
        at += sizeof(w);
      }
    if (!lab_drops()) send_to(g_peer[c], buf, at, Channel::Input);
  }
}

void desync(int player, uint32_t frame, uint32_t mine, uint32_t theirs) {
  if (g_desync) return;
  g_desync = true;
  LOGE("net: DESYNC at frame %u: %s's game %08X, this game %08X", frame, g_names[player].c_str(), theirs, mine);
  notice::say(L"ИГРЫ РАЗОШЛИСЬ\nНАЧНИТЕ СЕТЕВУЮ ИГРУ ЗАНОВО", 15);
}

void on_data(const Event& ev) {
  const uint8_t* d = ev.data.data();
  size_t n = ev.data.size();
  uint8_t type = mmp::type_of(d, n);
  if (!type) return;
  int from = player_of(ev.peer);
  if (from < 0) return;
  g_last_rx[from] = net::now_ms();
  switch (static_cast<mmp::Type>(type)) {
    case mmp::Type::Input: {
      if (!g_host || n < sizeof(mmp::Input)) return;
      mmp::Input in;
      memcpy(&in, d, sizeof(in));
      if (in.player != from) return;
      if (n < sizeof(in) + in.count * sizeof(mmp::PadWire) || in.count > in.newest + 1) return;
      if (in.ack > g_guest_has[from]) g_guest_has[from] = in.ack;
      for (uint32_t i = 0; i < in.count; ++i) {
        uint32_t frame = in.newest - in.count + 1 + i;
        if (frame < g_first || (g_newest[from] >= g_first && frame <= g_newest[from])) continue;  // known already
        if (frame != (g_newest[from] >= g_first ? g_newest[from] + 1 : g_first)) break;            // a gap: wait
        mmp::PadWire w;
        memcpy(&w, d + sizeof(in) + i * sizeof(w), sizeof(w));
        set_pad(from, frame, from_wire(w));
        g_newest[from] = frame;
      }
      break;
    }
    case mmp::Type::Bundle: {
      if (g_host || n < sizeof(mmp::Bundle)) return;
      mmp::Bundle b;
      memcpy(&b, d, sizeof(b));
      if (b.players != g_players || n < sizeof(b) + static_cast<size_t>(b.count) * b.players * sizeof(mmp::PadWire) ||
          b.count > b.newest + 1)
        return;
      if (b.ack > g_host_has && b.ack <= g_newest[g_me]) g_host_has = b.ack;
      for (int p = 0; p < g_players; ++p)
        if ((b.gone >> p & 1) && !g_gone[p] && p != g_me) {
          g_gone[p] = true;
          LOGW("net: %s (player %d) has left the session", g_names[p].c_str(), p + 1);
          wchar_t text[96];
          swprintf(text, 96, L"ИГРОК %d ВЫШЕЛ ИЗ ИГРЫ", p + 1);
          notice::say(text, 8);
        }
      for (uint32_t i = 0; i < b.count; ++i) {
        uint32_t frame = b.newest - b.count + 1 + i;
        if (frame < g_first || (g_bundle >= g_first && frame <= g_bundle)) continue;
        if (frame != (g_bundle >= g_first ? g_bundle + 1 : g_first)) break;
        for (int p = 0; p < g_players; ++p) {
          mmp::PadWire w;
          memcpy(&w, d + sizeof(b) + (static_cast<size_t>(i) * b.players + p) * sizeof(w), sizeof(w));
          set_pad(p, frame, from_wire(w));
        }
        g_bundle = frame;
      }
      break;
    }
    case mmp::Type::Hash: {
      if (!g_host || n < sizeof(mmp::Hash)) return;
      mmp::Hash hp;
      memcpy(&hp, d, sizeof(hp));
      auto it = g_hash_mine.find(hp.frame);
      if (it != g_hash_mine.end() && it->second != hp.hash && !g_desync) {
        desync(from, hp.frame, it->second, hp.hash);
        auto ds = mmp::make<mmp::Desync>(mmp::Type::Desync);
        ds.player = static_cast<uint8_t>(from);
        ds.frame = hp.frame;
        for (int p = 1; p < g_players; ++p)
          if (!g_gone[p]) send(g_peer[p], ds, Channel::Control);
      }
      break;
    }
    case mmp::Type::Desync: {
      if (g_host || n < sizeof(mmp::Desync)) return;
      mmp::Desync ds;
      memcpy(&ds, d, sizeof(ds));
      if (!g_desync) {
        g_desync = true;
        LOGE("net: DESYNC: the host says the game of player %d parted at frame %u", ds.player + 1, ds.frame);
        notice::say(L"ИГРЫ РАЗОШЛИСЬ\nНАЧНИТЕ СЕТЕВУЮ ИГРУ ЗАНОВО", 15);
      }
      break;
    }
    case mmp::Type::Bye:
      if (g_host)
        drop(from, "left");
      else
        end_session("the host has left");
      break;
    default:
      break;
  }
}

void pump_net(uint32_t wait_ms) {
  if (!g_t) return;
  g_pumped_ms = net::now_ms();
  Event ev[16];
  bool first = true;
  for (;;) {
    int n;
    {
      NetLock lock;
      if (first) {
        if (steam::ready()) steam::pump();
        g_t->Poll(wait_ms);
      }
      first = false;
      n = g_t->Receive(ev, 16);
    }
    if (n <= 0) break;
    for (int i = 0; i < n; ++i) {
      int p = player_of(ev[i].peer);
      if (p < 0) continue;
      if (ev[i].type == EventType::Data) {
        on_data(ev[i]);
      } else if (ev[i].type == EventType::Disconnected) {
        if (g_host)
          drop(p, ("the link is gone: " + ev[i].reason).c_str());
        else
          end_session(("the link to the host is gone: " + ev[i].reason).c_str());
      }
    }
  }
}

DWORD WINAPI keeper(void*) {
  uint32_t said = 0;
  for (;;) {
    Sleep(100);
    if (!g_keeper_on) continue;
    uint32_t now = net::now_ms();
    if (now - g_pumped_ms.load() < 300) continue;  // the game's thread is at it
    NetLock lock;
    if (!g_keeper_on || !g_t) continue;
    if (steam::ready()) steam::pump();
    g_t->Poll(0);
    ++g_kept;
    if (now - said >= 500) {
      said = now;
      mmp::Header alive;
      alive.type = static_cast<uint8_t>(mmp::Type::Alive);
      for (int p = 0; p < kMax; ++p)
        if (g_peer[p].valid() && !g_gone[p]) g_t->Send(g_peer[p], &alive, sizeof(alive), Channel::Input);
    }
  }
}

// --- the meeting -----------------------------------------------------------------------------
// Over Steam (by SteamID; the lab's stand-in too) or by address: a game started again from the network
// page is told (MELEENET_VIA), a game set up in the ini goes by [net] transport.
bool over_steam() {
  if (!g_cfg.net_via.empty()) return g_cfg.net_via == "steam";
  return g_cfg.net_transport == "steam" || g_cfg.net_transport == "mock";
}

// The host listens both ways when it can: Steam for friends, a UDP port for an address.
bool meet_host() {
  uint16_t port = static_cast<uint16_t>(g_cfg.net_port);
  const std::string& via = g_cfg.net_via;
  const std::string& tr = g_cfg.net_transport;
  bool want_steam = via.empty() ? (tr == "steam" || tr == "mock" || tr == "auto") : (via == "steam" || via == "both");
  bool want_enet = via.empty() ? (tr == "enet" || tr == "auto") : (via == "enet" || via == "both");
  int want = g_cfg.net_players < 2 ? 2 : g_cfg.net_players > kMax ? kMax : g_cfg.net_players;
  std::unique_ptr<transport::ITransport> ts, te;
  if (want_steam) {
    if (steam::boot(g_cfg))
      ts = transport::make_steam_transport([](uint64_t) { return true; });
    else
      LOGW("net: Steam is not there: %s", steam::unavailable_reason().c_str());
  }
  if (want_enet) {
    te = std::make_unique<transport::EnetTransport>(port, kMax - 1, 2000);
    if (!te->Init()) {
      LOGW("net: cannot listen on UDP port %u", port);
      te.reset();
    }
  }
  if (!ts && !te) {
    LOGE("net: no way to host (no Steam, no port)");
    return false;
  }
  bool via_steam = ts != nullptr;
  LOGI("net: hosting%s%s; waiting for %d player(s) (up to %d s)", ts ? " over Steam" : "", te ? " on a UDP port" : "", want - 1,
       g_cfg.net_wait_s);
  g_t = std::make_unique<transport::MultiTransport>(std::move(ts), std::move(te));
  g_names[0] = g_cfg.net_name;
  int in = 1;
  uint32_t t0 = net::now_ms();
  while (in < want && net::now_ms() - t0 < static_cast<uint32_t>(g_cfg.net_wait_s) * 1000) {
    if (via_steam) steam::pump();
    g_t->Poll(20);
    Event ev[8];
    int n = g_t->Receive(ev, 8);
    for (int i = 0; i < n && in < want; ++i) {
      if (ev[i].type != EventType::Data ||
          mmp::type_of(ev[i].data.data(), ev[i].data.size()) != static_cast<uint8_t>(mmp::Type::Hello) ||
          ev[i].data.size() < sizeof(mmp::Hello))
        continue;
      bool known = false;
      for (int p = 1; p < in; ++p) known = known || g_peer[p] == ev[i].peer;
      if (known) continue;  // (a Hello said twice)
      mmp::Hello hello;
      memcpy(&hello, ev[i].data.data(), sizeof(hello));
      hello.name[sizeof(hello.name) - 1] = 0;
      hello.mod_version[sizeof(hello.mod_version) - 1] = 0;
      const char* why = memcmp(hello.exe_hash, g_exe_hash, 8) != 0     ? "another ztmnt.exe"
                        : memcmp(hello.data_hash, g_data_hash, 8) != 0 ? "another archive.arc (the game's files differ)"
                                                                       : nullptr;
      if (why) {
        auto rej = mmp::make<mmp::Reject>(mmp::Type::Reject);
        copy_name(rej.reason, sizeof(rej.reason), why);
        send(ev[i].peer, rej, Channel::Control);
        g_t->Poll(20);
        LOGW("net: %s refused: %s", hello.name, why);
        continue;
      }
      g_peer[in] = ev[i].peer;
      g_names[in] = hello.name;
      LOGI("net: %s joined as player %d (meleenet %s, %s)", hello.name, in + 1, hello.mod_version,
           g_t->Describe(ev[i].peer).c_str());
      ++in;
    }
    if (!pump_messages()) return false;
  }
  if (in < 2) {
    LOGW("net: nobody joined in %d s; the game is played alone", g_cfg.net_wait_s);
    return false;
  }
  g_players = in;
  bool have_save = false;
  std::vector<uint8_t> save = read_file(module_dir() + L"tmnt.sv", have_save);
  for (int p = 1; p < g_players; ++p) {
    auto w = mmp::make<mmp::Welcome>(mmp::Type::Welcome);
    w.player = static_cast<uint8_t>(p);
    w.players = static_cast<uint8_t>(g_players);
    w.input_delay = static_cast<uint8_t>(g_delay);
    w.has_save = have_save && !save.empty() ? 1 : 0;
    w.first_frame = g_first;
    for (int q = 0; q < g_players; ++q) copy_name(w.names[q], sizeof(w.names[q]), g_names[q]);
    send(g_peer[p], w, Channel::Control);
    if (w.has_save) {
      std::vector<uint8_t> buf(sizeof(mmp::SaveFile) + save.size());
      auto sf = mmp::make<mmp::SaveFile>(mmp::Type::SaveFile);
      sf.size = static_cast<uint32_t>(save.size());
      memcpy(buf.data(), &sf, sizeof(sf));
      memcpy(buf.data() + sizeof(sf), save.data(), save.size());
      send_to(g_peer[p], buf.data(), buf.size(), Channel::Control);
    }
    send(g_peer[p], mmp::make<mmp::Start>(mmp::Type::Start), Channel::Control);
  }
  g_t->Poll(5);
  LOGI("net: %d players; the host's save (%u bytes) is sent; the game starts", g_players, (unsigned)save.size());
  g_me = 0;
  g_host = true;
  return true;
}

bool meet_guest() {
  net::Addr addr;
  std::string host = g_cfg.net_connect;
  uint16_t port = static_cast<uint16_t>(g_cfg.net_port);
  size_t colon = host.rfind(':');
  if (colon != std::string::npos) {
    port = static_cast<uint16_t>(atoi(host.c_str() + colon + 1));
    host.erase(colon);
  }
  bool via_steam = over_steam();
  if (via_steam) {
    if (!steam::boot(g_cfg)) {
      LOGE("net: Steam is not there: %s", steam::unavailable_reason().c_str());
      return false;
    }
    uint64_t host_id = g_cfg.net_peer ? g_cfg.net_peer : strtoull(g_cfg.net_connect.c_str(), nullptr, 10);
    if (!host_id) {
      LOGE("net: no SteamID of the host to join");
      return false;
    }
    g_t = transport::make_steam_transport([host_id](uint64_t id) { return id == host_id; });
    if (!g_t) return false;
    g_peer[0] = PeerId::steam(host_id);
    LOGI("net: joining the host %llu over Steam", (unsigned long long)host_id);
  } else {
    if (!net::resolve(host, port, addr)) {
      LOGE("net: cannot resolve %s", g_cfg.net_connect.c_str());
      return false;
    }
    g_t = std::make_unique<transport::EnetTransport>(static_cast<uint16_t>(0), 1, 2000);
    if (!g_t->Init()) {
      LOGE("net: cannot open a UDP socket");
      return false;
    }
    g_peer[0] = PeerId::enet(addr.sa.sin_addr.s_addr, port);
    LOGI("net: joining %s:%u", host.c_str(), port);
  }
  auto say_hello = [] {
    auto hello = mmp::make<mmp::Hello>(mmp::Type::Hello);
    memcpy(hello.exe_hash, g_exe_hash, 8);
    memcpy(hello.data_hash, g_data_hash, 8);
    copy_name(hello.mod_version, sizeof(hello.mod_version), MELEENET_VERSION);
    copy_name(hello.name, sizeof(hello.name), g_cfg.net_name);
    send(g_peer[0], hello, Channel::Control);
  };
  uint32_t t0 = net::now_ms();
  bool connected = false, welcomed = false, save_due = false;
  uint32_t dialled = 0, hello_at = 0;
  while (net::now_ms() - t0 < static_cast<uint32_t>(g_cfg.net_wait_s) * 1000) {
    if (!connected && (!dialled || net::now_ms() - dialled > (via_steam ? 1500u : 3000u))) {
      g_t->Close(g_peer[0]);
      g_t->Connect(g_peer[0]);  // (again: the host may not be up yet)
      dialled = net::now_ms();
    }
    // (Steam has no "connected": the session opens with the first message, and a host that is not up
    // yet never saw it)
    if (via_steam && connected && !welcomed && net::now_ms() - hello_at > 3000) {
      say_hello();
      hello_at = net::now_ms();
    }
    if (via_steam) steam::pump();
    g_t->Poll(20);
    Event ev[8];
    int n = g_t->Receive(ev, 8);
    for (int i = 0; i < n; ++i) {
      if (ev[i].peer != g_peer[0]) continue;
      if (ev[i].type == EventType::Connected) {
        connected = true;
        say_hello();
        hello_at = net::now_ms();
      } else if (ev[i].type == EventType::Disconnected) {
        connected = false;
        if (welcomed) {
          LOGE("net: the host is gone during the start: %s", ev[i].reason.c_str());
          return false;
        }
      } else if (ev[i].type == EventType::Data) {
        const uint8_t* d = ev[i].data.data();
        size_t size = ev[i].data.size();
        switch (static_cast<mmp::Type>(mmp::type_of(d, size))) {
          case mmp::Type::Reject: {
            mmp::Reject rej{};
            memcpy(&rej, d, size < sizeof(rej) ? size : sizeof(rej));
            rej.reason[sizeof(rej.reason) - 1] = 0;
            LOGE("net: the host refused: %s", rej.reason);
            return false;
          }
          case mmp::Type::Welcome: {
            if (size < sizeof(mmp::Welcome)) break;
            mmp::Welcome w;
            memcpy(&w, d, sizeof(w));
            if (w.player < 1 || w.player >= kMax || w.players < 2 || w.players > kMax) break;
            g_me = w.player;
            g_players = w.players;
            g_delay = w.input_delay;
            g_first = w.first_frame;
            for (int q = 0; q < g_players; ++q) {
              w.names[q][sizeof(w.names[q]) - 1] = 0;
              g_names[q] = w.names[q];
            }
            save_due = w.has_save != 0;
            welcomed = true;
            if (!save_due) DeleteFileW((module_dir() + L"tmnt-net.sv").c_str());  // the session is played without a save
            LOGI("net: in %s's game as player %d of %d, input delay %d%s", g_names[0].c_str(), g_me + 1, g_players, g_delay,
                 save_due ? ", the host's save to come" : ", no save");
            break;
          }
          case mmp::Type::SaveFile: {
            if (size < sizeof(mmp::SaveFile) || !welcomed) break;
            mmp::SaveFile sf;
            memcpy(&sf, d, sizeof(sf));
            if (size < sizeof(sf) + sf.size || sf.size > mmp::kSaveMax) break;
            if (FILE* fp = _wfopen((module_dir() + L"tmnt-net.sv").c_str(), L"wb")) {
              fwrite(d + sizeof(sf), 1, sf.size, fp);
              fclose(fp);
            }
            save_due = false;
            break;
          }
          case mmp::Type::Start:
            if (!welcomed) break;
            if (save_due) LOGW("net: the host's save did not arrive");
            g_host = false;
            return true;
          default:
            break;
        }
      }
    }
    if (!pump_messages()) return false;
  }
  LOGW("net: no host at %s in %d s; the game is played alone", g_cfg.net_connect.c_str(), g_cfg.net_wait_s);
  return false;
}

void write_pads(uint32_t frame) {
  for (int p = 0; p < kMax; ++p) {
    g_held[p] = p < g_players ? get_pad(p, frame) : input::Pad{};
    input::set_connected(p, p < g_players);
    input::write(p, g_held[p]);
  }
}

}  // namespace

bool active() { return g_phase == Phase::Playing; }
bool session_game() { return g_session_game; }
void leave(const char* why) { end_session(why); }
int me() { return g_phase == Phase::Playing || g_phase == Phase::Ready || g_phase == Phase::Ended ? g_me : 0; }
int players() { return g_phase == Phase::Off || g_phase == Phase::Meeting ? 1 : g_players; }
const char* name(int player) { return player >= 0 && player < kMax ? g_names[player].c_str() : ""; }

void meet() {
  if (g_phase != Phase::Meeting) return;
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  uint8_t hash[32] = {};
  sha256_file(exe, hash);
  memcpy(g_exe_hash, hash, 8);
  uint8_t data[32] = {};
  if (!sha256_file(module_dir() + L"archive.arc", data)) LOGW("net: archive.arc cannot be read");
  memcpy(g_data_hash, data, 8);
  net::startup();
  g_first = sim::frame() + 1;
  bool ok = g_cfg.net_mode == Config::Net::Host ? meet_host() : meet_guest();
  if (!ok) {
    if (g_t) g_t->Shutdown();
    g_t.reset();
    g_phase = Phase::Ended;
    g_players = 1;
    g_me = 0;
    notice::say(L"ИГРОКИ НЕ ПОДКЛЮЧИЛИСЬ\nСЕТЕВАЯ ИГРА НЕ НАЧАЛАСЬ", 20);
    return;
  }
  uint32_t now = net::now_ms();
  for (int p = 0; p < kMax; ++p) g_last_rx[p] = now;
  g_pumped_ms = now;
  g_phase = Phase::Ready;
  g_keeper_on = true;
  if (HANDLE t = CreateThread(nullptr, 0, keeper, nullptr, 0, nullptr)) CloseHandle(t);
}

bool frame(uint32_t frame) {
  g_frame = frame;
  if (g_phase == Phase::Ready && frame >= g_first) {
    if (frame != g_first) {
      LOGE("net: the session should have started at frame %u, this game is at %u", g_first, frame);
      end_session("the games did not start together");
    } else {
      g_phase = Phase::Playing;
      for (int p = 0; p < kMax; ++p) g_newest[p] = 0, g_guest_has[p] = 0;
      g_bundle = g_host_has = 0;
      g_started_ms = net::now_ms();
      *reinterpret_cast<int*>(game::kPrimaryPad) = 0;
      LOGI("net: playing from frame %u as player %d of %d", frame, g_me + 1, g_players);
    }
  }
  if (g_phase == Phase::Ended && g_session_game) {
    // The session is over; this PC's player goes on with the pad it had.
    input::Pad mine = input::read_local(frame);
    for (int p = 0; p < kMax; ++p) {
      g_held[p] = p == g_me ? mine : input::Pad{};
      input::write(p, g_held[p]);
    }
    return true;
  }
  if (g_phase != Phase::Playing) return false;
  if (*reinterpret_cast<const uint8_t*>(game::kQuit)) {
    end_session("this game is closing");
    return false;
  }

  // [debug] net_quit: this game leaves the session.
  if (g_cfg.net_quit_ms && net::now_ms() - g_started_ms >= g_cfg.net_quit_ms) {
    LOGI("net: [lab] leaving the session");
    end_session("this game is closing");
    return false;
  }
  // [debug] net_stall: the lab's stand-in for a frame that takes long.
  static bool stalled = false;
  if (g_cfg.net_stall_ms && !stalled && net::now_ms() - g_started_ms >= g_cfg.net_stall_at_ms) {
    stalled = true;
    LOGI("net: [lab] this game stands still for %u ms (frame %u)", g_cfg.net_stall_ms, frame);
    Sleep(g_cfg.net_stall_ms);
    LOGI("net: [lab] going on");
  }

  // This PC's pad, read now, is the pad of frame + delay; the first `delay` frames have none.
  if (frame == g_first)
    for (int d = 0; d < g_delay; ++d) set_pad(g_me, g_first + d, input::Pad{});
  input::Pad mine = input::read_local(frame);
  // (the start-up's "press Enter" screen: the host's game presses it, for everybody)
  if (g_host && frame % 20 == 0 && strcmp(sim::mode_name(), "TtTitleScreenMode") == 0) mine.buttons |= input::bit(game::kEnter);
  set_pad(g_me, frame + g_delay, mine);
  g_newest[g_me] = frame + g_delay;
  if (g_host) {
    for (int p = 1; p < g_players; ++p)
      if (g_gone[p]) set_pad(p, frame + g_delay, input::Pad{});
    send_bundles();
  } else {
    send_inputs();
  }

  if (frame % 60 == 0) {
    uint32_t h = sim::state_hash();
    if (g_host) {
      g_hash_mine[frame] = h;
      if (g_hash_mine.size() > 64) g_hash_mine.erase(g_hash_mine.begin());
    } else {
      auto hp = mmp::make<mmp::Hash>(mmp::Type::Hash);
      hp.frame = frame;
      hp.hash = h;
      send(g_peer[0], hp, Channel::Control);
    }
  }

  // Every player's pad for this frame.
  uint32_t t0 = net::now_ms(), resent = t0;
  pump_net(0);
  bool waited = false;
  auto ready = [&] { return g_host ? complete() >= frame : (g_bundle >= frame && g_bundle >= g_first); };
  while (g_phase == Phase::Playing && !ready()) {
    waited = true;
    pump_net(1);
    uint32_t now = net::now_ms();
    if (now - resent >= 8) {
      if (g_host)
        send_bundles();
      else
        send_inputs();
      resent = now;
    }
    if (g_host) {
      for (int p = 1; p < g_players; ++p)
        if (!g_gone[p] && now - g_last_rx[p] > 15000) drop(p, "nothing heard for 15 s");
    } else if (now - g_last_rx[0] > 15000) {
      end_session("nothing heard from the host for 15 s");
      break;
    }
    if (now - t0 > 100 && !pump_messages()) {
      end_session("this game is closing");
      break;
    }
  }
  if (waited) {
    g_waited_ms += net::now_ms() - t0;
    ++g_waits;
  }
  if (g_host) send_bundles();  // (the frame just completed goes out at once)
  if (frame % 1800 == 0) {
    NetLock lock;
    LOGI("net: frame %u, ping %u ms, waited %u ms in %u waits so far, the keeper stood in %u times%s", frame,
         g_t ? g_t->RttMs(g_peer[g_host ? 1 : 0]) : 0, g_waited_ms, g_waits, g_kept.load(), g_desync ? ", DESYNC" : "");
  }
  if (g_phase != Phase::Playing) {
    // (the session ended in this very frame: the frame is played with what is known)
    input::Pad own = get_pad(g_me, frame);
    for (int p = 0; p < kMax; ++p) {
      g_held[p] = p == g_me ? own : input::Pad{};
      input::write(p, g_held[p]);
    }
    return true;
  }
  write_pads(frame);
  return true;
}

void hold() {
  if (g_phase != Phase::Playing && !(g_phase == Phase::Ended && g_session_game)) return;
  for (int p = 0; p < kMax; ++p) input::write(p, g_held[p]);
}

bool prepare(Config& cfg) {
  wchar_t v[200];
  if (GetEnvironmentVariableW(L"MELEENET_AFTER", v, 200)) {
    // the game before this one closes: its window, its ports and its place in Steam are still its own
    if (HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, wcstoul(v, nullptr, 10))) {
      WaitForSingleObject(old, 15000);
      CloseHandle(old);
    }
    SetEnvironmentVariableW(L"MELEENET_AFTER", nullptr);
  }
  auto narrow = [](const wchar_t* w) {
    std::string s;
    for (; *w; ++w) s += static_cast<char>(*w < 128 ? *w : '?');
    return s;
  };
  if (!GetEnvironmentVariableW(L"MELEENET_NET", v, 200)) return false;
  std::wstring mode = v;
  SetEnvironmentVariableW(L"MELEENET_NET", nullptr);
  if (mode != L"host" && mode != L"join") return false;
  cfg.net_mode = mode == L"host" ? Config::Net::Host : Config::Net::Join;
  cfg.net_wait_s = 45;  // (the other games are starting again right now: they are there in seconds or not at all)
  if (GetEnvironmentVariableW(L"MELEENET_VIA", v, 200)) cfg.net_via = narrow(v);
  if (GetEnvironmentVariableW(L"MELEENET_PEER", v, 200)) cfg.net_peer = _wcstoui64(v, nullptr, 10);
  if (GetEnvironmentVariableW(L"MELEENET_JOIN", v, 200)) cfg.net_connect = narrow(v);
  if (GetEnvironmentVariableW(L"MELEENET_PLAYERS", v, 200)) cfg.net_players = _wtoi(v);
  for (const wchar_t* name : {L"MELEENET_VIA", L"MELEENET_PEER", L"MELEENET_JOIN", L"MELEENET_PLAYERS"})
    SetEnvironmentVariableW(name, nullptr);
  return true;
}

void install(const Config& cfg) {
  g_cfg = cfg;
  InitializeCriticalSection(&g_net_lock);
  g_delay = cfg.net_delay;
  if (cfg.net_mode != Config::Net::Off) {
    g_phase = Phase::Meeting;
    g_session_game = true;
    LOGI("net: %s", cfg.net_mode == Config::Net::Host ? "this game hosts" : "this game joins");
    if (cfg.net_mode == Config::Net::Join) use_session_save();
  }
}

}  // namespace netplay
