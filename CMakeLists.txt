cmake_minimum_required(VERSION 3.28)
project(ugh LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(UGH_SANITIZE "Build Debug with AddressSanitizer + UndefinedBehaviorSanitizer" ON)
option(UGH_CLANG_TIDY "Run clang-tidy while compiling" OFF)

find_package(SDL3 REQUIRED CONFIG)

# Nuked-OPL3 (LGPL-2.1): cycle-accurate emulation of the AdLib's Yamaha OPL chip. Pinned commit, not vendored.
include(FetchContent)
FetchContent_Declare(nuked_opl3
    GIT_REPOSITORY https://github.com/nukeykt/Nuked-OPL3.git
    GIT_TAG 765ec962e473aeb767e4cba74ffdc8f588ffbfe8)
FetchContent_MakeAvailable(nuked_opl3)
enable_language(C)
add_library(nuked_opl3 STATIC ${nuked_opl3_SOURCE_DIR}/opl3.c)
target_include_directories(nuked_opl3 SYSTEM PUBLIC ${nuked_opl3_SOURCE_DIR})
set_target_properties(nuked_opl3 PROPERTIES C_STANDARD 99 C_CLANG_TIDY "")

if(UGH_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy HINTS /opt/homebrew/opt/llvm/bin REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
endif()

# Memory-safety settings shared by every target (see CLAUDE.md).
add_library(ugh_options INTERFACE)
target_compile_options(ugh_options INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wold-style-cast -Wnull-dereference -Werror)
target_compile_definitions(ugh_options INTERFACE
    $<$<CONFIG:Debug>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST>)
if(UGH_SANITIZE)
    target_compile_options(ugh_options INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer>)
    target_link_options(ugh_options INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

# Platform-independent game data and logic.
add_library(ugh_core STATIC
    src/core/sha256.cpp
    src/data/data_segment.cpp
    src/data/exe_image.cpp
    src/data/ice.cpp
    src/data/graphics.cpp
    src/vga/vga.cpp
    src/game/video.cpp
    src/game/text.cpp
    src/game/assets.cpp
    src/game/game.cpp
    src/game/play.cpp
    src/game/physics.cpp
    src/game/passengers.cpp
    src/game/enemies.cpp
    src/game/water.cpp
    src/game/sequence.cpp
    src/sound/opl_chip.cpp
    src/sound/pit_timers.cpp
    src/sound/adlib_driver.cpp
    src/game/status.cpp)
target_include_directories(ugh_core PUBLIC src)
target_link_libraries(ugh_core PUBLIC ugh_options PRIVATE nuked_opl3)

# SDL front end.
add_library(ugh_platform STATIC
    src/platform/screen.cpp
    src/platform/keyboard.cpp
    src/platform/game_exe.cpp)
target_link_libraries(ugh_platform PUBLIC ugh_core SDL3::SDL3)

add_executable(ugh src/main.cpp)
target_link_libraries(ugh PRIVATE ugh_platform)

add_executable(ugh-extract tools/extract/main.cpp)
target_link_libraries(ugh-extract PRIVATE ugh_platform)

enable_testing()
add_executable(ugh-tests tests/data_tests.cpp)
target_link_libraries(ugh-tests PRIVATE ugh_core)
add_test(NAME data COMMAND ugh-tests ${CMAKE_SOURCE_DIR}/ugh/UGH.EXE)

add_executable(ugh-runtime-tests tests/runtime_tests.cpp)
target_link_libraries(ugh-runtime-tests PRIVATE ugh_core)
add_test(NAME runtime COMMAND ugh-runtime-tests)

add_executable(ugh-snap tools/snap/main.cpp)
target_link_libraries(ugh-snap PRIVATE ugh_platform)

add_executable(ugh-play-tests tests/play_tests.cpp)
target_link_libraries(ugh-play-tests PRIVATE ugh_core)
add_test(NAME play COMMAND ugh-play-tests ${CMAKE_SOURCE_DIR}/ugh/UGH.EXE)
