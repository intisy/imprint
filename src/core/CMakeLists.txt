add_library(imprint_core STATIC
    text.c
    error.c
    string_list.c
    document.c
    document_toml.c
    document_json.c
    document_file.c
    identifier.c
    platform_common.c
)
if(WIN32)
    target_sources(imprint_core PRIVATE platform_windows.c)
else()
    target_sources(imprint_core PRIVATE platform_posix.c)
endif()
target_include_directories(imprint_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imprint_core PRIVATE imprint_warnings imprint_tomlc17 imprint_cjson)
if(NOT WIN32)
    target_link_libraries(imprint_core PRIVATE m)
endif()
