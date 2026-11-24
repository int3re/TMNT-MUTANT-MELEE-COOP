// HTTP for the launcher: GET through WinHTTP, by a given route (the system's proxy settings, no proxy, a
// named proxy), with redirects followed or reported, the body kept or written to a file.
#include <winsock2.h>
#include <ws2tcpip.h>

#include "launcher/launcher.h"

#include <winhttp.h>

namespace lz {
namespace net {
namespace {

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
  operator HINTERNET() const { return h; }
};

}  // namespace

Response get(const std::wstring& url, const Route& route, bool follow, const std::wstring& to_file,
             const std::function<void(uint64_t, uint64_t)>& progress, const std::atomic<bool>* cancel) {
  Response r;
  wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host;
  uc.dwHostNameLength = 255;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2047;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2047;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
    r.error = GetLastError();
    return r;
  }
  bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
  Handle ses, con, req;
  const wchar_t* agent = L"meleenet-launcher/1.0";
  if (route.kind == 2) {
    ses.h = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_NAMED_PROXY, route.proxy.c_str(), WINHTTP_NO_PROXY_BYPASS, 0);
  } else if (route.kind == 1) {
    ses.h = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  } else {
    ses.h = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses.h)  // (before Windows 8.1)
      ses.h = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!ses.h) {
    r.error = GetLastError();
    return r;
  }
  WinHttpSetTimeouts(ses, 8000, 8000, 15000, 20000);
  con.h = WinHttpConnect(ses, host, uc.nPort, 0);
  if (!con.h) {
    r.error = GetLastError();
    return r;
  }
  std::wstring object = std::wstring(path) + extra;
  req.h = WinHttpOpenRequest(con, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             secure ? WINHTTP_FLAG_SECURE : 0);
  if (!req.h) {
    r.error = GetLastError();
    return r;
  }
  if (!follow) {
    DWORD off = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof(off));
  }
  if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(req, nullptr)) {
    r.error = GetLastError();
    return r;
  }
  r.sent = true;
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                      WINHTTP_NO_HEADER_INDEX);
  r.status = static_cast<int>(status);
  wchar_t loc[2048] = {};
  size = sizeof(loc) - sizeof(wchar_t);
  if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &size, WINHTTP_NO_HEADER_INDEX))
    r.location = loc;
  DWORD length = 0;
  size = sizeof(length);
  uint64_t total = 0;
  if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &length,
                          &size, WINHTTP_NO_HEADER_INDEX))
    total = length;

  HANDLE file = INVALID_HANDLE_VALUE;
  bool to_disk = !to_file.empty() && r.status == 200;
  if (to_disk) {
    file = CreateFileW(to_file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      r.error = GetLastError();
      return r;
    }
  }
  std::vector<char> buf(64 * 1024);
  uint64_t got = 0;
  for (;;) {
    if (cancel && cancel->load()) {
      r.error = ERROR_CANCELLED;
      break;
    }
    DWORD n = 0;
    if (!WinHttpReadData(req, buf.data(), static_cast<DWORD>(buf.size()), &n)) {
      r.error = GetLastError();
      break;
    }
    if (!n) break;
    got += n;
    if (to_disk) {
      DWORD written = 0;
      if (!WriteFile(file, buf.data(), n, &written, nullptr) || written != n) {
        r.error = GetLastError();
        break;
      }
      if (progress) progress(got, total);
    } else if (r.body.size() < 4 * 1024 * 1024) {
      r.body.append(buf.data(), n);
    }
  }
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  if (to_disk && !r.error && total && got != total) r.error = ERROR_HANDLE_EOF;  // (cut short)
  if (to_disk && r.error) DeleteFileW(to_file.c_str());
  return r;
}

bool port_open(const wchar_t* host, int port, int timeout_ms) {
  static bool started = false;
  if (!started) {
    WSADATA wsa;
    started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
  }
  if (!started) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port));
  if (InetPtonW(AF_INET, host, &addr.sin_addr) != 1) return false;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  u_long nonblocking = 1;
  ioctlsocket(s, FIONBIO, &nonblocking);
  bool ok = false;
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
    ok = true;
  } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
    fd_set w, e;  // (a refusal comes as an exception: no waiting for the timeout then)
    FD_ZERO(&w);
    FD_SET(s, &w);
    FD_ZERO(&e);
    FD_SET(s, &e);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    ok = select(0, nullptr, &w, &e, &tv) > 0 && FD_ISSET(s, &w);
  }
  closesocket(s);
  return ok;
}

}  // namespace net
}  // namespace lz
