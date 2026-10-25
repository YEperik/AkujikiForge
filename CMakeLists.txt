cmake_minimum_required(VERSION 3.16)
project(AkuijikiForge VERSION 1.0.0 LANGUAGES CXX)
# If your headers are inside an "include" folder at the root of the project
target_include_directories(akuijikiforge_core PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# OR: If your headers are directly inside "src/akuijikiforge"
target_include_directories(akuijikiforge_core PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
option(AKUIJIKIFORGE_BUILD_TESTS "Build unit tests" ON)

add_library(akuijikiforge_core src/probe.cpp)
target_include_directories(akuijikiforge_core PUBLIC include)
target_compile_features(akuijikiforge_core PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(akuijikiforge_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(akuijikiforge_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(akuijikiforge src/main.cpp)
target_link_libraries(akuijikiforge PRIVATE akuijikiforge_core)

if(AKUIJIKIFORGE_BUILD_TESTS)
    enable_testing()
    add_executable(akuijikiforge_tests tests/test_inspector.cpp)
    target_link_libraries(akuijikiforge_tests PRIVATE akuijikiforge_core)
    add_test(NAME inspector-unit-tests COMMAND akuijikiforge_tests)
endif()
