#pragma once
#include <cstdint>
#include <string>

// SHA-256 of a file via BCrypt. Returns false if the file cannot be read.
bool sha256_file(const std::wstring& path, uint8_t out[32]);
bool sha256_bytes(const void* data, size_t len, uint8_t out[32]);
std::string to_hex(const uint8_t* data, size_t len);
bool is_zero(const uint8_t* data, size_t len);
