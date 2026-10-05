cmake_minimum_required(VERSION 3.16)

# Use the RISC-V cross-compiler toolchain
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain-app.cmake")
endif()

project(app C)

# App source files — add your .c files here
set(APP_SOURCES
    main/main.c
    main/crt0.c
    main/chamber.c
    main/demo.c
    main/draft.c
    main/editor.c
    main/game.c
    main/input.c
    main/leds.c
    main/level.c
    main/menu.c
    main/physics.c
    main/player.c
    main/portal.c
    main/render.c
    main/settings.c
    main/sound.c
)

# Automated device tests (main/testkit/, README "Device tests"): perf and
# shots of the scripted demos in main/demo.c, driven over the debug console.
list(APPEND APP_SOURCES
    main/testkit/debugcon.c
    main/testkit/devtest.c
    main/testkit/profile.c
    main/testkit/report.c
    main/testkit/screenshot.c
    main/testkit/showtime.c
)
add_compile_definitions(SCREENSHOT_DIR="/sd/portals/test")

# Build identity for the debug console's READY records: the git commit
# (marked -dirty when there are uncommitted changes) and the build time.
# `make build` re-runs cmake, so it is fresh on every build. Without it the
# app reports "unknown" and tools/testrun.py refuses to believe a result.
execute_process(
    COMMAND git describe --always --dirty --abbrev=10
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE APP_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT APP_GIT_HASH)
    set(APP_GIT_HASH "unknown")
endif()
string(TIMESTAMP APP_BUILD_TIME "%Y-%m-%dT%H:%M:%SZ" UTC)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/metadata/metadata.json APP_METADATA_JSON)
string(JSON APP_VERSION ERROR_VARIABLE APP_VERSION_ERR GET "${APP_METADATA_JSON}" version)
if(APP_VERSION_ERR OR NOT APP_VERSION)
    set(APP_VERSION "")
endif()
configure_file(main/app_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/app_version.h @ONLY)

