add_library(imprint_core STATIC
    text.c
    error.c
    string_list.c
)
target_include_directories(imprint_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imprint_core PRIVATE imprint_warnings)
