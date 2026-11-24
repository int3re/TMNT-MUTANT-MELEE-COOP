#include "common/sha256.h"

#include <windows.h>
#include <bcrypt.h>

#include <vector>

bool sha256_file(const std::wstring& path, uint8_t out[32]) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;

  bool ok = false;
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
      BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
    std::vector<uint8_t> buf(1 << 16);
    DWORD got = 0;
    ok = true;
    while (ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr) && got > 0) {
      if (BCryptHashData(hash, buf.data(), got, 0) != 0) {
        ok = false;
        break;
      }
    }
    ok = ok && BCryptFinishHash(hash, out, 32, 0) == 0;
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(f);
  return ok;
}

std::string to_hex(const uint8_t* data, size_t len) {
  static const char* kDigits = "0123456789abcdef";
  std::string s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    s += kDigits[data[i] >> 4];
    s += kDigits[data[i] & 15];
  }
  return s;
}

bool is_zero(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i)
    if (data[i]) return false;
  return true;
}

bool sha256_bytes(const void* data, size_t len, uint8_t out[32]) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
            BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
            BCryptHashData(hash, (PUCHAR)data, (ULONG)len, 0) == 0 && BCryptFinishHash(hash, out, 32, 0) == 0;
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}
