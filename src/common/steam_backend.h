// Internal: the few Steam calls meleenet makes, behind interfaces, so the same
// transport and lobby logic runs over real Steam or over the lab mock
// (transport=mock: fake SteamIDs, localhost UDP, lobbies as files), which
// lets two players be tested on one PC with one Steam account or none.
// Network thread only, like all Steam use (steam_boot.h).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/config.h"
#include "common/lobby_steam.h"
#include "common/transport_steam.h"

namespace steamnet {

struct Msg {
  uint64_t from = 0;
  std::vector<uint8_t> data;
};

// A session request (a stranger's first message, held until Accept) or a
// failed session, as ISteamNetworkingMessages reports them.
struct SessionEvent {
  bool failed = false;
  uint64_t peer = 0;
  int end_reason = 0;
  std::string debug;
};

struct LinkStatus {
  bool connected = false;
  bool relayed = false;
  int ping_ms = -1;
  float quality_local = -1, quality_remote = -1, out_packets_per_s = 0;
  int pending_reliable = 0, pending_unreliable = 0, unacked_reliable = 0;
  std::string state;  // for the log
};

class IMessages {
 public:
  virtual ~IMessages() = default;
  // false = the message was not queued; error says why (for the log).
  virtual bool Send(uint64_t to, const void* data, uint32_t size, bool reliable, int channel,
                    int& error) = 0;
  virtual int Receive(int channel, Msg* out, int max) = 0;
  virtual void Accept(uint64_t peer) = 0;
  virtual void Close(uint64_t peer) = 0;
  virtual bool NextSessionEvent(SessionEvent& ev) = 0;
  virtual LinkStatus Status(uint64_t peer) = 0;
  virtual std::string Persona(uint64_t id) = 0;  // "" if unknown
  virtual void LogNetworkState() {}              // relay availability changes
};

}  // namespace steamnet

namespace transport {
// The transport logic (transport_steam.cpp) over a real or mock backend.
std::unique_ptr<ITransport> make_steam_transport_over(std::unique_ptr<steamnet::IMessages> msgs,
                                                      AcceptFn accept);
}  // namespace transport

namespace steam {
// Real Steam (steam_real.cpp, steam_stub.cpp without the SDK).
namespace real {
bool boot();
void pump();
void shutdown();
uint64_t my_id();
std::string my_name();
std::string unavailable_reason();
std::unique_ptr<steamnet::IMessages> messages();
std::unique_ptr<lobby::IHost> host(const Config& cfg, const std::string& exe16);
std::unique_ptr<lobby::IBrowser> browser(const std::string& my_exe16);
std::unique_ptr<lobby::IInvites> invites();
}  // namespace real

// The lab mock (steam_mock.cpp, always built).
namespace mock {
bool boot(const Config& cfg);
void pump();
void shutdown();
uint64_t my_id();
std::string my_name();
std::unique_ptr<steamnet::IMessages> messages();
std::unique_ptr<lobby::IHost> host(const Config& cfg, const std::string& exe16);
std::unique_ptr<lobby::IBrowser> browser(const std::string& my_exe16);
}  // namespace mock
}  // namespace steam
