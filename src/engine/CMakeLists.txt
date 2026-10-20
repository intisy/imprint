add_library(imprint_engine STATIC
    engine.c
    engine_hooks.c
    engine_install.c
    plan.c
    install/command_runner.c
    install/disks.c
    install/install_options.c
    install/storage_choice.c
    install/layout.c
    install/offline.c
    install/base_system.c
    install/software.c
    install/bootloader.c
    install/install_review.c
    install/install.c
)
target_include_directories(imprint_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/install)
target_link_libraries(imprint_engine PUBLIC imprint_core imprint_ui PRIVATE imprint_cjson imprint_warnings)

add_executable(imprint-engine main.c)
target_link_libraries(imprint-engine PRIVATE imprint_engine imprint_warnings)
