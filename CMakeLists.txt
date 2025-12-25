cmake_minimum_required(VERSION 3.14)
project(Cycles VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Core Library
add_library(CyclesCore
    src/context.cpp
    src/graph_builder.cpp
    src/execution_graph.cpp
    # src/thread_pool.cpp # Milestone 2
    # src/loop.cpp       # Milestone 3
)
target_include_directories(CyclesCore PUBLIC include)

# Tests
enable_testing()

add_executable(CyclesTests
    tests/test_topology.cpp
    tests/test_cycle_detection.cpp
    # tests/test_concurrency.cpp # Milestone 2
)
target_link_libraries(CyclesTests PRIVATE CyclesCore gtest_main)

include(GoogleTest)
gtest_discover_tests(CyclesTests)
