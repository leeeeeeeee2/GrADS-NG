cmake_minimum_required(VERSION 3.16)
project(GrADS-NG VERSION 0.1.0 LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Warnings (errors stay off so MSVC/GCC/Clang all pass by default)
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Canonical sources ------------------------------------------------------
# Session + command runtime + array evaluator live in ngcore; the CLI is a
# thin main. Legacy files (src/grads_ng.c, src/gs_parser.c, src/gx_virtual.c)
# are excluded from the default build; see GRADS_NG_LEGACY below.

# Parser as a reusable static library (unit tests link it directly).
add_library(ngparser STATIC src/parser/grads_ng_parser.c)
target_include_directories(ngparser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ngparser PUBLIC m)

# Portable OS layer (mmap, paths, dynamic loading) as a reusable library.
add_library(ngplatform STATIC src/platform/platform.c)
target_include_directories(ngplatform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/platform)

# Descriptor (.ctl) parser + binary grid reader as a reusable library.
add_library(ngio STATIC src/io/ctl.c src/io/grid.c src/time/ng_time.c)
target_link_libraries(ngio PUBLIC ngplatform)
target_include_directories(ngio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time)

# Core runtime library: session, variables, array evaluator, OS compat.
add_library(ngcore STATIC
    src/core/grads_ng.c
    src/backend/platform.c
    src/eval/array.c
    src/render/shade.c
    src/render/contour.c
    src/render/png.c
)
target_include_directories(ngcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eval
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
)
target_link_libraries(ngcore PUBLIC ngio ngparser ngplatform m)

# Executable (thin CLI over ngcore)
add_executable(grads-ng src/main.c)
target_link_libraries(grads-ng PRIVATE ngcore ${CMAKE_DL_LIBS})

# Install
install(TARGETS grads-ng DESTINATION bin)

# --- Legacy CLI (off by default; needs Cairo + libpng) --------------------
option(GRADS_NG_LEGACY "Build legacy gx_virtual/gs_parser CLI (requires Cairo, libpng)" OFF)
if(GRADS_NG_LEGACY)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CAIRO REQUIRED cairo)
    find_package(PNG REQUIRED)
    add_executable(grads-ng-legacy
        src/grads_ng.c
        src/gs_parser.c
        src/gx_virtual.c
    )
    target_include_directories(grads-ng-legacy PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${CAIRO_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
    target_link_libraries(grads-ng-legacy PRIVATE ${CAIRO_LIBRARIES} ${PNG_LIBRARIES})
endif()

# --- Tests -----------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# Print configuration
message(STATUS "GrADS-NG v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Legacy CLI: ${GRADS_NG_LEGACY}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
