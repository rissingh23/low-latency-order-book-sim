cmake_minimum_required(VERSION 3.20)

project(low_latency_order_book_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(
  lob_core
  src/engines/baseline_order_book.cpp
  src/engines/optimized_order_book.cpp
  src/workload.cpp
  src/dataset.cpp
  src/benchmark.cpp
  src/replay.cpp
)

target_include_directories(lob_core PUBLIC include)

target_compile_options(
  lob_core
  PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic>
)

add_executable(lob_simulator src/main.cpp)
target_link_libraries(lob_simulator PRIVATE lob_core)

enable_testing()

add_executable(lob_tests tests/test_order_books.cpp)
target_link_libraries(lob_tests PRIVATE lob_core)

add_test(NAME lob_tests COMMAND lob_tests)
