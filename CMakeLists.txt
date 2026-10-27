cmake_minimum_required(VERSION 3.26)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED ENV{CMAKE_BUILD_TYPE})
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type")
endif ()

project(howl_button VERSION 1.2.0 LANGUAGES CXX)

# The Dusklight version to build against: a release tag (e.g. "v2.0.0") or a commit SHA.
# Must match the Dusklight release you run (game ABI epoch).
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "v2.0.3")
endif ()
# Fetches the Dusklight sources into ./dusklight and provides DUSKLIGHT_DIR.
# Use -DDUSKLIGHT_DIR= to point at an existing Dusklight checkout.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

configure_file(mod.json.in "${CMAKE_CURRENT_BINARY_DIR}/mod.json" @ONLY NEWLINE_STYLE LF)

add_mod(howl_button
        FEATURES game fmt
        SOURCES src/mod.cpp
        MOD_JSON "${CMAKE_CURRENT_BINARY_DIR}/mod.json"
        RES_DIR res            # mod resources, including icon.png and banner.png
)

# The game is built with /Zc:__cplusplus but the mod SDK doesn't add it. Without it MSVC reports
# __cplusplus as 199711L, and dolphin/types.h then does `#define nullptr NULL`, breaking fmt.
if (MSVC)
    target_compile_options(howl_button PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>")
endif ()
