cmake_minimum_required(VERSION 3.22)
project(levi_item_physics LANGUAGES CXX)

if(NOT ANDROID)
  message(FATAL_ERROR "Levi Item Physics is an Android native mod. Use the Android NDK toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Pin the public LeviLauncher native-mod SDK for reproducible ABI/layout.
FetchContent_Declare(
  preloader_android
  GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
  GIT_TAG 0.2.2)

# Config.hpp exposes these types in public headers, so the mod links their
# public targets as the official full-cpp-mod example does.
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3)
FetchContent_Declare(boost_pfr
  GIT_REPOSITORY https://github.com/boostorg/pfr.git
  GIT_TAG boost-1.88.0)
FetchContent_Declare(magic_enum
  GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
  GIT_TAG v0.9.7)
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 11.2.0)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(
  nlohmann_json
  boost_pfr
  magic_enum
  fmt
  preloader_android)

add_library(levi_item_physics SHARED
  src/LeviItemPhysics.cpp
  src/ItemPhysicsRuntime.cpp
  src/RttiResolver.cpp)
set_target_properties(levi_item_physics PROPERTIES
  OUTPUT_NAME "levi_item_physics")
if(ANDROID_ABI)
  set_target_properties(levi_item_physics PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/${ANDROID_ABI}")
endif()

target_include_directories(levi_item_physics PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(levi_item_physics PRIVATE
  preloader
  nlohmann_json::nlohmann_json
  Boost::pfr
  magic_enum::magic_enum
  fmt::fmt)

find_library(LOG_LIB log)
find_library(DL_LIB dl)
if(LOG_LIB)
  target_link_libraries(levi_item_physics PRIVATE "${LOG_LIB}")
endif()
if(DL_LIB)
  target_link_libraries(levi_item_physics PRIVATE "${DL_LIB}")
endif()

target_link_options(levi_item_physics PRIVATE
  -Wl,-z,max-page-size=16384)

target_compile_options(levi_item_physics PRIVATE
  -Wall -Wextra -Wpedantic
  -fvisibility=hidden -fvisibility-inlines-hidden)
