set(IMPRINT_TEST_SOURCES
    main.c
    test_text.c
    test_error.c
    test_string_list.c
    test_document.c
    test_document_toml.c
    test_document_json.c
)

add_executable(imprint-tests ${IMPRINT_TEST_SOURCES})
target_link_libraries(imprint-tests PRIVATE imprint_core imprint_utest imprint_warnings)
# Under wine a path starting with / resolves on drive Z:, which wine maps to the Linux root.
target_compile_definitions(imprint-tests PRIVATE IMPRINT_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME unit COMMAND imprint-tests)

add_test(NAME manager_version COMMAND imprint --version)
set_tests_properties(manager_version PROPERTIES PASS_REGULAR_EXPRESSION "^imprint ${PROJECT_VERSION}")
add_test(NAME engine_version COMMAND imprint-engine --version)
set_tests_properties(engine_version PROPERTIES PASS_REGULAR_EXPRESSION "^imprint-engine ${PROJECT_VERSION}")
