// Transport abstraction: how meleenet packets travel between the players' PCs.
// ENet (LAN / direct IP) and Steam (ISteamNetworkingMessages via Steam
// Datagram Relay) implement the same interface; the packet format in
// protocol.h knows nothing about the transport.
//
// Threading: not thread-safe; one thread at a time (the network thread).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace transport {

enum class Kind : uint8_t { Enet = 1, Steam = 2 };

// A remote endpoint. ENet: IPv4 (network order) << 16 | port. Steam: SteamID64.
struct PeerId {
  Kind kind = Kind::Enet;
  uint64_t id = 0;

  bool valid() const { return id != 0; }
  bool operator==(const PeerId& o) const { return kind == o.kind && id == o.id; }
  bool operator!=(const PeerId& o) const { return !(*this == o); }

  static PeerId enet(uint32_t ip_network_order, uint16_t port) {
    return {Kind::Enet, (uint64_t)ip_network_order << 16 | port};
  }
  static PeerId steam(uint64_t steam_id64) { return {Kind::Steam, steam_id64}; }
  uint32_t enet_ip() const { return (uint32_t)(id >> 16); }  // network order
  uint16_t enet_port() const { return (uint16_t)(id & 0xFFFF); }
};

std::string to_string(const PeerId& p);  // "192.168.0.12:7777" / "steam:7656119..."

enum class Channel : uint8_t {
  Control = 0,  // reliable, ordered: Hello, HelloAck, Reject, Barrier
  Input = 1,    // unreliable: Input (8 frames of redundancy), StateHash
};

enum class EventType : uint8_t { Connected, Disconnected, Data };

struct Event {
  EventType type = EventType::Data;
  PeerId peer;
  Channel channel = Channel::Control;
  std::vector<uint8_t> data;  // Data only
  std::string reason;         // Disconnected only: why, for the log/UI
};

class ITransport {
 public:
  virtual ~ITransport() = default;

  virtual bool Init() = 0;
  // Pumps the network; may wait up to wait_ms for the first event.
  virtual void Poll(uint32_t wait_ms) = 0;
  virtual bool Send(const PeerId& peer, const void* data, size_t size, Channel ch) = 0;
  // Moves up to max queued events into out; returns how many.
  virtual int Receive(Event* out, int max) = 0;
  // Graceful: queued reliable data is still delivered, then Disconnected.
  virtual void Disconnect(const PeerId& peer) = 0;
  virtual void Shutdown() = 0;
  virtual const char* Name() const = 0;

  // Opens a session to a peer (client side): Connected once the link is up,
  // Disconnected if nobody answers.
  virtual bool Connect(const PeerId& peer) = 0;
  // Immediate, no event: forget the peer (switching hosts, giving up a bye).
  virtual void Close(const PeerId& peer) = 0;
  virtual uint32_t RttMs(const PeerId& peer) = 0;
  // Human-readable link description: "192.168.0.12:7777" / "relay, ping 41 ms".
  virtual std::string Describe(const PeerId& peer) = 0;
};

}  // namespace transport
