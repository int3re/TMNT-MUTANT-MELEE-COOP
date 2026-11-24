// Two ways to a host at once: guests who came over Steam and guests who dialled an address play in
// one session. A peer's id says which way it is (transport::Kind); the host talks to each by its own.
#pragma once
#include <windows.h>

#include <memory>

#include "common/transport.h"

namespace transport {

class MultiTransport : public ITransport {
 public:
  MultiTransport(std::unique_ptr<ITransport> steam, std::unique_ptr<ITransport> enet)
      : steam_(std::move(steam)), enet_(std::move(enet)) {}

  bool Init() override { return steam_ || enet_; }
  void Poll(uint32_t wait_ms) override {
    if (steam_) steam_->Poll(0);
    if (enet_) enet_->Poll(wait_ms);
    else if (wait_ms) Sleep(wait_ms);
  }
  bool Send(const PeerId& peer, const void* data, size_t size, Channel ch) override {
    ITransport* t = of(peer);
    return t && t->Send(peer, data, size, ch);
  }
  int Receive(Event* out, int max) override {
    int n = steam_ ? steam_->Receive(out, max) : 0;
    if (n < max && enet_) n += enet_->Receive(out + n, max - n);
    return n;
  }
  void Disconnect(const PeerId& peer) override {
    if (ITransport* t = of(peer)) t->Disconnect(peer);
  }
  void Shutdown() override {
    if (steam_) steam_->Shutdown();
    if (enet_) enet_->Shutdown();
  }
  const char* Name() const override { return "steam+address"; }
  bool Connect(const PeerId& peer) override {
    ITransport* t = of(peer);
    return t && t->Connect(peer);
  }
  void Close(const PeerId& peer) override {
    if (ITransport* t = of(peer)) t->Close(peer);
  }
  uint32_t RttMs(const PeerId& peer) override {
    ITransport* t = of(peer);
    return t ? t->RttMs(peer) : 0;
  }
  std::string Describe(const PeerId& peer) override {
    ITransport* t = of(peer);
    return t ? t->Describe(peer) : std::string();
  }

 private:
  ITransport* of(const PeerId& peer) { return peer.kind == Kind::Steam ? steam_.get() : enet_.get(); }
  std::unique_ptr<ITransport> steam_, enet_;
};

}  // namespace transport
