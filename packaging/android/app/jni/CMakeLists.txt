# Grand Theft Auto 2 Recompiled for Android - the app's own native libraries (agent L, 2026-11-26; from the Zone of
# the Enders Recompiled Android app).
# libvulkan_port.so: the game's Vulkan library - the system driver, or the player's Turnip through libadrenotools
# (vkshim/vulkan_port.c, from the Orta / ZOE Recompiled Android apps). libadrenotools (BSD-2): -DADRENOTOOLS_SRC=<source>;
# its hook libraries (main_hook, hook_impl, file_redirect_hook, gsl_alloc_hook) land in nativeLibraryDir with it.
# libgta2_app.so: the app's own JNI part (app/touchpad.c: the touch controls as a virtual SDL gamepad), linked against
# the APK's libSDL3.so: -DSDL3_SRC=<SDL3 source> (headers: ${SDL3_SRC}/include), -DSDL3_LIB=<that libSDL3.so>.
cmake_minimum_required(VERSION 3.22)
project(gta2_android_native C CXX ASM)
if(NOT ADRENOTOOLS_SRC)
    message(FATAL_ERROR "-DADRENOTOOLS_SRC=<libadrenotools source> missing")
endif()
add_subdirectory(${ADRENOTOOLS_SRC} adrenotools EXCLUDE_FROM_ALL)
add_library(vulkan_port SHARED vkshim/vulkan_port.c vkshim/vk_trampolines.S)
set_target_properties(vulkan_port PROPERTIES LINKER_LANGUAGE CXX)   # libadrenotools is C++ (static libc++)
target_link_libraries(vulkan_port PRIVATE adrenotools linkernsbypass log dl)
target_include_directories(vulkan_port PRIVATE ${ADRENOTOOLS_SRC}/lib/linkernsbypass)
# --exclude-libs: linkernsbypass's function-pointer variables must not leave this library (Orta, Tab S8 2026-11-11)
target_link_options(vulkan_port PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,-soname,libvulkan_port.so"
                    "-Wl,--exclude-libs,ALL")

if(NOT SDL3_SRC OR NOT SDL3_LIB)
    message(FATAL_ERROR "-DSDL3_SRC=<SDL3 source> / -DSDL3_LIB=<the APK's arm64 libSDL3.so> missing")
endif()
add_library(sdl3_shared SHARED IMPORTED)
set_target_properties(sdl3_shared PROPERTIES IMPORTED_LOCATION "${SDL3_LIB}"
                      INTERFACE_INCLUDE_DIRECTORIES "${SDL3_SRC}/include")
add_library(gta2_app SHARED app/touchpad.c)
target_link_libraries(gta2_app PRIVATE sdl3_shared log)
target_compile_options(gta2_app PRIVATE -Wall -Wextra)
target_link_options(gta2_app PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,-soname,libgta2_app.so")

add_custom_target(gta2_native ALL DEPENDS vulkan_port gta2_app main_hook hook_impl file_redirect_hook gsl_alloc_hook)

# The setup's programs (SetupRunner; an app may exec only from nativeLibraryDir, so they ship as lib*_exe.so):
# libpython_exe.so (python.org's Android CPython, embedded: setup_build.py extract / lift), libld_exe.so (clang's
# --ld-path: execs B's liblld_exe.so as "ld.lld"). libunshield_exe.so (the installer's cabinets) comes prebuilt in
# the libs folder (-PprebuiltLibs), like libclang_exe.so and liblld_exe.so.
# -DPYTHON_PREFIX=<python.org Android package prefix> (libpython3.15.so + include/python3.15).
if(NOT PYTHON_PREFIX)
    message(FATAL_ERROR "-DPYTHON_PREFIX=<python.org Android package prefix> missing")
endif()
add_library(gta2_python SHARED IMPORTED)
set_target_properties(gta2_python PROPERTIES IMPORTED_LOCATION "${PYTHON_PREFIX}/lib/libpython3.15.so"
                      INTERFACE_INCLUDE_DIRECTORIES "${PYTHON_PREFIX}/include/python3.15")
foreach(exe python_exe ld_exe)
    add_executable(${exe} setup/${exe}.c)
    target_link_options(${exe} PRIVATE "-Wl,-z,max-page-size=16384")
endforeach()
set_target_properties(python_exe PROPERTIES OUTPUT_NAME "libpython_exe" SUFFIX ".so")
set_target_properties(ld_exe PROPERTIES OUTPUT_NAME "libld_exe" SUFFIX ".so")
target_link_libraries(python_exe PRIVATE gta2_python)
add_dependencies(gta2_native python_exe ld_exe)
