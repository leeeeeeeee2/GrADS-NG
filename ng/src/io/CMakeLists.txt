set(IO_SOURCES ctl.c)

add_library(ng_io STATIC ${IO_SOURCES})
target_include_directories(ng_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_io PUBLIC ng_platform)
