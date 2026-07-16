# CMake toolchain: cross-compile wxNote for Linux/riscv64 on an x86_64 host.
#
# Used only by the CI "linux-riscv64" leg (.github/workflows/build.yml), which is the
# ONLY place this is exercised - the Windows dev machine cannot run it. Assumes an
# Ubuntu-multiarch host set up by that job:
#   * cross toolchain:  g++-riscv64-linux-gnu  (provides riscv64-linux-gnu-gcc/g++)
#   * runnable target:  qemu-user-static + binfmt  (so wxWidgets' try_run checks execute)
#   * riscv64 sysroot IN the host root via `dpkg --add-architecture riscv64` +
#     `apt install libgtk-3-dev:riscv64` -> headers in /usr/include, libs + .pc files
#     under /usr/lib/riscv64-linux-gnu. There is NO separate sysroot dir, so the find
#     root is '/'.
#
# One toolchain file covers both the FetchContent'd wxWidgets build and wxNote itself.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(CMAKE_C_COMPILER   riscv64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER riscv64-linux-gnu-g++)

# Multiarch: the riscv64 libs/headers live in the normal host root, so restrict library/
# include/package searches to the riscv64 multiarch dirs while still finding host tools
# (pkg-config, ninja) on PATH.
# riscv64 multiarch dirs FIRST (so find_library prefers them), then /usr so find_path resolves the
# arch-independent headers in /usr/include. CMAKE_LIBRARY_ARCHITECTURE makes find_library target the
# riscv64 multiarch libdir rather than the host's amd64 one.
set(CMAKE_LIBRARY_ARCHITECTURE riscv64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH /usr/riscv64-linux-gnu /usr/lib/riscv64-linux-gnu /usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Let CMake/wxWidgets try_run() checks execute riscv64 binaries under emulation.
set(CMAKE_CROSSCOMPILING_EMULATOR /usr/bin/qemu-riscv64-static)

# Point pkg-config at the riscv64 .pc files. With multiarch the .pc prefixes already
# resolve to /usr, so PKG_CONFIG_SYSROOT_DIR stays unset. LIBDIR (not PATH) so the host's
# amd64 .pc files are NOT consulted.
set(ENV{PKG_CONFIG_LIBDIR}   "/usr/lib/riscv64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH}     "")
unset(ENV{PKG_CONFIG_SYSROOT_DIR})
set(ENV{PKG_CONFIG_ALLOW_SYSTEM_CFLAGS} "1")
set(ENV{PKG_CONFIG_ALLOW_SYSTEM_LIBS}   "1")
