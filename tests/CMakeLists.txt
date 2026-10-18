set(IMPRINT_TEST_SOURCES
    main.c
    test_text.c
    test_error.c
    test_string_list.c
    test_document.c
    test_document_toml.c
    test_document_json.c
    test_identifier.c
    test_platform.c
    test_catalog.c
    test_setup_merge.c
    test_setup.c
    test_resolve.c
    test_engine.c
    test_ui.c
    cli_harness.c
    test_cli.c
    test_cli_git.c
)

add_executable(imprint-tests ${IMPRINT_TEST_SOURCES})
target_link_libraries(imprint-tests PRIVATE imprint_engine imprint_manager imprint_ui imprint_core imprint_utest imprint_warnings)
# Under wine a path starting with / resolves on drive Z:, which wine maps to the Linux root.
target_compile_definitions(imprint-tests PRIVATE IMPRINT_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_executable(argument_echo argument_echo.c)
target_link_libraries(argument_echo PRIVATE imprint_warnings)
add_dependencies(imprint-tests argument_echo)
target_compile_definitions(imprint-tests PRIVATE IMPRINT_TEST_ARGUMENT_ECHO="$<TARGET_FILE:argument_echo>")
add_test(NAME unit COMMAND imprint-tests)

add_test(NAME manager_version COMMAND imprint --version)
set_tests_properties(manager_version PROPERTIES PASS_REGULAR_EXPRESSION "^imprint ${PROJECT_VERSION}")
add_test(NAME engine_version COMMAND imprint-engine --version)
set_tests_properties(engine_version PROPERTIES PASS_REGULAR_EXPRESSION "^imprint-engine ${PROJECT_VERSION}")

add_test(NAME plan_round_trip
    COMMAND ${CMAKE_COMMAND}
        -DMANAGER=$<TARGET_FILE:imprint>
        -DENGINE=$<TARGET_FILE:imprint-engine>
        "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
        -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/plan_round_trip.cmake)
