cmake_minimum_required(VERSION 3.10)
project(ff1-x86 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# SDL3 local paths
set(SDL3_ROOT "${CMAKE_SOURCE_DIR}/src/SDL3")
set(SDL3_INCLUDE_DIR "${SDL3_ROOT}/include")
set(SDL3_LIB_DIR "${SDL3_ROOT}/lib")
set(SDL3_DLL "${SDL3_ROOT}/bin/SDL3.dll")

# The game's data, generated from the ROM: python tools/build_rom.py, then
# python tools/extract_data.py (data/generated/data/, never committed).
file(GLOB ROM_DATA_SOURCES ${CMAKE_SOURCE_DIR}/data/generated/data/*.c)
if(NOT ROM_DATA_SOURCES)
    message(FATAL_ERROR "No game data: run python tools/build_rom.py, then python tools/extract_data.py")
endif()

# Source files
set(SOURCES
    ${ROM_DATA_SOURCES}
    src/main.c
    src/audio.c
    src/ppu.c
    src/overworld_map.c
    src/render_overworld.c
    src/input.c
    src/game.c
    src/party.c
    src/text.c
    src/ui.c
    src/menu.c
    src/shop.c
    src/title.c
    src/ptygen.c
    src/intro.c
    src/lineup.c
    src/story.c
    src/minimap.c
    src/minigame.c
    src/apu.c
    src/music.c
    src/sfx.c
    src/save.c
    src/rom.c
    src/battle.c
    src/battle_draw.c
    src/rng.c
    src/screen_fx.c
    src/mapobj.c
    src/btl_core.c
    src/talk.c
    src/sm_map.c
    src/render_sm.c
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/data/generated
    ${SDL3_INCLUDE_DIR}
)

# Link SDL3
link_directories(${SDL3_LIB_DIR})

# Executable
add_executable(ff1-x86 ${SOURCES})

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ff1-x86 PRIVATE -Wall -Wextra)
endif()

target_link_libraries(ff1-x86
    libSDL3.dll.a
    -static-libgcc
)

# The game as a library for other front ends (love/): everything but the
# SDL files, plus ff1core.c.
set(CORE_SOURCES ${SOURCES})
list(REMOVE_ITEM CORE_SOURCES src/main.c src/audio.c src/input.c)
add_library(ff1core SHARED ${CORE_SOURCES} src/ff1core.c)
set_target_properties(ff1core PROPERTIES PREFIX "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ff1core PRIVATE -Wall -Wextra)
    target_link_options(ff1core PRIVATE -static-libgcc)
endif()

# Copy SDL3.dll to output directory
add_custom_command(TARGET ff1-x86 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SDL3_DLL}"
    "$<TARGET_FILE_DIR:ff1-x86>"
    COMMENT "Copying SDL3.dll..."
)

# ---- Tests: SDL-free game logic (run with `ctest --test-dir build`) ----
enable_testing()
add_executable(ff1-tests
    ${ROM_DATA_SOURCES}
    tests/test_main.c
    src/rom.c
    src/apu.c
    src/music.c
    src/sfx.c
    src/text.c
    src/save.c
    src/rng.c
    src/party.c
    src/screen_fx.c
    src/ppu.c
    src/btl_core.c
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ff1-tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME ff1-tests COMMAND ff1-tests)

# Headless screenshots of a few scenes (PPM), for checking the renderer:
#   build/ff1-screenshots <output-dir>
add_executable(ff1-screenshots
    ${ROM_DATA_SOURCES}
    tests/screenshots.c
    src/game.c
    src/party.c
    src/text.c
    src/ui.c
    src/rng.c
    src/screen_fx.c
    src/mapobj.c
    src/btl_core.c
    src/talk.c
    src/menu.c
    src/shop.c
    src/title.c
    src/ptygen.c
    src/intro.c
    src/lineup.c
    src/story.c
    src/minimap.c
    src/minigame.c
    src/apu.c
    src/music.c
    src/sfx.c
    src/save.c
    src/rom.c
    src/battle.c
    src/battle_draw.c
    src/sm_map.c
    src/render_sm.c
    src/overworld_map.c
    src/render_overworld.c
    src/ppu.c
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ff1-screenshots PRIVATE -Wall -Wextra)
endif()

# Set the working directory for the executable to the project root
set_property(TARGET ff1-x86 PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")