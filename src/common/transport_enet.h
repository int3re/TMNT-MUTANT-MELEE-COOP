// ENet transport: LAN / direct IP, also the fallback when Steam is not there.
#pragma once
#include "common/transport.h"

struct _ENetHost;
struct _ENetPeer;

namespace transport {

class EnetTransport : public ITransport {
 public:
  // Host: listens on port, accepts up to max_peers. Client: port 0, 1 peer.
  // timeout_ms: ENet disconnects a silent peer after about max(5 s, 5 x timeout_ms).
  EnetTransport(uint16_t port, int max_peers, uint32_t timeout_ms)
      : port_(port), max_peers_(max_peers), timeout_ms_(timeout_ms) {}
  ~EnetTransport() override { Shutdown(); }

  bool Init() override;
  void Poll(uint32_t wait_ms) override;
  bool Send(const PeerId& peer, const void* data, size_t size, Channel ch) override;
  int Receive(Event* out, int max) override;
  void Disconnect(const PeerId& peer) override;
  void Shutdown() override;
  const char* Name() const override { return "ENet"; }
  bool Connect(const PeerId& peer) override;
  void Close(const PeerId& peer) override;
  uint32_t RttMs(const PeerId& peer) override;
  std::string Describe(const PeerId& peer) override;

 private:
  _ENetPeer* find(const PeerId& peer) const;
  void forget(_ENetPeer* p);

  uint16_t port_;
  int max_peers_;
  uint32_t timeout_ms_;
  _ENetHost* host_ = nullptr;
  std::vector<std::pair<PeerId, _ENetPeer*>> peers_;
  std::vector<Event> queue_;
};

}  // namespace transport
