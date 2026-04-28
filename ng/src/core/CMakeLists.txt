add_library(ng_core STATIC
    core.c
    commands.c
    expr.c
)
target_include_directories(ng_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_core PUBLIC ng_platform ng_interp ng_render ng_io)
