#include "mod/netlobby.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "common/lobby_steam.h"
#include "common/log.h"
#include "common/net.h"
#include "common/protocol.h"
#include "common/sha256.h"
#include "common/steam_boot.h"
#include "common/transport.h"
#include "common/transport_enet.h"
#include "common/transport_steam.h"
#include "common/version.h"
#include "mod/video.h"

namespace netlobby {
namespace {

using transport::Channel;
using transport::Event;
using transport::EventType;
using transport::ITransport;
using transport::PeerId;

constexpr int kMax = mmp::kPlayers;

Config g_cfg;
View g_view;
State g_state = State::Closed;

uint8_t g_exe_hash[8] = {}, g_data_hash[8] = {};
std::string g_exe16;
bool g_hashed = false;

bool g_steam = false;
uint32_t g_steam_tried_ms = 0;
std::string g_steam_note;
std::unique_ptr<lobby::IBrowser> g_browser;
std::unique_ptr<lobby::IHost> g_host;
std::unique_ptr<ITransport> g_ts;  // Steam (or the lab's stand-in)
std::unique_ptr<ITransport> g_te;  // an address (ENet)

// Hosting: the guests, in the order they came (players 2..4). Joining: [0] = the host.
struct Peer {
  ITransport* via = nullptr;
  PeerId peer;
  std::string name;
  bool acked = false;
};
Peer g_peers[kMax - 1];
int g_guests = 0;
// Joined: the lobby as the host tells it.
int g_players = 0, g_me = 0;
std::string g_names[kMax];

std::string g_error, g_addresses;
uint32_t g_since_ms = 0;  // Joining: the knock; Starting: the start
bool g_is_host = false;
mmp::Go g_go{};           // guest: the host's start
bool g_relaunched = false;

void copy_text(char* to, size_t n, const std::string& s) {
  memset(to, 0, n);
  size_t len = s.size() < n - 1 ? s.size() : n - 1;
  while (len > 0 && len < s.size() && (static_cast<unsigned char>(s[len]) & 0xC0) == 0x80) --len;  // not inside a UTF-8 letter
  memcpy(to, s.data(), len);
}

std::string my_name() {
  if (g_steam && (g_cfg.net_name.empty() || g_cfg.net_name == "player")) {
    std::string n = steam::my_name();
    if (!n.empty()) return n;
  }
  return g_cfg.net_name.empty() ? "player" : g_cfg.net_name;
}

void hashes() {
  if (g_hashed) return;
  g_hashed = true;
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  uint8_t h[32] = {};
  sha256_file(exe, h);
  memcpy(g_exe_hash, h, 8);
  g_exe16 = to_hex(h, 8);
  uint8_t d[32] = {};
  sha256_file(module_dir() + L"archive.arc", d);
  memcpy(g_data_hash, d, 8);
}

void try_steam() {
  if (g_steam || g_cfg.net_transport == "enet") return;
  uint32_t now = net::now_ms();
  if (g_steam_tried_ms && now - g_steam_tried_ms < 5000) return;
  g_steam_tried_ms = now ? now : 1;
  g_steam = steam::boot(g_cfg);
  g_steam_note = g_steam ? "" : steam::unavailable_reason();
  if (g_steam && !g_browser) g_browser = lobby::make_browser(g_exe16);
}

bool header_of(const std::vector<uint8_t>& d, mmp::Header& h) {
  if (d.size() < sizeof(h)) return false;
  memcpy(&h, d.data(), sizeof(h));
  return h.magic == mmp::kMagic;
}

template <typename T>
void send(ITransport* t, const PeerId& peer, const T& packet) {
  if (t && peer.valid()) t->Send(peer, &packet, sizeof(packet), Channel::Control);
}

void say_bye() {
  mmp::Header bye;
  bye.type = static_cast<uint8_t>(mmp::Type::Bye);
  int n = g_is_host ? g_guests : 1;
  for (int i = 0; i < n; ++i)
    if (g_peers[i].via && g_peers[i].peer.valid()) {
      g_peers[i].via->Send(g_peers[i].peer, &bye, sizeof(bye), Channel::Control);
      g_peers[i].via->Poll(0);
    }
}

void drop_net() {
  for (Peer& p : g_peers) p = Peer{};
  g_guests = 0;
  g_players = 0;
  if (g_host) g_host.reset();
  if (g_browser) g_browser->Leave();
  if (g_ts) g_ts->Shutdown();
  if (g_te) g_te->Shutdown();
  g_ts.reset();
  g_te.reset();
}

void to_menu(const std::string& error) {
  drop_net();
  g_error = error;
  g_state = State::Menu;
}

// --- the game again, into the session ------------------------------------------------------
void relaunch(const wchar_t* mode, const wchar_t* via, uint64_t peer, const std::string& join, int players) {
  if (g_relaunched) return;
  g_relaunched = true;
  // (the lobby's ports and Steam are this game's until it is gone; the new one waits for that)
  drop_net();
  g_browser.reset();
  steam::shutdown();
  wchar_t num[32];
  SetEnvironmentVariableW(L"MELEENET_NET", mode);
  SetEnvironmentVariableW(L"MELEENET_VIA", via);
  swprintf(num, 32, L"%llu", static_cast<unsigned long long>(peer));
  SetEnvironmentVariableW(L"MELEENET_PEER", peer ? num : nullptr);
  SetEnvironmentVariableW(L"MELEENET_JOIN", join.empty() ? nullptr : std::wstring(join.begin(), join.end()).c_str());
  swprintf(num, 32, L"%d", players);
  SetEnvironmentVariableW(L"MELEENET_PLAYERS", num);
  swprintf(num, 32, L"%lu", GetCurrentProcessId());
  SetEnvironmentVariableW(L"MELEENET_AFTER", num);
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
  std::vector<wchar_t> line(cmd.begin(), cmd.end());
  line.push_back(0);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  bool ok = CreateProcessW(exe, line.data(), nullptr, nullptr, FALSE, 0, nullptr, module_dir().c_str(), &si, &pi) != 0;
  for (const wchar_t* v : {L"MELEENET_NET", L"MELEENET_VIA", L"MELEENET_PEER", L"MELEENET_JOIN", L"MELEENET_PLAYERS", L"MELEENET_AFTER"})
    SetEnvironmentVariableW(v, nullptr);
  if (!ok) {
    LOGE("lobby: cannot start the game again (error %lu)", GetLastError());
    g_relaunched = false;
    g_error = "Не удалось перезапустить игру.";
    g_state = State::Menu;
    return;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  LOGI("lobby: the game starts again into the session as %ls over %ls (new pid %lu); this one closes", mode, via,
       pi.dwProcessId);
  video::request_quit();
}

// --- host ----------------------------------------------------------------------------------
void reject(ITransport* t, const PeerId& peer, const char* why, uint16_t version) {
  auto r = mmp::make<mmp::Reject>(mmp::Type::Reject);
  r.h.version = version;  // (in the asker's own version: it must be able to read why)
  copy_text(r.reason, sizeof(r.reason), why);
  t->Send(peer, &r, sizeof(r), Channel::Control);
  t->Disconnect(peer);
}

int guest_of(ITransport* t, const PeerId& peer) {
  for (int i = 0; i < g_guests; ++i)
    if (g_peers[i].via == t && g_peers[i].peer == peer) return i;
  return -1;
}

// Everybody in the lobby is told who is in it.
void tell_lobby() {
  for (int i = 0; i < g_guests; ++i) {
    auto l = mmp::make<mmp::Lobby>(mmp::Type::Lobby);
    l.players = static_cast<uint8_t>(g_guests + 1);
    l.you = static_cast<uint8_t>(i + 1);
    copy_text(l.names[0], sizeof(l.names[0]), my_name());
    for (int k = 0; k < g_guests; ++k) copy_text(l.names[k + 1], sizeof(l.names[k + 1]), g_peers[k].name);
    send(g_peers[i].via, g_peers[i].peer, l);
  }
  if (g_host) g_host->SetInGame(false);
}

void remove_guest(int i, const char* why) {
  LOGI("lobby: %s %s", g_peers[i].name.c_str(), why);
  for (int k = i; k + 1 < g_guests; ++k) g_peers[k] = g_peers[k + 1];
  g_peers[--g_guests] = Peer{};
  tell_lobby();
}

void host_events(ITransport* t) {
  if (!t) return;
  t->Poll(0);
  Event ev[8];
  for (int n; (n = t->Receive(ev, 8)) > 0;) {
    for (int i = 0; i < n; ++i) {
      const Event& e = ev[i];
      int who = guest_of(t, e.peer);
      if (e.type == EventType::Disconnected) {
        if (who >= 0 && g_state != State::Starting) remove_guest(who, ("is gone (" + e.reason + ")").c_str());
        continue;
      }
      mmp::Header h;
      if (e.type != EventType::Data || !header_of(e.data, h)) continue;
      auto type = static_cast<mmp::Type>(h.type);
      if (h.version != mmp::kVersion) {
        if (type == mmp::Type::Knock || type == mmp::Type::Hello) {
          LOGW("lobby: a player with another version of meleenet knocked (protocol %u, here %u)", h.version, mmp::kVersion);
          reject(t, e.peer, "у хоста другая версия мода meleenet", h.version);
        }
        continue;
      }
      if (type == mmp::Type::Knock && e.data.size() >= sizeof(mmp::Hello)) {
        mmp::Hello k;
        memcpy(&k, e.data.data(), sizeof(k));
        k.name[sizeof(k.name) - 1] = 0;
        k.mod_version[sizeof(k.mod_version) - 1] = 0;
        const char* why = memcmp(k.exe_hash, g_exe_hash, 8) != 0     ? "у хоста другая версия игры (ztmnt.exe)"
                          : memcmp(k.data_hash, g_data_hash, 8) != 0 ? "у хоста другие файлы игры (archive.arc)"
                          : who < 0 && g_guests >= kMax - 1          ? "в игре уже четыре игрока"
                          : g_state != State::Hosting                ? "игра уже начинается"
                                                                     : nullptr;
        if (why) {
          LOGW("lobby: %s is not let in: %s", k.name, why);
          reject(t, e.peer, why, mmp::kVersion);
          continue;
        }
        if (who < 0) {
          who = g_guests++;
          g_peers[who].via = t;
          g_peers[who].peer = e.peer;
          LOGI("lobby: %s is in as player %d (meleenet %s, %s)", k.name, who + 2, k.mod_version, t->Describe(e.peer).c_str());
        }
        g_peers[who].name = k.name;
        tell_lobby();
      } else if (type == mmp::Type::Bye && who >= 0 && g_state != State::Starting) {
        remove_guest(who, "left");
      } else if (type == mmp::Type::GoAck && who >= 0) {
        g_peers[who].acked = true;
      }
    }
  }
}

// --- guest ---------------------------------------------------------------------------------
void knock() {
  auto k = mmp::make<mmp::Hello>(mmp::Type::Knock);
  memcpy(k.exe_hash, g_exe_hash, 8);
  memcpy(k.data_hash, g_data_hash, 8);
  copy_text(k.mod_version, sizeof(k.mod_version), MELEENET_VERSION);
  copy_text(k.name, sizeof(k.name), my_name());
  send(g_peers[0].via, g_peers[0].peer, k);
}

void guest_events() {
  ITransport* t = g_peers[0].via;
  if (!t) return;
  t->Poll(0);
  Event ev[8];
  for (int n; g_peers[0].via == t && (n = t->Receive(ev, 8)) > 0;) {
    for (int i = 0; i < n && g_peers[0].via == t; ++i) {
      const Event& e = ev[i];
      if (e.peer != g_peers[0].peer) continue;
      if (e.type == EventType::Connected) {
        knock();
        continue;
      }
      if (e.type == EventType::Disconnected) {
        if (g_state == State::Starting) continue;  // (the host's game is closing to start again, as this one will)
        LOGW("lobby: the link to the host is gone (%s)", e.reason.c_str());
        to_menu(g_state == State::Joined ? "Хост закрыл игру." : "Не удалось подключиться к хосту.");
        return;
      }
      mmp::Header h;
      if (!header_of(e.data, h)) continue;
      auto type = static_cast<mmp::Type>(h.type);
      if (type == mmp::Type::Reject) {  // (in any version: this is how another version says so)
        mmp::Reject r{};
        memcpy(&r, e.data.data(), e.data.size() < sizeof(r) ? e.data.size() : sizeof(r));
        r.reason[sizeof(r.reason) - 1] = 0;
        LOGW("lobby: the host does not let in: %s", r.reason);
        to_menu(std::string("Хост не пустил: ") + r.reason);
        return;
      }
      if (h.version != mmp::kVersion) continue;
      if (type == mmp::Type::Lobby && e.data.size() >= sizeof(mmp::Lobby)) {
        mmp::Lobby l;
        memcpy(&l, e.data.data(), sizeof(l));
        if (l.players < 2 || l.players > kMax || l.you < 1 || l.you >= l.players) continue;
        g_players = l.players;
        g_me = l.you;
        for (int k = 0; k < kMax; ++k) {
          l.names[k][sizeof(l.names[k]) - 1] = 0;
          g_names[k] = k < g_players ? l.names[k] : "";
        }
        if (g_state != State::Joined) LOGI("lobby: in %s's game (%s); waiting for the start", l.names[0], t->Describe(e.peer).c_str());
        g_state = State::Joined;
      } else if (type == mmp::Type::Go && e.data.size() >= sizeof(mmp::Go) && g_state == State::Joined) {
        memcpy(&g_go, e.data.data(), sizeof(g_go));
        mmp::Header ack;
        ack.type = static_cast<uint8_t>(mmp::Type::GoAck);
        t->Send(e.peer, &ack, sizeof(ack), Channel::Control);
        g_state = State::Starting;
        g_since_ms = net::now_ms();
        LOGI("lobby: the host starts the game");
      } else if (type == mmp::Type::Bye) {
        to_menu("Хост закрыл игру.");
        return;
      }
    }
  }
}

void begin_join(ITransport* t, const PeerId& host) {
  for (Peer& p : g_peers) p = Peer{};
  g_peers[0].via = t;
  g_peers[0].peer = host;
  g_error.clear();
  g_is_host = false;
  g_state = State::Joining;
  g_since_ms = net::now_ms();
  if (!t->Connect(host)) to_menu("Не удалось подключиться к хосту.");
}

void fill_view() {
  g_view.state = g_state;
  g_view.steam = g_steam;
  g_view.steam_note = g_steam_note;
  g_view.error = g_error;
  g_view.addresses = g_addresses;
  if (g_is_host && (g_state == State::Hosting || g_state == State::Starting)) {
    g_view.players = g_guests + 1;
    g_view.me = 0;
    g_view.names[0] = my_name();
    for (int k = 1; k < kMax; ++k) g_view.names[k] = k <= g_guests ? g_peers[k - 1].name : "";
  } else if (g_state == State::Joined || (g_state == State::Starting && !g_is_host)) {
    g_view.players = g_players;
    g_view.me = g_me;
    for (int k = 0; k < kMax; ++k) g_view.names[k] = g_names[k];
  } else {
    g_view.players = 0;
    g_view.me = 0;
    for (auto& n : g_view.names) n.clear();
  }
  g_view.games.clear();
  if (g_browser && g_state == State::Menu)
    for (const lobby::Found& f : g_browser->List()) {
      Game g;
      g.lobby = f.lobby;
      g.host = f.host.empty() ? f.name : f.host;
      g.ok = f.compatible && f.state != "ingame";
      g.why = !f.compatible ? f.why_not : f.state == "ingame" ? "уже играет" : "";
      g_view.games.push_back(g);
    }
}

}  // namespace

void install(const Config& cfg) { g_cfg = cfg; }

const View& view() { return g_view; }

void open() {
  if (g_state != State::Closed) return;
  hashes();
  net::startup();
  try_steam();
  g_error.clear();
  g_state = State::Menu;
  LOGI("lobby: the network page is open (%s)", g_steam ? (steam::is_mock() ? "the lab's Steam" : "Steam") : g_steam_note.c_str());
  fill_view();
}

void close() {
  if (g_state == State::Closed || g_state == State::Starting) return;
  say_bye();
  drop_net();
  g_state = State::Closed;
  fill_view();
}

void leave() {
  if (g_state == State::Starting || g_state == State::Closed) return;
  say_bye();
  to_menu("");
  fill_view();
}

void host() {
  if (g_state != State::Menu) return;
  g_error.clear();
  try_steam();
  if (g_steam) {
    g_host = lobby::make_host(g_cfg, g_exe16);
    g_ts = transport::make_steam_transport([](uint64_t id) { return g_host && g_host->IsMember(id); });
  }
  if (g_cfg.net_transport != "steam" && g_cfg.net_transport != "mock") {
    g_te = std::make_unique<transport::EnetTransport>(static_cast<uint16_t>(g_cfg.net_port), kMax - 1, 2000);
    if (!g_te->Init()) {
      LOGW("lobby: UDP port %d is taken: no game by address", g_cfg.net_port);
      g_te.reset();
    }
  }
  if (!g_ts && !g_te) {
    to_menu("Не удалось открыть игру: Steam недоступен, порт занят.");
    fill_view();
    return;
  }
  g_addresses.clear();
  if (g_te) {
    for (const net::LocalAddr& a : net::local_ipv4()) {
      in_addr ia;
      ia.s_addr = a.ip;
      g_addresses = inet_ntoa(ia);
      if (g_cfg.net_port != mmp::kDefaultPort) g_addresses += ":" + std::to_string(g_cfg.net_port);
      break;  // (the adapter with the default route comes first)
    }
  }
  for (Peer& p : g_peers) p = Peer{};
  g_guests = 0;
  g_is_host = true;
  g_state = State::Hosting;
  LOGI("lobby: this player's game is open (%s%s%s)", g_ts ? "Steam" : "", g_ts && g_te ? " and " : "", g_te ? "an address" : "");
  fill_view();
}

void join_game(uint64_t lobby_id) {
  if (g_state != State::Menu || !g_browser) return;
  g_error.clear();
  g_browser->Join(lobby_id);  // the answer comes to tick()
}

void join_address(const std::string& address) {
  if (g_state != State::Menu) return;
  std::string host = address;
  uint16_t port = static_cast<uint16_t>(g_cfg.net_port);
  size_t colon = host.rfind(':');
  if (colon != std::string::npos) {
    port = static_cast<uint16_t>(atoi(host.c_str() + colon + 1));
    host.erase(colon);
  }
  net::Addr addr;
  if (host.empty() || !net::resolve(host, port, addr)) {
    g_error = "Такой адрес не найти: " + address;
    fill_view();
    return;
  }
  g_te = std::make_unique<transport::EnetTransport>(static_cast<uint16_t>(0), 1, 2000);
  if (!g_te->Init()) {
    g_te.reset();
    g_error = "Не удалось открыть сеть.";
    fill_view();
    return;
  }
  LOGI("lobby: knocking at %s:%u", host.c_str(), port);
  begin_join(g_te.get(), PeerId::enet(addr.sa.sin_addr.s_addr, port));
  fill_view();
}

void start() {
  if (g_state != State::Hosting || g_guests < 1) return;
  for (int i = 0; i < g_guests; ++i) {
    auto go = mmp::make<mmp::Go>(mmp::Type::Go);
    go.via = g_peers[i].via == g_ts.get() ? 2 : 1;
    go.players = static_cast<uint8_t>(g_guests + 1);
    go.port = static_cast<uint16_t>(g_cfg.net_port);
    go.steam_id = g_steam ? steam::my_id() : 0;
    send(g_peers[i].via, g_peers[i].peer, go);
    g_peers[i].acked = false;
  }
  g_state = State::Starting;
  g_since_ms = net::now_ms();
  LOGI("lobby: the start: %d games start again into the session", g_guests + 1);
  fill_view();
}

void tick() {
  if (g_state == State::Closed || g_relaunched) return;
  uint32_t now = net::now_ms();
  if (g_steam) steam::pump();
  switch (g_state) {
    case State::Menu: {
      if (!g_steam) try_steam();
      if (!g_browser) break;
      g_browser->Tick(now, true);
      lobby::JoinResult jr;
      if (g_browser->TakeJoinResult(jr)) {
        if (!jr.ok) {
          g_error = jr.error;
        } else {
          uint64_t host_id = jr.host_id;
          g_ts = transport::make_steam_transport([host_id](uint64_t id) { return id == host_id; });
          if (g_ts)
            begin_join(g_ts.get(), PeerId::steam(host_id));
          else
            to_menu("Steam не дал связаться с хостом.");
        }
      }
      break;
    }
    case State::Hosting: {
      if (g_host) {
        g_host->Tick(now);
        uint64_t left = 0;
        while (g_host->TakeLeft(left)) {
          int who = g_ts ? guest_of(g_ts.get(), PeerId::steam(left)) : -1;
          if (who >= 0) remove_guest(who, "left the Steam lobby");
        }
      }
      host_events(g_ts.get());
      host_events(g_te.get());
      break;
    }
    case State::Joining:
      if (g_browser) g_browser->Tick(now, false);
      guest_events();
      if (g_state == State::Joining && now - g_since_ms > 15000) to_menu("Хост не отвечает.");
      break;
    case State::Joined:
      if (g_browser) g_browser->Tick(now, false);
      guest_events();
      break;
    case State::Starting:
      if (g_is_host) {  // the host: once every guest has the word (or it took too long)
        host_events(g_ts.get());
        host_events(g_te.get());
        bool all = true;
        bool any_steam = false, any_enet = false;
        for (int i = 0; i < g_guests; ++i) {
          all = all && g_peers[i].acked;
          (g_peers[i].via == g_ts.get() ? any_steam : any_enet) = true;
        }
        if (all || now - g_since_ms > 2500)
          relaunch(L"host", any_steam && any_enet ? L"both" : any_steam ? L"steam" : L"enet", 0, "", g_guests + 1);
      } else {  // a guest: a moment for the answer to leave
        if (g_peers[0].via) g_peers[0].via->Poll(0);
        if (now - g_since_ms > 400) {
          if (g_go.via == 2) {
            relaunch(L"join", L"steam", g_go.steam_id ? g_go.steam_id : g_peers[0].peer.id, "", g_go.players);
          } else {
            in_addr ia;
            ia.s_addr = g_peers[0].peer.enet_ip();
            relaunch(L"join", L"enet", 0,
                     std::string(inet_ntoa(ia)) + ":" + std::to_string(g_go.port ? g_go.port : g_peers[0].peer.enet_port()),
                     g_go.players);
          }
        }
      }
      break;
    default:
      break;
  }
  fill_view();
}

}  // namespace netlobby
