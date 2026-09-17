cmake_minimum_required(VERSION 3.20)
project(matching_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ME_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ME_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ME_BUILD_TESTS "Build unit tests" ON)
option(ME_BUILD_BENCHMARKS "Build benchmarks" ON)
option(ME_BUILD_APPS "Build apps" ON)

add_library(me_warnings INTERFACE)
target_compile_options(me_warnings INTERFACE -Wall -Wextra -Wpedantic)

add_library(me_sanitizers INTERFACE)
if(ME_ENABLE_ASAN)
  target_compile_options(me_sanitizers INTERFACE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(me_sanitizers INTERFACE -fsanitize=address)
endif()
if(ME_ENABLE_UBSAN)
  target_compile_options(me_sanitizers INTERFACE -fsanitize=undefined -fno-omit-frame-pointer)
  target_link_options(me_sanitizers INTERFACE -fsanitize=undefined)
endif()

find_package(Threads REQUIRED)

add_library(matching_engine_core INTERFACE)
target_include_directories(matching_engine_core INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(matching_engine_core INTERFACE Threads::Threads me_warnings me_sanitizers)

add_subdirectory(src)

if(ME_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  add_subdirectory(tests)
endif()

if(ME_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(ME_BUILD_APPS)
  add_subdirectory(apps)
endif()
