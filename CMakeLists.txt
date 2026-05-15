cmake_minimum_required(VERSION 3.16)
project(lockstep CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

add_library(lockstep STATIC
  src/hart.cpp
  src/lane_switch.cpp
  src/lane_uop.cpp
  src/harness.cpp
  src/remote.cpp
)
target_include_directories(lockstep PUBLIC include)
target_link_libraries(lockstep PUBLIC Threads::Threads)

add_executable(lockstep_tests tests/test_main.cpp)
target_link_libraries(lockstep_tests PRIVATE lockstep)

add_executable(lane_worker tools/lane_worker.cpp)
target_link_libraries(lane_worker PRIVATE lockstep)

add_executable(redundant_demo demo/redundant_demo.cpp)
target_link_libraries(redundant_demo PRIVATE lockstep)

enable_testing()
add_test(NAME lockstep_tests COMMAND lockstep_tests)
