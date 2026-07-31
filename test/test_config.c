#include "unity.h"
#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// parse_line and parse_content have external linkage but are not in config.h
int parse_line(char* line_start, char** argn, char** argv);
int parse_content(Config* cfg, char* content);

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static Config cfg;
static char temp_path[64];

// write `content` to a temp .tetrishrc and return its path
static const char* write_config(const char* content) {
    snprintf(temp_path, sizeof(temp_path), "/tmp/corestack-config-test-%d.rc", (int)getpid());
    FILE* file = fopen(temp_path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    if (content[0] != '\0') {
        TEST_ASSERT_EQUAL_size_t(strlen(content), fwrite(content, 1, strlen(content), file));
    }
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
    return temp_path;
}

// parse_content mutates its input, so every case needs its own copy
static void parse(const char* content) {
    char* copy = strdup(content);
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_INT(0, parse_content(&cfg, copy));
    free(copy);
}

static void expect_arg(const char* directive, const char* expected) {
    size_t idx = config_get_arg_idx(&cfg, directive);
    TEST_ASSERT_TRUE_MESSAGE(idx != CONFIG_MAX_ARGS, directive);
    TEST_ASSERT_EQUAL_STRING(expected, cfg.argv[idx]);
}

static void expect_missing(const char* directive) {
    TEST_ASSERT_EQUAL_size_t(CONFIG_MAX_ARGS, config_get_arg_idx(&cfg, directive));
}

void setUp(void) {
    memset(&cfg, 0, sizeof(cfg));
    temp_path[0] = '\0';
}

void tearDown(void) {
    config_free(&cfg);
    if (temp_path[0] != '\0') {
        unlink(temp_path);
    }
}

/* ------------------------------------------------------------------ */
/* parse_line                                                          */
/* ------------------------------------------------------------------ */

// run parse_line on a mutable copy of `line`
static int try_parse_line(const char* line, char** argn, char** argv, char* buf, size_t buf_size) {
    TEST_ASSERT_TRUE(strlen(line) < buf_size);
    strcpy(buf, line);
    return parse_line(buf, argn, argv);
}

static void expect_line_ok(const char* line, const char* expected_argn, const char* expected_argv) {
    char buf[256];
    char* argn = NULL;
    char* argv = NULL;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, try_parse_line(line, &argn, &argv, buf, sizeof(buf)), line);
    TEST_ASSERT_EQUAL_STRING(expected_argn, argn);
    TEST_ASSERT_EQUAL_STRING(expected_argv, argv);
}

static void expect_line_rejected(const char* line) {
    char buf[256];
    char* argn = NULL;
    char* argv = NULL;
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, try_parse_line(line, &argn, &argv, buf, sizeof(buf)), line);
}

static void test_parse_line_basic(void) {
    expect_line_ok("listen_port=8080", "listen_port", "8080");
}

static void test_parse_line_skips_leading_whitespace(void) {
    expect_line_ok("   listen_port=8080", "listen_port", "8080");
    expect_line_ok("\t\v\flisten_port=8080", "listen_port", "8080");
}

static void test_parse_line_key_charset(void) {
    expect_line_ok("_leading=v", "_leading", "v");
    expect_line_ok("with_1_digits2=v", "with_1_digits2", "v");
    expect_line_ok("9=v", "9", "v");
}

static void test_parse_line_empty_value_is_allowed(void) {
    expect_line_ok("log_path=", "log_path", "");
}

static void test_parse_line_value_keeps_everything_after_the_first_equals(void) {
    expect_line_ok("a=b=c", "a", "b=c");
    expect_line_ok("a=  spaced  ", "a", "  spaced  ");
    // there is no comment syntax: a trailing # lands in the value
    expect_line_ok("a=b # note", "a", "b # note");
}

static void test_parse_line_strips_carriage_return(void) {
    expect_line_ok("a=b\r", "a", "b");
    // only up to the first \r
    expect_line_ok("a=b\rc", "a", "b");
}

static void test_parse_line_rejects_empty_and_blank(void) {
    expect_line_rejected("");
    expect_line_rejected("   ");
    expect_line_rejected("\t");
}

static void test_parse_line_rejects_missing_equals(void) {
    expect_line_rejected("listen_port 8080");
    expect_line_rejected("listen_port");
}

static void test_parse_line_rejects_spaces_around_equals(void) {
    // the key charset stops at the space, and the next char must be '='
    expect_line_rejected("a =b");
    expect_line_rejected("a\t=b");
}

static void test_parse_line_rejects_empty_key(void) {
    expect_line_rejected("=value");
    expect_line_rejected("  =value");
}

static void test_parse_line_rejects_comment_and_punctuation_keys(void) {
    // this is what makes '#' lines behave like comments
    expect_line_rejected("# a comment");
    expect_line_rejected("#a=b");
    expect_line_rejected("a-b=c");
    expect_line_rejected("a.b=c");
}

