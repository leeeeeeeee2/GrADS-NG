add_library(ng_interp STATIC
    lexer.c
    parser.c
    eval.c
)
target_include_directories(ng_interp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_interp PUBLIC ng_platform)
