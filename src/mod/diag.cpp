#include "mod/diag.h"

#include <windows.h>

#include <psapi.h>

#include <cstdio>
#include <cstring>

#include "common/log.h"

namespace diag {
namespace {

volatile LONG g_logged = 0;
const LONG kMaxReports = 4;

void module_of(uintptr_t addr, char* out, size_t n) {
  HMODULE m = nullptr;
  out[0] = 0;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(addr), &m) &&
      m) {
    char path[MAX_PATH];
    GetModuleFileNameA(m, path, MAX_PATH);
    const char* name = strrchr(path, '\\');
    snprintf(out, n, "%s+0x%X", name ? name + 1 : path, (unsigned)(addr - reinterpret_cast<uintptr_t>(m)));
  }
}

bool readable(uintptr_t p) {
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof(mbi))) return false;
  return mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* ep) {
  const EXCEPTION_RECORD* r = ep->ExceptionRecord;
  DWORD code = r->ExceptionCode;
  bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
               code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW ||
               code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_IN_PAGE_ERROR;
  if (!fatal) return EXCEPTION_CONTINUE_SEARCH;
  if (InterlockedIncrement(&g_logged) > kMaxReports) return EXCEPTION_CONTINUE_SEARCH;
  const CONTEXT* c = ep->ContextRecord;
  char mod[320];
  module_of(c->Eip, mod, sizeof(mod));
  LOGE("exception 0x%08X at 0x%08X (%s)%s 0x%08X, thread %lu", code, (unsigned)c->Eip, mod,
       code == EXCEPTION_ACCESS_VIOLATION ? (r->ExceptionInformation[0] == 1 ? " writing" : r->ExceptionInformation[0] == 8 ? " executing" : " reading") : "",
       code == EXCEPTION_ACCESS_VIOLATION ? (unsigned)r->ExceptionInformation[1] : 0u, GetCurrentThreadId());
  LOGE("  eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X", (unsigned)c->Eax, (unsigned)c->Ebx,
       (unsigned)c->Ecx, (unsigned)c->Edx, (unsigned)c->Esi, (unsigned)c->Edi, (unsigned)c->Ebp, (unsigned)c->Esp);
  // Code addresses on the stack: an approximate call chain.
  char line[900];
  int n = 0, hits = 0;
  line[0] = 0;
  for (uintptr_t p = c->Esp & ~3u; hits < 28 && readable(p); p += 4) {
    if ((p & 0xFFF) == 0 && p != (c->Esp & ~3u) && !readable(p)) break;
    uint32_t v = *reinterpret_cast<const uint32_t*>(p);
    if (v >= 0x00401000 && v < 0x0055C000) {
      n += snprintf(line + n, sizeof(line) - n, " %08X", v);
      ++hits;
    }
    if (p - c->Esp > 0x3000) break;
  }
  LOGE("  stack:%s", line);
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void install(const Config&) { AddVectoredExceptionHandler(1, on_exception); }

}  // namespace diag
