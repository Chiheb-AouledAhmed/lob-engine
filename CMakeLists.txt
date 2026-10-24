cmake_minimum_required(VERSION 3.20)
project(lob_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(LOB_BUILD_TESTS "Build unit tests" ON)
option(LOB_BUILD_BENCH "Build benchmark" ON)
option(LOB_SANITIZE "Build with address/undefined sanitizers" OFF)

add_library(lob src/order_book.cpp)
target_include_directories(lob PUBLIC include)
target_compile_options(lob PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(LOB_SANITIZE)
  target_compile_options(lob PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(lob PUBLIC -fsanitize=address,undefined)
endif()

if(LOB_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  add_executable(lob_tests tests/order_book_test.cpp)
  target_link_libraries(lob_tests PRIVATE lob GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(lob_tests)
endif()

if(LOB_BUILD_BENCH)
  add_executable(lob_bench bench/bench.cpp)
  target_link_libraries(lob_bench PRIVATE lob)
  target_compile_options(lob_bench PRIVATE -O3)
endif()
