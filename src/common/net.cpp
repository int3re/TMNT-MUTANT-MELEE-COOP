#include "common/net.h"

#include <iphlpapi.h>
#include <mstcpip.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#include <cstdio>

namespace net {

bool startup() {
  WSADATA wsa;
  return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

uint32_t now_ms() {
  static LARGE_INTEGER freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return (uint32_t)((c.QuadPart * 1000) / freq.QuadPart);
}

std::string Addr::str() const {
  char ip[INET_ADDRSTRLEN] = "?";
  inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
  char buf[64];
  snprintf(buf, sizeof(buf), "%s:%u", ip, ntohs(sa.sin_port));
  return buf;
}

SOCKET udp_socket(uint16_t port, bool broadcast) {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return s;

  BOOL off = FALSE;
  DWORD ret = 0;
  WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
  if (broadcast) {
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(on));
  }
  u_long nonblocking = 1;
  ioctlsocket(s, FIONBIO, &nonblocking);

  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(port);
  if (bind(s, (sockaddr*)&local, sizeof(local)) != 0) {
    closesocket(s);
    return INVALID_SOCKET;
  }
  return s;
}

bool resolve(const std::string& host, uint16_t port, Addr& out) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
  out.sa = *(sockaddr_in*)res->ai_addr;
  out.sa.sin_port = htons(port);
  freeaddrinfo(res);
  return true;
}

std::vector<Addr> broadcast_addrs(uint16_t port) {
  std::vector<Addr> out;
  auto add = [&](uint32_t ip_host_order) {
    Addr a;
    a.sa.sin_family = AF_INET;
    a.sa.sin_addr.s_addr = htonl(ip_host_order);
    a.sa.sin_port = htons(port);
    for (const Addr& o : out)
      if (o == a) return;
    out.push_back(a);
  };
  add(INADDR_BROADCAST);

  ULONG size = 16 * 1024;
  std::vector<uint8_t> buf(size);
  auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) == ERROR_BUFFER_OVERFLOW) {
    buf.resize(size);
    list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  }
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) != NO_ERROR) return out;
  for (auto* ad = list; ad; ad = ad->Next) {
    if (ad->OperStatus != IfOperStatusUp || ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (auto* u = ad->FirstUnicastAddress; u; u = u->Next) {
      auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
      uint8_t prefix = u->OnLinkPrefixLength;
      if (sin->sin_family != AF_INET || prefix == 0 || prefix >= 31) continue;
      uint32_t ip = ntohl(sin->sin_addr.s_addr);
      uint32_t mask = 0xFFFFFFFFu << (32 - prefix);
      add(ip | ~mask);
    }
  }
  return out;
}

std::vector<LocalAddr> local_ipv4() {
  std::vector<LocalAddr> out;
  ULONG size = 16 * 1024;
  std::vector<uint8_t> buf(size);
  auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
                GAA_FLAG_INCLUDE_GATEWAYS;
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) == ERROR_BUFFER_OVERFLOW) {
    buf.resize(size);
    list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  }
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) != NO_ERROR) return out;
  for (auto* ad = list; ad; ad = ad->Next) {
    if (ad->OperStatus != IfOperStatusUp || ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    char name[128] = "";
    WideCharToMultiByte(CP_UTF8, 0, ad->FriendlyName, -1, name, sizeof(name), nullptr, nullptr);
    for (auto* u = ad->FirstUnicastAddress; u; u = u->Next) {
      auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
      if (sin->sin_family != AF_INET) continue;
      uint8_t first = (uint8_t)(ntohl(sin->sin_addr.s_addr) >> 24);
      if (first == 169) continue;  // link-local: no DHCP, useless for others
      LocalAddr a{sin->sin_addr.s_addr, name};
      // Adapters with a default gateway (the real LAN) go first.
      if (ad->FirstGatewayAddress)
        out.insert(out.begin(), a);
      else
        out.push_back(a);
    }
  }
  return out;
}

int send_to(SOCKET s, const void* data, int len, const Addr& to) {
  return sendto(s, (const char*)data, len, 0, (const sockaddr*)&to.sa, sizeof(to.sa));
}

int recv_from(SOCKET s, void* buf, int len, Addr& from) {
  int fromlen = sizeof(from.sa);
  int n = recvfrom(s, (char*)buf, len, 0, (sockaddr*)&from.sa, &fromlen);
  return n == SOCKET_ERROR ? -1 : n;
}

int wait_readable(const SOCKET* socks, int n, int timeout_ms, bool* ready) {
  fd_set rd;
  FD_ZERO(&rd);
  for (int i = 0; i < n; ++i)
    if (socks[i] != INVALID_SOCKET) FD_SET(socks[i], &rd);
  timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  int r = select(0, &rd, nullptr, nullptr, &tv);
  for (int i = 0; i < n; ++i)
    ready[i] = r > 0 && socks[i] != INVALID_SOCKET && FD_ISSET(socks[i], &rd);
  return r;
}

}  // namespace net
