// SPDX-License-Identifier: Apache-2.0
//
// wxNote - plugin ABI compatibility layer: cross-platform type shims.
// Copyright 2026 The wxNote Authors.
//
// This header is original work, written for wxNote. It declares portable
// equivalents of the Win32 scalar types in which the Notepad++ plugin ABI happens
// to be expressed, so that the same interface is usable on every platform:
//
//   * on Windows  -> these resolve to the native Win32 types, so existing
//                    Notepad++ plugin *binaries* stay ABI-compatible; and
//   * on Linux /  -> they resolve to portable C++ equivalents, so Notepad++
//     macOS          plugin *sources* can simply be recompiled.
//
// No Notepad++ source code is reproduced here - only the portable type vocabulary
// required to express a binary-compatible interface. Notepad++ is a trademark of
// its owner; wxNote is an independent, unaffiliated reimplementation.

#pragma once

#ifdef _WIN32

  #include <windows.h>
  #define NPP_CDECL  __cdecl
  #define NPP_EXPORT __declspec(dllexport)

#else  // ---------------- Linux / macOS portable equivalents ----------------

  #include <cstdint>

  using HWND      = void*;
  using HMENU     = void*;
  using HICON     = void*;
  using HBITMAP   = void*;
  using HINSTANCE = void*;
  using HANDLE    = void*;
  using LRESULT   = intptr_t;
  using WPARAM    = uintptr_t;
  using LPARAM    = intptr_t;
  using UINT      = unsigned int;
  using INT       = int;
  using LONG      = long;
  using ULONG     = unsigned long;
  using BOOL      = int;
  using BYTE      = unsigned char;
  using UCHAR     = unsigned char;
  using WORD      = unsigned short;
  using DWORD     = unsigned int;
  using COLORREF  = unsigned int;
  using TCHAR     = wchar_t;
  using LPCWSTR   = const wchar_t*;
  using LPWSTR    = wchar_t*;

  struct RECT  { long left = 0, top = 0, right = 0, bottom = 0; };
  struct POINT { long x = 0, y = 0; };

  #ifndef TRUE
    #define TRUE  1
    #define FALSE 0
  #endif
  #ifndef WM_USER
    #define WM_USER 0x0400
  #endif
  #ifndef MAX_PATH
    #define MAX_PATH 260
  #endif

  #define NPP_CDECL
  #define NPP_EXPORT __attribute__((visibility("default")))

#endif
