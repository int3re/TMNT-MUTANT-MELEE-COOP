// The little of Direct3D 8 the mod needs. Today's Windows SDK has no d3d8.h: the two interfaces are
// called through their tables by number (the order of d3d8.h), the structures are written out.
#pragma once
#include <windows.h>

#include <cstdint>

namespace d3d8 {

struct PresentParameters {
  UINT BackBufferWidth, BackBufferHeight;
  DWORD BackBufferFormat;
  UINT BackBufferCount;
  DWORD MultiSampleType;
  DWORD SwapEffect;
  HWND hDeviceWindow;
  BOOL Windowed;
  BOOL EnableAutoDepthStencil;
  DWORD AutoDepthStencilFormat;
  DWORD Flags;
  UINT FullScreen_RefreshRateInHz;
  UINT FullScreen_PresentationInterval;
};

struct DisplayMode {
  UINT Width, Height, RefreshRate;
  DWORD Format;
};

struct SurfaceDesc {
  DWORD Format, Type, Usage, Pool;
  UINT Size;
  DWORD MultiSampleType;
  UINT Width, Height;
};

struct LockedRect {
  INT Pitch;
  void* pBits;
};

struct Caps {
  DWORD DeviceType;
  UINT AdapterOrdinal;
  DWORD Caps, Caps2, Caps3, PresentationIntervals, CursorCaps, DevCaps, PrimitiveMiscCaps, RasterCaps, ZCmpCaps,
      SrcBlendCaps, DestBlendCaps, AlphaCmpCaps, ShadeCaps, TextureCaps, TextureFilterCaps, CubeTextureFilterCaps,
      VolumeTextureFilterCaps, TextureAddressCaps, VolumeTextureAddressCaps, LineCaps;
  DWORD MaxTextureWidth, MaxTextureHeight, MaxVolumeExtent, MaxTextureRepeat, MaxTextureAspectRatio, MaxAnisotropy;
  DWORD rest[32];
};

enum : DWORD {
  kFmtR8G8B8 = 20,
  kFmtA8R8G8B8 = 21,
  kFmtX8R8G8B8 = 22,
  kFmtR5G6B5 = 23,
  kFmtD16 = 80,
  kFmtD24S8 = 75,
  kFmtD24X8 = 77,
  kSwapDiscard = 1,
  kSwapFlip = 2,
  kSwapCopy = 3,
  kSwapCopyVsync = 4,
  kDevHal = 1,
  kLockReadOnly = 0x10,
  kPresentFlagLockable = 1,
  // texture stage states
  kTssMagFilter = 16,
  kTssMinFilter = 17,
  kTssMipFilter = 18,
  kTssMaxAnisotropy = 21,
  kTexfLinear = 2,
  kTexfAnisotropic = 3,
  kFilterCapMinAniso = 0x400,
  kFilterCapMagAniso = 0x4000000,
};

// Slots of IDirect3D8.
enum {
  kD3dGetAdapterModeCount = 6,
  kD3dEnumAdapterModes = 7,
  kD3dGetAdapterDisplayMode = 8,
  kD3dCheckDeviceMultiSampleType = 11,
  kD3dCreateDevice = 15,
};

// Slots of IDirect3DDevice8.
enum {
  kDevTestCooperativeLevel = 3,
  kDevGetDeviceCaps = 7,
  kDevReset = 14,
  kDevPresent = 15,
  kDevGetBackBuffer = 16,
  kDevCreateImageSurface = 27,
  kDevCopyRects = 28,
  kDevSetViewport = 40,
  kDevGetViewport = 41,
  kDevSetTextureStageState = 63,
};

// Slots of IDirect3DSurface8.
enum { kSurfGetDesc = 8, kSurfLockRect = 9, kSurfUnlockRect = 10 };

inline void** vtable(void* obj) { return *reinterpret_cast<void***>(obj); }

template <typename R, typename... A>
R call(void* obj, int slot, A... args) {
  using Fn = R(__stdcall*)(void*, A...);
  return reinterpret_cast<Fn>(vtable(obj)[slot])(obj, args...);
}

inline ULONG release(void* obj) { return call<ULONG>(obj, 2); }

}  // namespace d3d8
