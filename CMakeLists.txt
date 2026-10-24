cmake_minimum_required(VERSION 3.16)
project(ps2_asset_inspector VERSION 0.6.0 LANGUAGES CXX)

option(PS2PROBE_BUILD_TESTS "Build the self-contained test executable" ON)

add_library(ps2probe_core
    src/probe.cpp
    src/deep_scan.cpp
    src/material_links.cpp
)
target_include_directories(ps2probe_core PUBLIC include)
target_compile_features(ps2probe_core PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(ps2probe_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(ps2probe_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(ps2probe src/main.cpp)
target_link_libraries(ps2probe PRIVATE ps2probe_core)

if(PS2PROBE_BUILD_TESTS)
    enable_testing()
    add_executable(ps2probe_tests tests/test_probe.cpp)
    target_link_libraries(ps2probe_tests PRIVATE ps2probe_core)
    add_test(NAME ps2probe_tests COMMAND ps2probe_tests)
endif()
