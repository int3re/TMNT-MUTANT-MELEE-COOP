// Steam transport logic over steamnet::IMessages (real Steam or the lab mock).
//
// ISteamNetworkingMessages is "UDP by SteamID": it reports failed sessions but
// not a peer that closes or dies, and a restarted peer comes back with the
// same id. So every message carries a small transport header:
//   [u32 sender run id][payload]           channels 0 (control), 1 (input)
//   [u32 sender run id][u8 kind]           channel 2: kind 1 = bye, 2 = keep-alive
// The run id is random per transport instance: a new id from a known peer
// means it restarted (old session dropped, new one starts clean), and late
// datagrams with an earlier id are dropped. Keep-alives go out every second;
// a peer silent for kSilenceMs is dropped. The game protocol is unchanged.
#include "common/transport_steam.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "common/log.h"
#include "common/net.h"
#include "common/steam_backend.h"

namespace transport {
namespace {

constexpr int kChanTransport = 2;
constexpr uint8_t kBye = 1, kKeepAlive = 2;
constexpr uint32_t kKeepAliveMs = 1000;
constexpr uint32_t kSilenceMs = 30000;  // (a player's link may be gone for a while and come back: netplay decides)
// A client sends Hello right after joining the lobby; the host may see the
// session request before the lobby tells it about the new member.
constexpr uint32_t kPendingAcceptMs = 5000;
constexpr uint32_t kCloseGraceMs = 1500;  // let a final Reject get through
constexpr size_t kHeader = 4;
constexpr size_t kMaxOldRuns = 4;

// Time since then, signed: a timestamp taken after "now" (a peer added later
// in the same poll) counts as 0, not as 49 days of silence.
int32_t elapsed(uint32_t now, uint32_t then) { return (int32_t)(now - then); }

uint32_t random_run() {
  std::random_device rd;
  uint32_t r = 0;
  while (r == 0) r = rd() ^ (GetCurrentProcessId() << 16) ^ GetTickCount();
  return r;
}

class SteamTransport final : public ITransport {
 public:
  SteamTransport(std::unique_ptr<steamnet::IMessages> msgs, AcceptFn accept)
      : msgs_(std::move(msgs)), accept_(std::move(accept)), run_(random_run()) {}
  ~SteamTransport() override { Shutdown(); }

  bool Init() override {
    LOGI("Steam transport ready (ISteamNetworkingMessages, run %08X)", run_);
    return msgs_ != nullptr;
  }

  void Poll(uint32_t wait_ms) override {
    if (!msgs_) return;
    uint32_t start = net::now_ms();
    for (;;) {
      session_events();
      int got = receive_all();
      if (got || wait_ms == 0 || net::now_ms() - start >= wait_ms) break;
      Sleep(1);
    }
    uint32_t now = net::now_ms();
    check_pending(now);
    keep_alive(now);
    finish_closing(now);
    if (now - last_debug_ >= 1000) {
      last_debug_ = now;
      msgs_->LogNetworkState();
      log_status();
    }
  }

  bool Send(const PeerId& peer, const void* data, size_t size, Channel ch) override {
    Peer* p = find(peer.id);
    if (!msgs_ || peer.kind != Kind::Steam || !p || p->closing) return false;
    out_.resize(kHeader + size);
    std::memcpy(out_.data(), &run_, kHeader);
    std::memcpy(out_.data() + kHeader, data, size);
    int err = 0;
    if (!msgs_->Send(peer.id, out_.data(), (uint32_t)out_.size(), ch == Channel::Control, (int)ch, err)) {
      uint32_t now = net::now_ms();
      if (now - last_send_error_ >= 2000) {  // input goes out 120 times a second
        last_send_error_ = now;
        LOGW("Steam send to %llu failed (EResult %d)", (unsigned long long)peer.id, err);
      }
      return false;
    }
    p->last_sent = net::now_ms();
    return true;
  }

  int Receive(Event* out, int max) override {
    int n = std::min<int>(max, (int)queue_.size());
    for (int i = 0; i < n; ++i) out[i] = std::move(queue_[i]);
    queue_.erase(queue_.begin(), queue_.begin() + n);
    return n;
  }

  void Disconnect(const PeerId& peer) override {
    Peer* p = find(peer.id);
    if (!p || p->closing) return;
    send_transport(peer.id, kBye, true);
    p->closing = true;
    p->close_at = net::now_ms() + kCloseGraceMs;
  }

  void Close(const PeerId& peer) override {
    if (!find(peer.id)) return;
    send_transport(peer.id, kBye, false);  // best effort
    msgs_->Close(peer.id);
    forget(peer.id);
  }

  void Shutdown() override {
    if (!msgs_) return;
    for (const Peer& p : std::vector<Peer>(peers_)) Close(PeerId::steam(p.id));
    pending_.clear();
    queue_.clear();
    msgs_.reset();
  }

