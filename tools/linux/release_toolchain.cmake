# The Linux release build's toolchain (from Zone of the Enders' engine/tools/linux/release_toolchain.cmake, the same
# author's): the port's prebuilt libraries, SDL3, unshield and the launcher compiled with the official LLVM release's
# clang against an Ubuntu 22.04 sysroot (glibc 2.35) - the same clang and sysroot the player's first start compiles
# the lifted code with (tools/linux/release_linux.sh ships them) - so the release runs on every distribution with
# glibc 2.35 or newer.
#
#   cmake -S . -B <dir> -G Ninja -DCMAKE_TOOLCHAIN_FILE=tools/linux/release_toolchain.cmake
#         -DGTA2_LINUX_LLVM=<the official LLVM release, unpacked> -DGTA2_LINUX_SYSROOT=<the dev sysroot> ...
#
# (No CMAKE_SYSTEM_NAME: the build machine is x86-64 Linux too - set, CMake would take it for a cross build.)

if(NOT GTA2_LINUX_LLVM OR NOT GTA2_LINUX_SYSROOT)
    message(FATAL_ERROR "release_toolchain.cmake: -DGTA2_LINUX_LLVM=<LLVM release folder> -DGTA2_LINUX_SYSROOT=<sysroot>")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES GTA2_LINUX_LLVM GTA2_LINUX_SYSROOT GTA2_LINUX_PREFIX)

set(CMAKE_C_COMPILER ${GTA2_LINUX_LLVM}/bin/clang)
set(CMAKE_CXX_COMPILER ${GTA2_LINUX_LLVM}/bin/clang++)
set(CMAKE_ASM_COMPILER ${GTA2_LINUX_LLVM}/bin/clang)
set(CMAKE_AR ${GTA2_LINUX_LLVM}/bin/llvm-ar)
set(CMAKE_RANLIB ${GTA2_LINUX_LLVM}/bin/llvm-ranlib)
set(CMAKE_SYSROOT ${GTA2_LINUX_SYSROOT})
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-linux-gnu)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=bfd -static-libgcc")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=bfd -static-libgcc")

# pkg-config (SDL3's checks) looks into the sysroot only
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${GTA2_LINUX_SYSROOT})
set(ENV{PKG_CONFIG_LIBDIR} "${GTA2_LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/pkgconfig:${GTA2_LINUX_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

set(CMAKE_FIND_ROOT_PATH ${GTA2_LINUX_SYSROOT} ${GTA2_LINUX_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# build_deps.sh's prefix: SDL3 (find_package) and the Vulkan headers (SDL3's own copy; the runtime loads Vulkan
# through SDL at run time, links nothing of it)
if(GTA2_LINUX_PREFIX)
    set(CMAKE_C_FLAGS_INIT "-isystem ${GTA2_LINUX_PREFIX}/include")
endif()
