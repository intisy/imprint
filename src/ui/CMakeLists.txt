add_library(imprint_ui STATIC ui_ansi.c ui_scripted.c)
target_include_directories(imprint_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imprint_ui PUBLIC imprint_core PRIVATE imprint_warnings)
