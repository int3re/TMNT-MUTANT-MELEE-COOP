// Steam transport: ISteamNetworkingMessages (Steam Datagram Relay picks a
// direct or relayed route, NAT traversal included). Control = reliable with
// session auto-restart, Input = unreliable; no Nagle delay on either. Adds
// keep-alives, a silence timeout and restart detection (transport_steam.cpp).
#pragma once
#include <functional>
#include <memory>

#include "common/transport.h"

namespace transport {

// Who may open a session with us (host: lobby members and allow_steamid;
// client: the host it dials). Others are refused and logged.
using AcceptFn = std::function<bool(uint64_t steam_id64)>;

// nullptr when Steam (or the lab mock) is not ready: steam::boot() first.
std::unique_ptr<ITransport> make_steam_transport(AcceptFn accept);

}  // namespace transport
