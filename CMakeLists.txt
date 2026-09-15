cmake_minimum_required(VERSION 3.20)

project(ForgeDB
    VERSION 0.1.0
    DESCRIPTION "A from-scratch relational database management system in C++17"
    LANGUAGES CXX
)

# Enforce C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set common binary output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Compiler warning flags
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(MINGW)
        add_link_options(-static -static-libgcc -static-libstdc++)
    endif()
endif()

# Enable testing subsystem
enable_testing()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/include)

# Subdirectories
add_subdirectory(src)
add_subdirectory(tests)
