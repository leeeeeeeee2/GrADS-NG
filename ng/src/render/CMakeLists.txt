find_package(PkgConfig QUIET)

set(RENDER_SOURCES canvas.c)

# Off-screen Cairo backend (preferred headless renderer)
if(ENABLE_CAIRO AND PkgConfig_FOUND)
    pkg_check_modules(CAIRO cairo)
    if(CAIRO_FOUND)
        list(APPEND RENDER_SOURCES backend_cairo.c)
        add_definitions(-DRENDER_CAIRO=1)
    endif()
endif()

add_library(ng_render STATIC ${RENDER_SOURCES})
target_include_directories(ng_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_render PUBLIC ng_platform)

if(CAIRO_FOUND)
    target_include_directories(ng_render PRIVATE ${CAIRO_INCLUDE_DIRS})
    target_link_libraries(ng_render PUBLIC ${CAIRO_LIBRARIES})
    target_link_directories(ng_render PUBLIC ${CAIRO_LIBRARY_DIRS})
endif()
