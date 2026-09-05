cmake_minimum_required(VERSION 3.16)
project(PhotoEngine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -march=native)
elseif(MSVC)
    add_compile_options(/O2 /arch:AVX2)
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

# Source files
set(SOURCES
    src/document/ImageSource.cpp
    src/document/Document.cpp
    src/adjustments/ToneCurve.cpp
    src/rendering/Renderer.cpp
    src/rendering/RenderedImage.cpp
    src/color/ColorSpace.cpp
    src/cache/RenderCache.cpp
)

# Main library
add_library(photo_engine STATIC ${SOURCES})
target_include_directories(photo_engine PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

# Link pthread for threading
find_package(Threads REQUIRED)
target_link_libraries(photo_engine PUBLIC Threads::Threads)

# Test executable
add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine photo_engine)

# Benchmark executable
add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark photo_engine)

# Install targets
install(TARGETS photo_engine LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# Print configuration summary
message(STATUS "")
message(STATUS "Photo Engine Configuration:")
message(STATUS "  C++ Standard: C++20")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
