cmake_minimum_required(VERSION 3.20)
project(gta2_recomp C)

# the log's head names the build: the port's commit (and "dirty" with local changes),
# refreshed on every build (tools/git_id.cmake writes build/gta2_git.h only when it changed)
add_custom_target(gta2_git_id ALL
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_BINARY_DIR}/gta2_git.h
            -P ${CMAKE_SOURCE_DIR}/tools/git_id.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/gta2_git.h)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(WINRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../winrecomp" CACHE PATH "winrecomp toolkit")
set(RECOMP_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gen/src" CACHE PATH "generated C (tools/lift.sh)")

add_subdirectory(${WINRECOMP_DIR}/runtime ${CMAKE_BINARY_DIR}/winrecomp_rt)

# GTA2_PORT_ONLY (Android setup kit): the port and the runtime WITHOUT generated code, as a static library the
# device links with the code it lifts and compiles from the player's own game (no game code in the APK).
option(GTA2_PORT_ONLY "build only libgta2_port.a (no generated code)" OFF)
if(NOT GTA2_PORT_ONLY)
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${RECOMP_GEN_DIR}/*.c")
if(NOT RECOMP_GEN_SOURCES)
    message(FATAL_ERROR "No generated C in ${RECOMP_GEN_DIR} - run tools/lift.sh first")
endif()

# The generated code: one object library, optimised even in debug builds
# (an unoptimised 1.1M-line translation is unusably slow).
add_library(gta2_gen OBJECT ${RECOMP_GEN_SOURCES})
target_include_directories(gta2_gen PRIVATE ${RECOMP_GEN_DIR})
if(NOT MSVC)
    target_compile_options(gta2_gen PRIVATE -O2 -w -fwrapv -fno-strict-aliasing)
endif()

# binkw32.dll, lifted the same way (tools/lift.sh bink) - the original decoder
set(BINK_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gen_bink/src")
file(GLOB BINK_GEN_SOURCES CONFIGURE_DEPENDS "${BINK_GEN_DIR}/*.c")
add_library(bink_gen OBJECT ${BINK_GEN_SOURCES})
target_include_directories(bink_gen PRIVATE ${BINK_GEN_DIR})
if(NOT MSVC)
    target_compile_options(bink_gen PRIVATE -O2 -w -fwrapv -fno-strict-aliasing)
endif()
endif()   # NOT GTA2_PORT_ONLY

set(GTA2_PORT_SOURCES
    src/main.c
    src/gta2_registry.c
    src/gta2_manual.c
    src/gta2_input.c
    src/gta2_options.c
    src/gta2_menu.c
    src/gta2_state.c
    src/gta2_gameplay.c
    src/gta2_shaders.c
    src/gta2_padview.c
    src/gta2_framerate.c
    src/gta2_rumble.c
    src/gta2_mixtape.c
    src/gta2_translations.c
    src/gta2_text.c
    src/render/vid.c
    src/render/gbh.c
    src/render/bink_buffer.c
    $<TARGET_OBJECTS:gta2_gen>
    $<TARGET_OBJECTS:bink_gen>)
if(GTA2_PORT_ONLY)
    list(FILTER GTA2_PORT_SOURCES EXCLUDE REGEX "TARGET_OBJECTS")
    add_library(gta2_port STATIC ${GTA2_PORT_SOURCES})
    set_target_properties(gta2_port PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(gta2_port PRIVATE winrecomp_rt)
    add_dependencies(gta2_port gta2_git_id)
    target_include_directories(gta2_port PRIVATE ${CMAKE_BINARY_DIR})
    target_compile_options(gta2_port PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    return()
endif()
if(ANDROID)
    # Android: SDL's Java side (SDLActivity) loads libmain.so and calls SDL_main (M8)
    add_library(gta2_recomp SHARED ${GTA2_PORT_SOURCES})
    set_target_properties(gta2_recomp PROPERTIES OUTPUT_NAME main)
    target_link_libraries(gta2_recomp PRIVATE android log)
else()
    add_executable(gta2_recomp ${GTA2_PORT_SOURCES})
endif()
target_link_libraries(gta2_recomp PRIVATE winrecomp_rt)
add_dependencies(gta2_recomp gta2_git_id)
target_include_directories(gta2_recomp PRIVATE ${CMAKE_BINARY_DIR})
if(NOT MSVC)
    target_compile_options(gta2_recomp PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()
# Crash reports name the generated functions: dladdr needs them in the
# dynamic symbol table. Only those - exporting everything (-rdynamic) lets a
# library bind to our symbols of the same name (the Vulkan driver called our
# vkEnumeratePhysicalDevices function-pointer variable).
if(NOT WIN32 AND NOT APPLE)
    target_link_options(gta2_recomp PRIVATE "-Wl,--dynamic-list=${CMAKE_CURRENT_SOURCE_DIR}/src/exports.dynlist")
endif()

# librashader (slang shaders): built from source by tools/deps_librashader.sh,
# loaded at run time from next to the exe (missing = shaders unavailable).
set(LIBRASHADER_SO "${CMAKE_CURRENT_SOURCE_DIR}/../../deps/linux/build/librashader/x86_64-unknown-linux-gnu/optimized/liblibrashader_capi.so" CACHE FILEPATH "librashader C API library")
if(EXISTS "${LIBRASHADER_SO}" AND NOT WIN32 AND NOT ANDROID)
    add_custom_command(TARGET gta2_recomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBRASHADER_SO}" "$<TARGET_FILE_DIR:gta2_recomp>/librashader.so")
endif()