  const char* Name() const override { return "Steam"; }

  // There is no explicit connect: the session opens with the first message
  // (the client's Hello). A host that never accepts shows up as SessionFailed
  // or as silence.
  bool Connect(const PeerId& peer) override {
    if (!msgs_ || peer.kind != Kind::Steam || !peer.valid()) return false;
    if (find(peer.id)) Close(peer);
    add(peer.id);
    LOGI("Steam: opening a session with %s", who(peer.id).c_str());
    push(EventType::Connected, peer.id);
    return true;
  }

  uint32_t RttMs(const PeerId& peer) override {
    if (!msgs_) return 0;
    steamnet::LinkStatus st = msgs_->Status(peer.id);
    return st.connected && st.ping_ms > 0 ? (uint32_t)st.ping_ms : 0;
  }

  std::string Describe(const PeerId& peer) override {
    std::string out = "Steam '" + persona(peer.id) + "'";
    if (msgs_) {
      steamnet::LinkStatus st = msgs_->Status(peer.id);
      if (st.connected) out += st.relayed ? ", relay" : ", direct";
    }
    return out;
  }

 private:
  struct Peer {
    uint64_t id = 0;
    uint32_t run = 0;                // the peer's current run id, 0 = not heard yet
    std::vector<uint32_t> old_runs;  // earlier runs: their late datagrams are dropped
    uint32_t last_heard = 0, last_sent = 0;
    bool closing = false;
    uint32_t close_at = 0;
  };
  struct Pending {
    uint64_t id;
    uint32_t since;
  };

  std::string persona(uint64_t id) {
    std::string n = msgs_ ? msgs_->Persona(id) : "";
    return n.empty() ? std::to_string(id) : n;
  }
  std::string who(uint64_t id) {
    return persona(id) + " (" + std::to_string(id) + ")";
  }

  Peer* find(uint64_t id) {
    for (Peer& p : peers_)
      if (p.id == id) return &p;
    return nullptr;
  }
  Peer& add(uint64_t id) {
    Peer p;
    p.id = id;
    p.last_heard = p.last_sent = net::now_ms();
    peers_.push_back(p);
    return peers_.back();
  }
  void forget(uint64_t id) {
    peers_.erase(std::remove_if(peers_.begin(), peers_.end(), [id](const Peer& p) { return p.id == id; }),
                 peers_.end());
  }
  void push(EventType type, uint64_t id, const std::string& reason = "") {
    Event e;
    e.type = type;
    e.peer = PeerId::steam(id);
    e.reason = reason;
    queue_.push_back(std::move(e));
  }
  void drop(uint64_t id, const std::string& reason) {
    msgs_->Close(id);
    forget(id);
    push(EventType::Disconnected, id, reason);
  }

  void send_transport(uint64_t to, uint8_t kind, bool reliable) {
    uint8_t buf[kHeader + 1];
    std::memcpy(buf, &run_, kHeader);
    buf[kHeader] = kind;
    int err = 0;
    msgs_->Send(to, buf, sizeof(buf), reliable, kChanTransport, err);
  }

  void accept_session(uint64_t id) {
    msgs_->Accept(id);
    if (find(id)) return;  // a known peer restarting its session
    add(id);
    LOGI("Steam session from %s accepted", who(id).c_str());
    push(EventType::Connected, id);
  }

  void session_events() {
    steamnet::SessionEvent ev;
    while (msgs_->NextSessionEvent(ev)) {
      if (!ev.failed) {
        if (find(ev.peer) || (accept_ && accept_(ev.peer))) {
          accept_session(ev.peer);
          continue;
        }
        bool known = std::any_of(pending_.begin(), pending_.end(), [&](const Pending& q) { return q.id == ev.peer; });
        if (!known) {
          LOGD("Steam session request from %llu: not (yet) allowed, waiting for lobby membership",
               (unsigned long long)ev.peer);
          pending_.push_back(Pending{ev.peer, net::now_ms()});
        }
      } else {
        LOGW("Steam session with %s failed: end reason %d, %s", who(ev.peer).c_str(), ev.end_reason,
             ev.debug.c_str());
        if (!find(ev.peer)) continue;
        char reason[160];
        snprintf(reason, sizeof(reason), "Steam connection failed (%d: %s)", ev.end_reason, ev.debug.c_str());
        forget(ev.peer);
        push(EventType::Disconnected, ev.peer, reason);
      }
    }
  }

  // Applies the run id check; returns false if the message must be dropped.
  bool check_run(Peer& p, uint32_t run) {
    if (run == p.run) return true;
    if (std::find(p.old_runs.begin(), p.old_runs.end(), run) != p.old_runs.end()) return false;  // late
    if (p.run != 0) {
      // The peer restarted: end the old session, start a new one.
      LOGI("Steam: %s restarted (run %08X -> %08X)", who(p.id).c_str(), p.run, run);
      p.old_runs.push_back(p.run);
      if (p.old_runs.size() > kMaxOldRuns) p.old_runs.erase(p.old_runs.begin());
      push(EventType::Disconnected, p.id, "the other side restarted");
      push(EventType::Connected, p.id);
    }
    p.run = run;
    return true;
  }

