cmake_minimum_required(VERSION 3.22)

project(
    levi_item_physics
    LANGUAGES CXX
)

# ============================================================
# Platform guard
# ============================================================

if(NOT ANDROID)
    message(
        FATAL_ERROR
        "Levi Item Physics is an Android native mod. "
        "Use the Android NDK toolchain."
    )
endif()

# ============================================================
# C++ configuration
# ============================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ============================================================
# LeviLauncher / Preloader SDK
#
# IMPORTANT:
#
# DO NOT use preloader-android 0.2.2 here.
#
# LeviLaunchroid v1.5.16 uses:
#
# 26ff71e48483aeddf2b5657b994ef4ade63e5cc0
#
# ModuleInfo in this version contains:
#
# - onToggle
# - onConfigChanged
# - onKeybind
#
# Building against the old 0.2.2 ModuleInfo causes an ABI
# mismatch and SIGSEGV inside pl::modmenu::registerModule().
#
# ============================================================

FetchContent_Declare(
    preloader_android

    GIT_REPOSITORY
        https://github.com/LiteLDev/preloader-android.git

    GIT_TAG
        26ff71e48483aeddf2b5657b994ef4ade63e5cc0
)

# ============================================================
# Dependencies required by Config.hpp / public SDK
# ============================================================

FetchContent_Declare(
    nlohmann_json

    GIT_REPOSITORY
        https://github.com/nlohmann/json.git

    GIT_TAG
        v3.11.3
)

FetchContent_Declare(
    boost_pfr

    GIT_REPOSITORY
        https://github.com/boostorg/pfr.git

    GIT_TAG
        boost-1.88.0
)

FetchContent_Declare(
    magic_enum

    GIT_REPOSITORY
        https://github.com/Neargye/magic_enum.git

    GIT_TAG
        v0.9.7
)

FetchContent_Declare(
    fmt

    GIT_REPOSITORY
        https://github.com/fmtlib/fmt.git

    GIT_TAG
        11.2.0
)

# Disable unnecessary fmt targets.
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)

# ============================================================
# Download / configure dependencies
# ============================================================

FetchContent_MakeAvailable(
    nlohmann_json
    boost_pfr
    magic_enum
    fmt
    preloader_android
)

# ============================================================
# Levi Item Physics
# ============================================================

add_library(
    levi_item_physics
    SHARED

    src/LeviItemPhysics.cpp
    src/ItemPhysicsRuntime.cpp
    src/RttiResolver.cpp
)

set_target_properties(
    levi_item_physics

    PROPERTIES

    OUTPUT_NAME
        "levi_item_physics"
)

# ============================================================
# Android output path
#
# Result:
#
# build/out/arm64-v8a/liblevi_item_physics.so
#
# ============================================================

if(ANDROID_ABI)

    set_target_properties(
        levi_item_physics

        PROPERTIES

        LIBRARY_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/out/${ANDROID_ABI}"
    )

endif()

# ============================================================
# Include directories
# ============================================================

target_include_directories(
    levi_item_physics

    PRIVATE

    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# ============================================================
# Link libraries
# ============================================================

target_link_libraries(
    levi_item_physics

    PRIVATE

    preloader

    nlohmann_json::nlohmann_json
    Boost::pfr
    magic_enum::magic_enum
    fmt::fmt
)

# ============================================================
# Android system libraries
# ============================================================

find_library(
    LOG_LIB
    log
)

find_library(
    DL_LIB
    dl
)

if(LOG_LIB)

    target_link_libraries(
        levi_item_physics
        PRIVATE
        "${LOG_LIB}"
    )

endif()

if(DL_LIB)

    target_link_libraries(
        levi_item_physics
        PRIVATE
        "${DL_LIB}"
    )

endif()

# ============================================================
# Android 16 / 16 KB page compatibility
# ============================================================

target_link_options(
    levi_item_physics

    PRIVATE

    -Wl,-z,max-page-size=16384
)

# ============================================================
# Compiler options
# ============================================================

target_compile_options(
    levi_item_physics

    PRIVATE

    -Wall
    -Wextra
    -Wpedantic

    -fvisibility=hidden
    -fvisibility-inlines-hidden
)

# ============================================================
# Build information
# ============================================================

message(
    STATUS
    "Levi Item Physics configuration:"
)

message(
    STATUS
    "  Android ABI      = ${ANDROID_ABI}"
)

message(
    STATUS
    "  Android platform = ${ANDROID_PLATFORM}"
)

message(
    STATUS
    "  Preloader commit = 26ff71e48483aeddf2b5657b994ef4ade63e5cc0"
)
