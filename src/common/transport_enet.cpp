#include "common/transport_enet.h"

#include <enet/enet.h>

#include <algorithm>
#include <cstdio>

#include "common/log.h"
#include "common/protocol.h"

namespace transport {

std::string to_string(const PeerId& p) {
  char buf[64];
  if (p.kind == Kind::Steam) {
    snprintf(buf, sizeof(buf), "steam:%llu", (unsigned long long)p.id);
  } else {
    uint32_t ip = p.enet_ip();  // network order: first octet in the low byte
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF,
             ip >> 24, p.enet_port());
  }
  return buf;
}

namespace {
PeerId id_of(const ENetPeer* p) { return PeerId::enet(p->address.host, p->address.port); }
}  // namespace

bool EnetTransport::Init() {
  if (enet_initialize() != 0) {
    LOGE("enet_initialize failed");
    return false;
  }
  ENetAddress addr{};
  addr.host = ENET_HOST_ANY;
  addr.port = port_;
  host_ = enet_host_create(port_ ? &addr : nullptr, (size_t)max_peers_, mmp::kChannelCount, 0, 0);
  if (!host_) {
    enet_deinitialize();
    return false;
  }
  return true;
}

void EnetTransport::Shutdown() {
  if (!host_) return;
  enet_host_destroy(host_);
  host_ = nullptr;
  peers_.clear();
  queue_.clear();
  enet_deinitialize();
}

ENetPeer* EnetTransport::find(const PeerId& peer) const {
  for (const auto& e : peers_)
    if (e.first == peer) return e.second;
  return nullptr;
}

void EnetTransport::forget(ENetPeer* p) {
  peers_.erase(std::remove_if(peers_.begin(), peers_.end(),
                              [p](const std::pair<PeerId, ENetPeer*>& e) { return e.second == p; }),
               peers_.end());
}

void EnetTransport::Poll(uint32_t wait_ms) {
  if (!host_) return;
  ENetEvent ev;
  // Wait up to wait_ms for the first event, then drain without waiting.
  for (int r = enet_host_service(host_, &ev, wait_ms); r > 0; r = enet_host_service(host_, &ev, 0)) {
    Event out;
    out.peer = id_of(ev.peer);
    switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        // The same address dialling again (client restarted its session)
        // while the old connection has not timed out yet: the old one is gone.
        ENetPeer* old = find(out.peer);
        if (old && old != ev.peer) {
          forget(old);
          enet_peer_reset(old);
          Event gone;
          gone.type = EventType::Disconnected;
          gone.peer = out.peer;
          gone.reason = "reconnected from the same address";
          queue_.push_back(std::move(gone));
        }
        if (old != ev.peer) peers_.emplace_back(out.peer, ev.peer);
        if (timeout_ms_) {
          // Fast "P2 stands still" is the caller's input-stale timer; ENet's
          // own timeout only decides when the connection is really gone. Both
          // sides use the same values: a client on ENet's defaults (up to 30 s)
          // kept "sending" to a host that had already dropped it (chaos test).
          uint32_t tmin = std::max<uint32_t>(5000, timeout_ms_ * 5);
          enet_peer_timeout(ev.peer, ENET_PEER_TIMEOUT_LIMIT, tmin, tmin * 3);
        }
        // No packet throttling: after an RTT spike ENet drops unreliable sends
        // and recovers by 2/32 every 5 s, so input arrived in bursts for many
        // seconds after a blackout (chaos test, bad_wifi). Input is tiny and
        // carries 8 frames of redundancy; dropping it only adds lag.
        enet_peer_throttle_configure(ev.peer, ENET_PEER_PACKET_THROTTLE_INTERVAL, ENET_PEER_PACKET_THROTTLE_SCALE, 0);
        ev.peer->packetThrottle = ENET_PEER_PACKET_THROTTLE_SCALE;
        out.type = EventType::Connected;
        queue_.push_back(std::move(out));
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE:
        out.type = EventType::Data;
        out.channel = ev.channelID == mmp::kChannelInput ? Channel::Input : Channel::Control;
        out.data.assign(ev.packet->data, ev.packet->data + ev.packet->dataLength);
        enet_packet_destroy(ev.packet);
        if (find(out.peer) == ev.peer) queue_.push_back(std::move(out));
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        if (find(out.peer) == ev.peer) {
          forget(ev.peer);
          out.type = EventType::Disconnected;
          out.reason = "connection closed or timed out";
          queue_.push_back(std::move(out));
        }
        break;
      default:
        break;
    }
  }
}

int EnetTransport::Receive(Event* out, int max) {
  int n = std::min<int>(max, (int)queue_.size());
  for (int i = 0; i < n; ++i) out[i] = std::move(queue_[i]);
  queue_.erase(queue_.begin(), queue_.begin() + n);
  return n;
}

bool EnetTransport::Send(const PeerId& peer, const void* data, size_t size, Channel ch) {
  ENetPeer* p = find(peer);
  if (!p) return false;
  bool reliable = ch == Channel::Control;
  ENetPacket* pk = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  if (!pk) return false;
  if (enet_peer_send(p, (enet_uint8)ch, pk) != 0) {
    enet_packet_destroy(pk);
    return false;
  }
  enet_host_flush(host_);  // input must not wait for the next Poll
  return true;
}

bool EnetTransport::Connect(const PeerId& peer) {
  if (!host_ || peer.kind != Kind::Enet) return false;
  if (ENetPeer* old = find(peer)) {
    forget(old);
    enet_peer_reset(old);
  }
  ENetAddress a{};
  a.host = peer.enet_ip();
  a.port = peer.enet_port();
  ENetPeer* p = enet_host_connect(host_, &a, mmp::kChannelCount, 0);
  if (!p) return false;
  peers_.emplace_back(peer, p);
  return true;
}

void EnetTransport::Disconnect(const PeerId& peer) {
  if (ENetPeer* p = find(peer)) enet_peer_disconnect_later(p, 0);
}

void EnetTransport::Close(const PeerId& peer) {
  if (ENetPeer* p = find(peer)) {
    forget(p);
    enet_peer_reset(p);
  }
}

uint32_t EnetTransport::RttMs(const PeerId& peer) {
  ENetPeer* p = find(peer);
  return p ? p->roundTripTime : 0;
}

std::string EnetTransport::Describe(const PeerId& peer) { return to_string(peer); }

}  // namespace transport
