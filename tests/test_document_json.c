#include "document.h"
#include "utest.h"

#include <stdlib.h>

static document_node *parse(const char *text, error_list *errors) {
    return document_parse_json(text, "app.json", errors);
}

UTEST(json, reads_objects_numbers_and_nested_arrays) {
    error_list errors;
    error_list_init(&errors);
    document_node *root = parse(
        "{\"id\": \"git\", \"count\": 3, \"ratio\": 0.5, \"flag\": false,"
        " \"install\": {\"arch\": {\"packages\": [\"git\", \"base-devel\"]}}}", &errors);
    ASSERT_TRUE(root != NULL);
    ASSERT_TRUE(document_get(root, "count")->kind == DOCUMENT_INTEGER);
    ASSERT_EQ(3LL, document_get(root, "count")->integer_value);
    ASSERT_TRUE(document_get(root, "ratio")->kind == DOCUMENT_FLOAT);
    ASSERT_FALSE(document_get(root, "flag")->boolean_value);
    const document_node *packages = document_get(document_get(document_get(root, "install"), "arch"), "packages");
    ASSERT_STREQ("base-devel", packages->children[1]->string_value);
    document_free(root);
}

UTEST(json, syntax_error_names_the_line) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(parse("{\n\"id\": \"git\",\n\"name\" \"Git\"\n}\n", &errors) == NULL);
    ASSERT_EQ((size_t)1, errors.count);
    ASSERT_STREQ("app.json", errors.items[0].file);
    ASSERT_EQ(3, errors.items[0].line);
    error_list_free(&errors);
}

UTEST(json, duplicate_keys_are_an_error) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(parse("{\"id\": \"a\", \"id\": \"b\"}", &errors) == NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "id: duplicate key"));
    error_list_free(&errors);
}

UTEST(json, null_is_refused) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(parse("{\"config\": null}", &errors) == NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "config: null is not supported"));
    error_list_free(&errors);
}

UTEST(json, the_top_level_must_be_an_object) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(parse("[\"git\"]", &errors) == NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "the top level must be an object"));
    error_list_free(&errors);
}

UTEST(json, a_nul_escape_in_a_value_is_refused_with_its_key_path) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(parse("{\"install\": {\"name\": \"ab\\u0000cd\"}}", &errors) == NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "install.name: contains a NUL character, which is not supported"));
    error_list_free(&errors);
}

UTEST(json, a_nul_escape_in_a_key_is_refused) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(parse("{\"ab\\u0000cd\": \"x\"}", &errors) == NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "contains a NUL character, which is not supported"));
    error_list_free(&errors);
}

UTEST(json, an_escaped_backslash_before_u0000_is_not_a_nul) {
    error_list errors;
    error_list_init(&errors);
    document_node *root = parse("{\"path\": \"a\\\\u0000b\"}", &errors);
    ASSERT_TRUE(root != NULL);
    ASSERT_EQ((size_t)0, errors.count);
    ASSERT_STREQ("a\\u0000b", document_get(root, "path")->string_value);
    document_free(root);
}

UTEST(json, byte_order_mark_and_crlf_load_like_lf) {
    error_list marked_errors;
    error_list plain_errors;
    error_list_init(&marked_errors);
    error_list_init(&plain_errors);
    document_node *marked = parse("\xEF\xBB\xBF" "{\r\n  \"id\": \"git\",\r\n  \"count\": 3\r\n}\r\n", &marked_errors);
    document_node *plain = parse("{\n  \"id\": \"git\",\n  \"count\": 3\n}\n", &plain_errors);
    ASSERT_EQ((size_t)0, marked_errors.count);
    ASSERT_EQ((size_t)0, plain_errors.count);
    ASSERT_TRUE(marked != NULL);
    ASSERT_TRUE(plain != NULL);
    ASSERT_STREQ("git", document_get(marked, "id")->string_value);
    ASSERT_TRUE(document_equal(marked, plain));
    document_free(marked);
    document_free(plain);
}

UTEST(json, writer_round_trips_a_toml_document) {
    error_list errors;
    error_list_init(&errors);
    document_node *original = document_parse_toml(
        "distro = \"arch\"\nsize = 32768\nratio = 0.5\nlive = true\napps = [\"git\"]\n"
        "[disk]\nlabel = \"data\"\n[[host_files]]\nsource = \"a\\\\b \\\"quoted\\\"\"\n", "setup.toml", &errors);
    ASSERT_TRUE(original != NULL);
    char *json = document_to_json(original);
    document_node *again = parse(json, &errors);
    ASSERT_EQ((size_t)0, errors.count);
    ASSERT_TRUE(document_equal(original, again));
    free(json);
    document_free(original);
    document_free(again);
}

UTEST(json, load_file_picks_the_parser_by_extension) {
    error_list errors;
    error_list_init(&errors);
    document_node *toml = document_load_file(IMPRINT_TEST_FIXTURES "/documents/sample.toml", &errors);
    document_node *json = document_load_file(IMPRINT_TEST_FIXTURES "/documents/sample.json", &errors);
    ASSERT_TRUE(toml != NULL && json != NULL);
    ASSERT_TRUE(document_equal(toml, json));
    ASSERT_TRUE(document_load_file(IMPRINT_TEST_FIXTURES "/documents/sample.txt", &errors) == NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "sample.txt: not a .toml or .json file"));
    ASSERT_TRUE(document_load_file(IMPRINT_TEST_FIXTURES "/documents/missing.toml", &errors) == NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "missing.toml: cannot read"));
    document_free(toml);
    document_free(json);
    error_list_free(&errors);
}