/* ------------------------------------------------------------------ */
/* parse_content                                                       */
/* ------------------------------------------------------------------ */

static void test_parse_content_empty(void) {
    parse("");
    TEST_ASSERT_EQUAL_size_t(0, cfg.argc);
}

static void test_parse_content_multiple_directives(void) {
    parse("listen_port=8080\naddress=127.0.0.1\nlog_path=var/log\n");
    TEST_ASSERT_EQUAL_size_t(3, cfg.argc);
    expect_arg("listen_port", "8080");
    expect_arg("address", "127.0.0.1");
    expect_arg("log_path", "var/log");
}

static void test_parse_content_skips_blank_and_invalid_lines(void) {
    parse("\n\n# comment\nlisten_port=8080\n   \nnot a directive\naddress=::1\n");
    TEST_ASSERT_EQUAL_size_t(2, cfg.argc);
    expect_arg("listen_port", "8080");
    expect_arg("address", "::1");
}

static void test_parse_content_without_trailing_newline(void) {
    parse("listen_port=8080");
    TEST_ASSERT_EQUAL_size_t(1, cfg.argc);
    expect_arg("listen_port", "8080");
}

static void test_parse_content_handles_crlf(void) {
    parse("listen_port=8080\r\naddress=::1\r\n");
    TEST_ASSERT_EQUAL_size_t(2, cfg.argc);
    expect_arg("listen_port", "8080");
    expect_arg("address", "::1");
}

static void test_parse_content_keeps_duplicates_and_first_wins(void) {
    parse("port=1\nport=2\n");
    TEST_ASSERT_EQUAL_size_t(2, cfg.argc);
    TEST_ASSERT_EQUAL_size_t(0, config_get_arg_idx(&cfg, "port"));
    TEST_ASSERT_EQUAL_STRING("1", cfg.argv[0]);
    TEST_ASSERT_EQUAL_STRING("2", cfg.argv[1]);
}

static void test_parse_content_stops_at_config_max_args(void) {
    char content[1024];
    size_t offset = 0;
    for (unsigned i = 0; i < CONFIG_MAX_ARGS + 8u; i++) {
        offset += (size_t)snprintf(content + offset, sizeof(content) - offset, "k%u=%u\n", i, i);
    }

    parse(content);
    TEST_ASSERT_EQUAL_size_t(CONFIG_MAX_ARGS, cfg.argc);
    expect_arg("k0", "0");
    expect_arg("k31", "31");
    expect_missing("k32");
}

/* ------------------------------------------------------------------ */
/* config_make                                                         */
/* ------------------------------------------------------------------ */

static void test_config_make_reads_a_file(void) {
    const char* path = write_config("listen_port=8080\naddress=127.0.0.1\n");
    TEST_ASSERT_EQUAL_INT(0, config_make(&cfg, path));
    TEST_ASSERT_EQUAL_size_t(2, cfg.argc);
    expect_arg("listen_port", "8080");
    expect_arg("address", "127.0.0.1");
}

static void test_config_make_empty_file(void) {
    const char* path = write_config("");
    TEST_ASSERT_EQUAL_INT(0, config_make(&cfg, path));
    TEST_ASSERT_EQUAL_size_t(0, cfg.argc);
}

static void test_config_make_missing_file(void) {
    TEST_ASSERT_EQUAL_INT(-1, config_make(&cfg, "/tmp/corestack-config-test-absent.rc"));
    TEST_ASSERT_EQUAL_size_t(0, cfg.argc);
}

static void test_config_make_directory(void) {
    // fopen("r") succeeds on a directory and ftell then reports LONG_MAX, so it
    // is the oversized malloc that fails rather than the read
    TEST_ASSERT_EQUAL_INT(-1, config_make(&cfg, "/tmp"));
    TEST_ASSERT_EQUAL_size_t(0, cfg.argc);
}

static void test_config_make_file_without_trailing_newline(void) {
    const char* path = write_config("listen_port=8080");
    TEST_ASSERT_EQUAL_INT(0, config_make(&cfg, path));
    expect_arg("listen_port", "8080");
}

/* ------------------------------------------------------------------ */
/* config_get_arg_idx                                                  */
/* ------------------------------------------------------------------ */

static void test_get_arg_idx_on_empty_config(void) {
    expect_missing("listen_port");
}

static void test_get_arg_idx_is_case_sensitive(void) {
    parse("listen_port=8080\n");
    expect_arg("listen_port", "8080");
    expect_missing("Listen_Port");
    expect_missing("LISTEN_PORT");
}

static void test_get_arg_idx_requires_a_full_match(void) {
    parse("listen_port=8080\n");
    expect_missing("listen");
    expect_missing("listen_port2");
    expect_missing("");
}