# Include directories — add paths to your headers here
set(APP_INCLUDES
    main
    include
    include/esp_additions/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# The built-in chambers: chambers/*.txt turned into C (tools/embed_chambers.py).
file(GLOB CHAMBER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/chambers/*.txt)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/chambers_builtin.c
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_chambers.py ${CMAKE_CURRENT_SOURCE_DIR}/chambers
            ${CMAKE_CURRENT_BINARY_DIR}/generated/chambers_builtin.c
    DEPENDS ${CHAMBER_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_chambers.py
    COMMENT "Embedding chambers/*.txt"
)
list(APPEND APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/generated/chambers_builtin.c)

# Compile app sources as a relocatable object library
add_library(app_obj OBJECT ${APP_SOURCES})
target_include_directories(app_obj PRIVATE ${APP_INCLUDES})
target_compile_options(app_obj PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -Os -fPIC
    -ffunction-sections -fdata-sections -fno-common
    -D__thread=
    -DESP_PLATFORM
)

# ESP-IDF normally force-includes FreeRTOS headers for all components
target_compile_options(app_obj PRIVATE
    "SHELL:-include freertos/FreeRTOS.h"
    "SHELL:-include freertos/semphr.h"
    "SHELL:-include freertos/task.h"
    "SHELL:-include freertos/queue.h"
    "SHELL:-include freertos/event_groups.h"
)

# SynthEngine3D, the 3D engine (https://github.com/nullislandspace/synthengine3D),
# if this app has it. It is not part of the template: an app that wants 3D adds
# it as a git submodule at synthengine3D/ (`make engine`, see README), and this
# block then builds it. Without it every variable below stays empty and the
# link line is exactly the one a plain app has always had, so apps that do not
# use the engine are unaffected by merges from the template.
#
# In this plain-CMake build the engine is a relocatable OBJECT library, so
# add_subdirectory MUST come before the link script's file(GENERATE) for
# $<TARGET_OBJECTS:synthengine3d> to resolve. Linking it to app_obj propagates
# the engine's PUBLIC include directory (synthengine3D/include), which is what
# lets app sources #include "synthengine3d.h"; its object files are folded into
# app.so by the link script below, since the custom command, not CMake, links.
set(ENGINE_OBJS "")
set(ENGINE_DEPS "")
# Engine settings. Compile definitions that must reach the synthengine3d
# target too, so they come before add_subdirectory().
#
#   RENDER_NEAR_CLIP_Z  down from 0.5: the eye is 0.3 from a wall it
#       touches, and passes within a hair of a portal's plane on the
#       way through. The far limit drops to 64000 * 0.05 = 3200 m.
#   RENDER_HORIZON_Y    the middle of the screen, for a first-person view.
#   SE_SCENE_LINE_CAP   the game draws no lines, but the engine's splash
#       writes its title in line segments: at 64 it was cut off mid-word.
#   SE_SCENE_TEXTURED_TRI_CAP  one pass holds the whole chamber: up to
#       LV_MAX_QUADS (1024) rectangles, two triangles each, plus cubes,
#       portal frames and what clipping adds. 4096 is 272 KB of PSRAM.
#   SE_SCENE_DEPTH16_INTERNAL  the quarter-resolution depth plane in
#       internal SRAM: every covered pixel reads it.
add_compile_definitions(RENDER_NEAR_CLIP_Z=0.05f)
add_compile_definitions(RENDER_HORIZON_Y=240.0f)
add_compile_definitions(SE_SCENE_LINE_CAP=1024)
add_compile_definitions(SE_SCENE_TEXTURED_TRI_CAP=4096)
add_compile_definitions(SE_SCENE_DEPTH16_INTERNAL=1)
# Menu row text, down from 28 px so nine rows fit (main/menu.c).
add_compile_definitions(SE_UI_ROW_TEXT_H=20.0f)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/synthengine3D/CMakeLists.txt)
    message(STATUS "SynthEngine3D: building synthengine3D/")
    add_subdirectory(synthengine3D)
    target_link_libraries(app_obj PRIVATE synthengine3d)
    set(ENGINE_OBJS "$<JOIN:$<TARGET_OBJECTS:synthengine3d>, >")
    list(APPEND ENGINE_DEPS synthengine3d $<TARGET_OBJECTS:synthengine3d>)
endif()

# Collect object files into a list file, then link via a response file
# This avoids issues with generator expressions in custom commands
set(LINK_SCRIPT ${CMAKE_BINARY_DIR}/link_app.sh)
file(GENERATE OUTPUT ${LINK_SCRIPT} CONTENT
"#!/bin/sh
${CMAKE_C_COMPILER} \
    -march=rv32imafc_zicsr_zifencei_zaamo_zalrsc_xesploop_xespv2p1 -mabi=ilp32f \
    -shared -nostdlib \
    -Wl,--gc-sections \
    -Wl,-Bsymbolic \
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/app_version.script \
    -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/app.ld \
    -Wl,-Map=${CMAKE_BINARY_DIR}/app.map \
    -L${CMAKE_CURRENT_SOURCE_DIR}/fakelib \
    $<JOIN:$<TARGET_OBJECTS:app_obj>, > ${ENGINE_OBJS} \
    -lall \
    -o ${CMAKE_BINARY_DIR}/app.so
")

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/app.so
    COMMAND sh ${LINK_SCRIPT}
    DEPENDS app_obj
        $<TARGET_OBJECTS:app_obj>
        ${ENGINE_DEPS}
        ${CMAKE_CURRENT_SOURCE_DIR}/app.ld
        ${CMAKE_CURRENT_SOURCE_DIR}/app_version.script
        ${CMAKE_CURRENT_SOURCE_DIR}/fakelib/liball.so
    COMMENT "Linking app.so"
)

add_custom_target(app ALL DEPENDS ${CMAKE_BINARY_DIR}/app.so)

# Print size after build
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_BINARY_DIR}/app.so
    COMMENT "app.so size:"
)
