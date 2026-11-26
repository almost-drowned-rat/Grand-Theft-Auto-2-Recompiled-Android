# GTA 2 Recompiled - the Linux release launcher (launcher.h): one small program, SDL3 linked in, that builds the game
# from the player's GTA 2 freeware installer at the first start and starts it. A program of its own, not the game.
#
# Taken from Zone of the Enders Recompiled's engine/launcher (ps2recomp-option-p, the same author's, GPL-3.0-or-later,
# at ad3b491) and adapted to GTA 2 (an installer instead of a disc image, no CHD); this folder is under the GPL v3
# (LICENSE here), the rest of the port is not affected - the launcher is a separate program.
#
#   cmake -S launcher -B <dir> -G Ninja -DCMAKE_TOOLCHAIN_FILE=tools/linux/release_toolchain.cmake ...
#         -DGTA2_LAUNCHER_SDL3=<an SDL 3.4.x source folder> [-DGTA2_LAUNCHER_ICON=<a 64x64 32-bit BMP>]
# (tools/linux/release_linux.sh does it.)
cmake_minimum_required(VERSION 3.20)
project(gta2_launcher LANGUAGES C)

set(GTA2_LAUNCHER_SDL3 "" CACHE PATH "an SDL3 source folder (linked in statically)")
set(GTA2_LAUNCHER_ICON "" CACHE FILEPATH "a BMP (32-bit with alpha, 64x64) for the setup window's icon (empty: none)")
set(GTA2_LAUNCHER_OUTPUT "gta2_launcher" CACHE STRING "the launcher's program name")
if(NOT GTA2_LAUNCHER_SDL3)
    message(FATAL_ERROR "-DGTA2_LAUNCHER_SDL3=<an SDL3 source folder>")
endif()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${GTA2_LAUNCHER_SDL3} ${CMAKE_BINARY_DIR}/sdl3 EXCLUDE_FROM_ALL)

# The window (game_setup_ui_win95_sdl.c, the Windows 2000 look): its font Liberation Sans (fonts/, SIL OFL 1.1) by
# stb_truetype (public domain / MIT, fetched at a fixed commit and hash), both built in.
set(launcher_gen ${CMAKE_CURRENT_BINARY_DIR}/launcher_generated)
if(NOT EXISTS ${launcher_gen}/stb_truetype.h)
    file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_truetype.h
         ${launcher_gen}/stb_truetype.h
         EXPECTED_HASH SHA256=ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab TLS_VERIFY ON)
endif()
foreach(face Regular Bold)
    string(TOLOWER ${face} face_lower)
    set(font_file ${CMAKE_CURRENT_LIST_DIR}/fonts/LiberationSans-${face}.ttf)
    set(font_inc ${launcher_gen}/liberation_sans_${face_lower}.inc)
    if(NOT EXISTS ${font_inc} OR ${font_file} IS_NEWER_THAN ${font_inc})
        file(READ ${font_file} font_hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," font_bytes "${font_hex}")
        file(WRITE ${font_inc} "// From launcher/fonts/LiberationSans-${face}.ttf: generated.\n${font_bytes}\n")
    endif()
endforeach()

add_executable(gta2_launcher
    launcher_main.c
    launcher.c
    game_setup_ui_win95_sdl.c
    sha256.c
    zip_read.c
    third_party/miniz/miniz_tinfl.c)
target_include_directories(gta2_launcher PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/third_party/miniz
                                                 ${CMAKE_CURRENT_BINARY_DIR} ${launcher_gen})
# miniz (3.1.2, MIT, third_party/miniz/LICENSE): only its inflate; miniz_export.h, which its own build makes, empty
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/miniz_export.h "#define MINIZ_EXPORT\n")
target_compile_definitions(gta2_launcher PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_DEFLATE_APIS
                           MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_source_files_properties(third_party/miniz/miniz_tinfl.c PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(gta2_launcher PRIVATE SDL3::SDL3-static m)
# The setup window's icon (title bar, task bar): the BMP built in as launcher_icon.inc
if(GTA2_LAUNCHER_ICON)
    set(icon_inc ${CMAKE_CURRENT_BINARY_DIR}/launcher_icon.inc)
    file(READ ${GTA2_LAUNCHER_ICON} icon_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," icon_bytes "${icon_hex}")
    file(WRITE ${icon_inc} "// From ${GTA2_LAUNCHER_ICON}: generated.\n${icon_bytes}\n")
    target_compile_definitions(gta2_launcher PRIVATE GTA2_LAUNCHER_ICON_INC=1)
endif()
set_target_properties(gta2_launcher PROPERTIES OUTPUT_NAME ${GTA2_LAUNCHER_OUTPUT})
