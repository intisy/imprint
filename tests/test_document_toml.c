#include "document.h"
#include "utest.h"

#include <string.h>

static document_node *parse(const char *text, error_list *errors) {
    return document_parse_toml(text, "setup.toml", errors);
}

UTEST(toml, reads_values_tables_and_arrays_of_tables) {
    const char *text =
        "distro = \"arch\"\n"
        "persistence_size_mb = 32768\n"
        "ratio = 0.5\n"
        "expire_password = true\n"
        "apps = [\"git\", \"steam\"]\n"
        "\n"
        "[disk.data_partition]\n"
        "label = \"data\"\n"
        "\n"
        "[config_files]\n"
        "\".config/i3/config\" = \"config\"\n"
        "\n"
        "[[host_files]]\n"
        "source = 'C:\\tools\\drive-sync'\n"
        "\n"
        "[[host_files]]\n"
        "source = \"b\"\n";
    error_list errors;
    error_list_init(&errors);
    document_node *root = parse(text, &errors);
    ASSERT_TRUE(root != NULL);
    ASSERT_EQ((size_t)0, errors.count);
    ASSERT_STREQ("arch", document_get(root, "distro")->string_value);
    ASSERT_TRUE(document_get(root, "persistence_size_mb")->kind == DOCUMENT_INTEGER);
    ASSERT_EQ(32768LL, document_get(root, "persistence_size_mb")->integer_value);
    ASSERT_TRUE(document_get(root, "ratio")->kind == DOCUMENT_FLOAT);
    ASSERT_TRUE(document_get(root, "expire_password")->boolean_value);
    ASSERT_EQ((size_t)2, document_get(root, "apps")->child_count);
    ASSERT_EQ(5, document_get(root, "apps")->line);
    const document_node *partition = document_get(document_get(root, "disk"), "data_partition");
    ASSERT_STREQ("data", document_get(partition, "label")->string_value);
    ASSERT_STREQ("config", document_get(document_get(root, "config_files"), ".config/i3/config")->string_value);
    const document_node *host_files = document_get(root, "host_files");
    ASSERT_EQ((size_t)2, host_files->child_count);
    ASSERT_STREQ("C:\\tools\\drive-sync", document_get(host_files->children[0], "source")->string_value);
    document_free(root);
}

UTEST(toml, syntax_error_names_the_file_and_the_line) {
    error_list errors;
    error_list_init(&errors);
    document_node *root = parse("distro = \"arch\"\nname = \n", &errors);
    ASSERT_TRUE(root == NULL);
    ASSERT_EQ((size_t)1, errors.count);
    ASSERT_STREQ("setup.toml", errors.items[0].file);
    ASSERT_EQ(2, errors.items[0].line);
    ASSERT_TRUE(strncmp(errors.items[0].message, "(line", 5) != 0);
    error_list_free(&errors);
}

UTEST(toml, duplicate_key_is_an_error) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(parse("apps = []\napps = []\n", &errors) == NULL);
    ASSERT_EQ((size_t)1, errors.count);
    error_list_free(&errors);
}

UTEST(toml, dates_are_refused_with_their_key) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(parse("[install]\nwhen = 2026-10-17\n", &errors) == NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "install.when: dates and times are not supported"));
    error_list_free(&errors);
}

UTEST(toml, byte_order_mark_and_crlf_load_like_lf) {
    error_list windows_errors;
    error_list unix_errors;
    error_list_init(&windows_errors);
    error_list_init(&unix_errors);
    document_node *windows = parse("\xEF\xBB\xBF" "distro = \"arch\"\r\napps = [\"git\"]\r\n", &windows_errors);
    document_node *unix_text = parse("distro = \"arch\"\napps = [\"git\"]\n", &unix_errors);
    ASSERT_EQ((size_t)0, windows_errors.count);
    ASSERT_EQ((size_t)0, unix_errors.count);
    ASSERT_TRUE(windows != NULL);
    ASSERT_TRUE(unix_text != NULL);
    ASSERT_TRUE(document_equal(windows, unix_text));
    document_free(windows);
    document_free(unix_text);
}

UTEST(toml, string_with_a_nul_character_is_refused_with_its_key) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(parse("[install]\nname = \"a\\u0000b\"\n", &errors) == NULL);
    ASSERT_EQ((size_t)1, errors.count);
    ASSERT_TRUE(error_list_mentions(&errors, "install.name: contains a NUL character"));
    error_list_free(&errors);
}
