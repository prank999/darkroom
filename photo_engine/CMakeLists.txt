cmake_minimum_required(VERSION 3.16)
project(PhotoEngine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags for optimization and warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -march=native)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# Source files
set(CORE_SOURCES
    src/core/image.cpp
)

set(COLOR_SOURCES
    src/color/color_space.cpp
)

set(ADJUSTMENTS_SOURCES
    src/adjustments/tone_curve.cpp
)

set(RENDER_SOURCES
    src/render/renderer.cpp
)

# Main library
add_library(photo_engine STATIC
    ${CORE_SOURCES}
    ${COLOR_SOURCES}
    ${ADJUSTMENTS_SOURCES}
    ${RENDER_SOURCES}
)

target_include_directories(photo_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

# Test executable
add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine photo_engine)

# Benchmark executable
add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark photo_engine)

# Example/main executable (for manual testing)
add_executable(photo_cli src/main.cpp)
target_link_libraries(photo_cli photo_engine)

# Installation
install(TARGETS photo_engine ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
