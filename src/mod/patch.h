// Runtime patching of ztmnt.exe: guarded byte writes, MinHook hooks, import
// and vtable redirection. Nothing here touches the exe on disk; the game's
// addresses are used only after the exe's fingerprint matched (dllmain.cpp).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace patch {

// True if [addr, addr+size) lies inside the exe image.
bool in_image(uintptr_t addr, size_t size);

// True if the bytes at addr are exactly `expect` (the code we are about to change).
bool bytes_are(uintptr_t addr, const void* expect, size_t n);

// Writes bytes into code/rdata (VirtualProtect + FlushInstructionCache).
bool write(uintptr_t addr, const void* bytes, size_t n);

// write() only if the current bytes are `expect`; logs the outcome under `name`.
bool replace(const char* name, uintptr_t addr, const void* expect, const void* bytes, size_t n);

template <typename T>
T read(uintptr_t addr) {
  T v;
  std::memcpy(&v, reinterpret_cast<const void*>(addr), sizeof(T));
  return v;
}

// MinHook create+enable (initializes MinHook once). Logs the outcome.
bool hook(const char* name, uintptr_t target, void* detour, void** original);

// hook() for an export of a system dll (`dll` must be loaded already).
bool hook_api(const char* dll, const char* func, void* detour, void** original);

// Points the exe's import `dll!func` at `hook`; the previous target goes to *original.
bool import(const char* dll, const char* func, void* hook, void** original);

// Replaces a pointer-sized slot (vtable entry) if it currently holds `expect`.
bool slot(const char* name, uintptr_t addr, uintptr_t expect, void* value);

}  // namespace patch
