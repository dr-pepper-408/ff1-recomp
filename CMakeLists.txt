cmake_minimum_required(VERSION 3.10)
project(ff1-recomp C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# SDL3 local paths
set(SDL3_ROOT "${CMAKE_SOURCE_DIR}/src/SDL3")
set(SDL3_INCLUDE_DIR "${SDL3_ROOT}/include")
set(SDL3_LIB_DIR "${SDL3_ROOT}/lib")
set(SDL3_DLL "${SDL3_ROOT}/bin/SDL3.dll")

# The game's data is read from the player's ROM at start-up (src/rom.c,
# src/romdata.c): nothing from the game is built in.  The tables' storage:
set(ROMDATA_SOURCES src/romdata_storage.c src/romdata.c)
# (tools/extract_data.py's generated data, if present, only for the
# ff1-dump-ref check below)
file(GLOB ROM_DATA_SOURCES ${CMAKE_SOURCE_DIR}/data/generated/data/*.c)

# Source files
set(SOURCES
    ${ROMDATA_SOURCES}
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
add_executable(ff1-recomp ${SOURCES})

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ff1-recomp PRIVATE -Wall -Wextra)
endif()

target_link_libraries(ff1-recomp
    libSDL3.dll.a
    -static-libgcc
)

# The game as a library for other front ends (love/): everything but the
# SDL files, plus ff1core.c.
set(CORE_SOURCES ${SOURCES})
list(REMOVE_ITEM CORE_SOURCES src/main.c src/audio.c src/input.c)
add_library(ff1core SHARED ${CORE_SOURCES} src/ff1core.c src/view3d_world.c src/view3d_battle.c)
set_target_properties(ff1core PROPERTIES PREFIX "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ff1core PRIVATE -Wall -Wextra)
    target_link_options(ff1core PRIVATE -static-libgcc)
endif()

# Copy SDL3.dll to output directory
add_custom_command(TARGET ff1-recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SDL3_DLL}"
    "$<TARGET_FILE_DIR:ff1-recomp>"
    COMMENT "Copying SDL3.dll..."
)

# ---- Tests: SDL-free game logic (run with `ctest --test-dir build`) ----
enable_testing()
add_executable(ff1-tests
    ${ROMDATA_SOURCES}
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
    ${ROMDATA_SOURCES}
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
set_property(TARGET ff1-recomp PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
# The game's tables as text, to check src/romdata.c against the extractor:
#   ff1-dump-ref <out>  /  ff1-dump-rom <rom> <out>  -- the files must match.
if(ROM_DATA_SOURCES)
    add_executable(ff1-dump-ref ${ROM_DATA_SOURCES} tests/dump_tables.c)
endif()
add_executable(ff1-dump-rom src/romdata_storage.c src/romdata.c src/rom.c tests/dump_tables.c)
target_compile_definitions(ff1-dump-rom PRIVATE DUMP_FROM_ROM)
