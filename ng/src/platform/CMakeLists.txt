add_library(ng_platform STATIC
    platform.c
)
target_include_directories(ng_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
