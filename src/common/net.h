// Thin Winsock UDP helpers shared by the mod and the client.
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <string>
#include <vector>

namespace net {

bool startup();
uint32_t now_ms();  // monotonic, QueryPerformanceCounter based

struct Addr {
  sockaddr_in sa{};
  bool valid() const { return sa.sin_family == AF_INET; }
  bool same_host(const Addr& o) const { return sa.sin_addr.s_addr == o.sa.sin_addr.s_addr; }
  bool operator==(const Addr& o) const {
    return same_host(o) && sa.sin_port == o.sa.sin_port;
  }
  bool operator!=(const Addr& o) const { return !(*this == o); }
  std::string str() const;
};

// Creates a UDP socket bound to INADDR_ANY:port (0 = ephemeral). Returns
// INVALID_SOCKET on failure. ICMP "port unreachable" resets are disabled so a
// vanished peer never breaks recvfrom.
SOCKET udp_socket(uint16_t port, bool broadcast);
bool resolve(const std::string& host, uint16_t port, Addr& out);
// 255.255.255.255 plus the directed broadcast of every active IPv4 adapter:
// Windows sends the limited broadcast through one interface only, which
// misses the LAN on PCs with VPN/Docker/VirtualBox adapters.
std::vector<Addr> broadcast_addrs(uint16_t port);

// Active non-loopback IPv4 addresses with the adapter's friendly name, the
// adapter with the default route first.
struct LocalAddr {
  uint32_t ip;  // network order
  std::string adapter;
};
std::vector<LocalAddr> local_ipv4();
int send_to(SOCKET s, const void* data, int len, const Addr& to);
// Returns bytes received, or -1 if nothing is pending / on error.
int recv_from(SOCKET s, void* buf, int len, Addr& from);
// Waits until any socket is readable (or timeout). ready[i] set per socket.
int wait_readable(const SOCKET* socks, int n, int timeout_ms, bool* ready);

}  // namespace net