/* ------------------------------------------------------------------ */
/* config_get_path                                                     */
/* ------------------------------------------------------------------ */
/* A NULL project_dir with a relative value reaches strlen(NULL) in     */
/* concat_path. That is a contract violation, deliberately not tested.  */

static void expect_path(const char* directive, const char* project_dir, const char* expected) {
    char* path = config_get_path(&cfg, directive, project_dir);
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING(expected, path);
    free(path);
}

static void test_get_path_missing_directive(void) {
    parse("listen_port=8080\n");
    TEST_ASSERT_NULL(config_get_path(&cfg, "cert_path", "/srv/tetrish"));
}

static void test_get_path_relative_is_prefixed(void) {
    parse("cert_path=auth/cert.pem\n");
    expect_path("cert_path", "/srv/tetrish", "/srv/tetrish/auth/cert.pem");
}

static void test_get_path_absolute_ignores_project_dir(void) {
    parse("cert_path=/etc/ssl/cert.pem\n");
    expect_path("cert_path", "/srv/tetrish", "/etc/ssl/cert.pem");
    // project_dir is not even read on this path
    expect_path("cert_path", NULL, "/etc/ssl/cert.pem");
}

static void test_get_path_project_dir_trailing_slash(void) {
    parse("cert_path=auth/cert.pem\n");
    expect_path("cert_path", "/srv/tetrish/", "/srv/tetrish/auth/cert.pem");
    // only one trailing slash is trimmed
    expect_path("cert_path", "/srv/tetrish//", "/srv/tetrish//auth/cert.pem");
}

static void test_get_path_empty_value(void) {
    parse("cert_path=\n");
    expect_path("cert_path", "/srv/tetrish", "/srv/tetrish/");
}

/* ------------------------------------------------------------------ */
/* config_get_long_arg                                                 */
/* ------------------------------------------------------------------ */

static void expect_long(const char* content, const char* directive, long expected) {
    config_free(&cfg);
    parse(content);
    long out = 12345;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, config_get_long_arg(&cfg, directive, &out), content);
    TEST_ASSERT_EQUAL_INT64(expected, out);
}

static void expect_long_rejected(const char* content, const char* directive) {
    config_free(&cfg);
    parse(content);
    long out = 12345;
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, config_get_long_arg(&cfg, directive, &out), content);
}

static void test_get_long_decimal(void) {
    expect_long("max_events=64\n", "max_events", 64);
    expect_long("max_events=0\n", "max_events", 0);
    expect_long("max_events=-1\n", "max_events", -1);
    expect_long("max_events=+7\n", "max_events", 7);
}

static void test_get_long_uses_base_zero(void) {
    // a leading 0 means octal and a leading 0x means hex, which is easy to trip over
    expect_long("max_events=010\n", "max_events", 8);
    expect_long("max_events=0x10\n", "max_events", 16);
}

static void test_get_long_accepts_leading_whitespace(void) {
    // strtol skips leading blanks, and the value keeps them
    expect_long("max_events=   64\n", "max_events", 64);
}

static void test_get_long_rejects_trailing_garbage(void) {
    expect_long_rejected("max_events=64a\n", "max_events");
    expect_long_rejected("max_events=64 \n", "max_events");
    expect_long_rejected("max_events=6 4\n", "max_events");
    expect_long_rejected("max_events=abc\n", "max_events");
}

static void test_get_long_rejects_empty_value(void) {
    expect_long_rejected("max_events=\n", "max_events");
}

static void test_get_long_rejects_missing_directive(void) {
    config_free(&cfg);
    parse("listen_port=8080\n");
    long out = 12345;
    TEST_ASSERT_EQUAL_INT(-1, config_get_long_arg(&cfg, "max_events", &out));
    // out is untouched when the directive is absent
    TEST_ASSERT_EQUAL_INT64(12345, out);
}

static void test_get_long_limits(void) {
    char content[64];
    snprintf(content, sizeof(content), "v=%ld\n", LONG_MAX);
    expect_long(content, "v", LONG_MAX);
    snprintf(content, sizeof(content), "v=%ld\n", LONG_MIN);
    expect_long(content, "v", LONG_MIN);
}

static void test_get_long_overflow_clobbers_out(void) {
    parse("v=99999999999999999999\n");
    long out = 12345;
    TEST_ASSERT_EQUAL_INT(-1, config_get_long_arg(&cfg, "v", &out));
    // ERANGE is detected after strtol has already written the saturated value
    TEST_ASSERT_EQUAL_INT64(LONG_MAX, out);
}

/* ------------------------------------------------------------------ */
/* concat_path                                                         */
/* ------------------------------------------------------------------ */

