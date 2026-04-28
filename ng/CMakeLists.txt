cmake_minimum_required(VERSION 3.16)
project(GrADS-NG VERSION 2.2.1 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(GRADS_NG_SOURCES
    src/grads_ng.c
    src/gs_parser.c
    src/gx_virtual.c
)

# Executable
add_executable(grads-ng ${GRADS_NG_SOURCES})

# Libraries
target_link_libraries(grads-ng PRIVATE m)

# Install
install(TARGETS grads-ng DESTINATION bin)

# Print configuration
message(STATUS "GrADS-NG v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")