add_library(imprint_manager STATIC
    cli.c
    workspace.c
    commands_read.c
    commands_interactive.c
    commands_git.c
    commands_hooks.c
)
target_include_directories(imprint_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imprint_manager PUBLIC imprint_core imprint_ui PRIVATE imprint_warnings)

add_executable(imprint main.c)
target_link_libraries(imprint PRIVATE imprint_manager imprint_warnings)