static void expect_concat(const char* first, const char* second, const char* expected) {
    char* path = concat_path(first, second);
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING(expected, path);
    free(path);
}

static void test_concat_path_basic(void) {
    expect_concat("/srv", "auth", "/srv/auth");
    expect_concat("/srv", "auth/cert.pem", "/srv/auth/cert.pem");
}

static void test_concat_path_trims_one_trailing_slash(void) {
    expect_concat("/srv/", "auth", "/srv/auth");
    expect_concat("/srv//", "auth", "/srv//auth");
    expect_concat("/", "auth", "/auth");
}

static void test_concat_path_empty_operands(void) {
    // an empty first operand yields an absolute path
    expect_concat("", "auth", "/auth");
    expect_concat("/srv", "", "/srv/");
    expect_concat("", "", "/");
}

static void test_concat_path_second_leading_slash_doubles_up(void) {
    expect_concat("/srv", "/auth", "/srv//auth");
}

/* ------------------------------------------------------------------ */
/* config_free                                                         */
/* ------------------------------------------------------------------ */

static void test_config_free_resets_argc(void) {
    parse("a=1\nb=2\n");
    TEST_ASSERT_EQUAL_size_t(2, cfg.argc);
    config_free(&cfg);
    TEST_ASSERT_EQUAL_size_t(0, cfg.argc);
    expect_missing("a");
}

static void test_config_free_is_idempotent(void) {
    parse("a=1\n");
    config_free(&cfg);
    config_free(&cfg);
    TEST_ASSERT_EQUAL_size_t(0, cfg.argc);
}

static void test_config_free_on_zeroed_config(void) {
    Config empty;
    memset(&empty, 0, sizeof(empty));
    config_free(&empty);
    TEST_ASSERT_EQUAL_size_t(0, empty.argc);
}

/* ------------------------------------------------------------------ */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_parse_line_basic);
    RUN_TEST(test_parse_line_skips_leading_whitespace);
    RUN_TEST(test_parse_line_key_charset);
    RUN_TEST(test_parse_line_empty_value_is_allowed);
    RUN_TEST(test_parse_line_value_keeps_everything_after_the_first_equals);
    RUN_TEST(test_parse_line_strips_carriage_return);
    RUN_TEST(test_parse_line_rejects_empty_and_blank);
    RUN_TEST(test_parse_line_rejects_missing_equals);
    RUN_TEST(test_parse_line_rejects_spaces_around_equals);
    RUN_TEST(test_parse_line_rejects_empty_key);
    RUN_TEST(test_parse_line_rejects_comment_and_punctuation_keys);

    RUN_TEST(test_parse_content_empty);
    RUN_TEST(test_parse_content_multiple_directives);
    RUN_TEST(test_parse_content_skips_blank_and_invalid_lines);
    RUN_TEST(test_parse_content_without_trailing_newline);
    RUN_TEST(test_parse_content_handles_crlf);
    RUN_TEST(test_parse_content_keeps_duplicates_and_first_wins);
    RUN_TEST(test_parse_content_stops_at_config_max_args);

    RUN_TEST(test_config_make_reads_a_file);
    RUN_TEST(test_config_make_empty_file);
    RUN_TEST(test_config_make_missing_file);
    RUN_TEST(test_config_make_directory);
    RUN_TEST(test_config_make_file_without_trailing_newline);

    RUN_TEST(test_get_arg_idx_on_empty_config);
    RUN_TEST(test_get_arg_idx_is_case_sensitive);
    RUN_TEST(test_get_arg_idx_requires_a_full_match);

    RUN_TEST(test_get_path_missing_directive);
    RUN_TEST(test_get_path_relative_is_prefixed);
    RUN_TEST(test_get_path_absolute_ignores_project_dir);
    RUN_TEST(test_get_path_project_dir_trailing_slash);
    RUN_TEST(test_get_path_empty_value);

    RUN_TEST(test_get_long_decimal);
    RUN_TEST(test_get_long_uses_base_zero);
    RUN_TEST(test_get_long_accepts_leading_whitespace);
    RUN_TEST(test_get_long_rejects_trailing_garbage);
    RUN_TEST(test_get_long_rejects_empty_value);
    RUN_TEST(test_get_long_rejects_missing_directive);
    RUN_TEST(test_get_long_limits);
    RUN_TEST(test_get_long_overflow_clobbers_out);

    RUN_TEST(test_concat_path_basic);
    RUN_TEST(test_concat_path_trims_one_trailing_slash);
    RUN_TEST(test_concat_path_empty_operands);
    RUN_TEST(test_concat_path_second_leading_slash_doubles_up);

    RUN_TEST(test_config_free_resets_argc);
    RUN_TEST(test_config_free_is_idempotent);
    RUN_TEST(test_config_free_on_zeroed_config);

    return UNITY_END();
}
