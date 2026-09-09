cmake_minimum_required(VERSION 3.22)
project(levi_item_physics LANGUAGES CXX)

if(NOT ANDROID)
  message(FATAL_ERROR "Levi Item Physics is Android-only. Use the Android NDK toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_ANDROID_STL_TYPE c++_shared)

include(FetchContent)
FetchContent_Declare(
  preloader_android
  GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
  GIT_TAG 26ff71e48483aeddf2b5657b994ef4ade63e5cc0
)
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 11.2.0
)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(fmt preloader_android)

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
  OUTPUT_NAME "levi_item_physics"
)

if(ANDROID_ABI)
  set_target_properties(
    levi_item_physics
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/${ANDROID_ABI}"
  )
endif()

target_include_directories(
  levi_item_physics
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# The visual core intentionally has no JSON/reflection dependency. fmt is the
# only support library retained because it is part of the preloader logger API.
# c++_shared stays external in LeviLaunchroid, keeping this .so small.
target_link_libraries(levi_item_physics PRIVATE preloader fmt::fmt)

find_library(LOG_LIB log)
find_library(DL_LIB dl)
if(LOG_LIB)
  target_link_libraries(levi_item_physics PRIVATE "${LOG_LIB}")
endif()
if(DL_LIB)
  target_link_libraries(levi_item_physics PRIVATE "${DL_LIB}")
endif()

target_compile_options(
  levi_item_physics
  PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  # This renderer runs on the frame thread. O2 keeps the small stripped binary
  # far below the package cap while avoiding Oz's speed-for-size trade-offs.
  -O2
  -ffunction-sections
  -fdata-sections
  -fvisibility=hidden
  -fvisibility-inlines-hidden
)

target_link_options(
  levi_item_physics
  PRIVATE
  -Wl,-z,max-page-size=16384
  -Wl,--gc-sections
  -Wl,--icf=safe
  -Wl,--as-needed
  -Wl,--exclude-libs,ALL
  -Wl,--strip-all
)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_OK OUTPUT IPO_ERROR)
if(IPO_OK)
  set_property(
    TARGET levi_item_physics
    PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
  )
else()
  message(WARNING "IPO/LTO unavailable: ${IPO_ERROR}")
endif()

message(STATUS "Levi Item Physics: ABI=${ANDROID_ABI}, platform=${ANDROID_PLATFORM}, strict 1.26.45.1 visual profile")