  int receive_all() {
    int total = 0;
    for (int ch : {(int)Channel::Control, (int)Channel::Input, kChanTransport}) {
      steamnet::Msg in[32];
      int n;
      while ((n = msgs_->Receive(ch, in, 32)) > 0) {
        total += n;
        for (int i = 0; i < n; ++i) {
          const steamnet::Msg& m = in[i];
          Peer* p = find(m.from);
          if (!p) {
            LOGD("Steam message from %llu outside a session dropped", (unsigned long long)m.from);
            continue;
          }
          if (m.data.size() < kHeader) continue;
          uint32_t run;
          std::memcpy(&run, m.data.data(), kHeader);
          if (!check_run(*p, run)) continue;
          p->last_heard = net::now_ms();
          if (ch == kChanTransport) {
            if (m.data.size() > kHeader && m.data[kHeader] == kBye) {
              LOGI("Steam: %s closed the session", who(m.from).c_str());
              drop(m.from, "the other side closed the session");
            }
            continue;  // keep-alive: last_heard is enough
          }
          if (p->closing) continue;
          Event e;
          e.type = EventType::Data;
          e.peer = PeerId::steam(m.from);
          e.channel = (Channel)ch;
          e.data.assign(m.data.begin() + kHeader, m.data.end());
          queue_.push_back(std::move(e));
        }
      }
    }
    return total;
  }

  void check_pending(uint32_t now) {
    for (size_t i = 0; i < pending_.size();) {
      uint64_t id = pending_[i].id;
      if (accept_ && accept_(id)) {
        accept_session(id);
      } else if (elapsed(now, pending_[i].since) >= (int32_t)kPendingAcceptMs) {
        LOGW("Steam session request from %s refused: not in the lobby and not in allow_steamid",
             who(id).c_str());
        msgs_->Close(id);  // drop what it queued
      } else {
        ++i;
        continue;
      }
      pending_.erase(pending_.begin() + (std::ptrdiff_t)i);
    }
  }

  void keep_alive(uint32_t now) {
    for (const Peer& p : std::vector<Peer>(peers_)) {
      if (p.closing) continue;
      if (elapsed(now, p.last_heard) > (int32_t)kSilenceMs) {
        LOGW("Steam: %s silent for %u s, dropping the session", who(p.id).c_str(), kSilenceMs / 1000);
        drop(p.id, "no messages for " + std::to_string(kSilenceMs / 1000) + " s");
        continue;
      }
      if (elapsed(now, p.last_sent) >= (int32_t)kKeepAliveMs) {
        send_transport(p.id, kKeepAlive, false);
        if (Peer* q = find(p.id)) q->last_sent = now;
      }
    }
  }

  void finish_closing(uint32_t now) {
    for (const Peer& p : std::vector<Peer>(peers_)) {
      if (!p.closing) continue;
      steamnet::LinkStatus st = msgs_->Status(p.id);
      bool delivered = !st.connected || (st.pending_reliable == 0 && st.unacked_reliable == 0);
      if (!delivered && (int32_t)(now - p.close_at) < 0) continue;
      drop(p.id, "session closed");
    }
  }

  void log_status() {
    if (!tlog::enabled(tlog::Debug)) return;
    for (const Peer& p : peers_) {
      steamnet::LinkStatus st = msgs_->Status(p.id);
      LOGD("Steam %llu: %s, %s, ping %d ms, quality %.2f/%.2f, out %.0f pkt/s, pending %d/%d, unacked %d, "
           "heard %u ms ago%s",
           (unsigned long long)p.id, st.state.c_str(), st.relayed ? "relay" : "direct", st.ping_ms,
           st.quality_local, st.quality_remote, st.out_packets_per_s, st.pending_reliable,
           st.pending_unreliable, st.unacked_reliable, net::now_ms() - p.last_heard,
           p.closing ? ", closing" : "");
    }
  }

  std::unique_ptr<steamnet::IMessages> msgs_;
  AcceptFn accept_;
  uint32_t run_;
  std::vector<Peer> peers_;
  std::vector<Pending> pending_;
  std::vector<Event> queue_;
  std::vector<uint8_t> out_;
  uint32_t last_debug_ = 0, last_send_error_ = 0;
};

}  // namespace

std::unique_ptr<ITransport> make_steam_transport_over(std::unique_ptr<steamnet::IMessages> msgs,
                                                      AcceptFn accept) {
  auto t = std::make_unique<SteamTransport>(std::move(msgs), std::move(accept));
  if (!t->Init()) return nullptr;
  return t;
}

}  // namespace transport
