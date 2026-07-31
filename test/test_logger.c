#include "unity.h"
#include "logger.h"
#include "wire.h"
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// 2023-11-14 22:13:20 UTC, so "%T %D" renders as "22:13:20 11/14/23"
#define FIXED_EPOCH ((time_t)1700000000)
#define FIXED_DATE  "22:13:20 11/14/23"

// last time_t gmtime_r accepts: one more overflows tm_year past INT_MAX
#define GMTIME_MAX ((time_t)67768036191676799)

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

// everything past the timestamp field is deterministic even without _at
static const char* body(const char* string) {
    const char* bracket = strchr(string, ']');
    return (bracket != NULL) ? bracket + 1 : string;
}

static char* captured = NULL;

static int capture(char* string) {
    free(captured);
    captured = string;
    return 0;
}

// length of "[date][file:line][severity][group] " for the fixed test parameters
static size_t fixed_prefix_length(void) {
    char* empty = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "%s", "");
    size_t length = strlen(empty) - 1; // minus the trailing newline
    free(empty);
    return length;
}

// build a log line whose strlen is exactly `total`, padding the message with 'z'
static char* make_log_of_length(size_t total) {
    size_t prefix = fixed_prefix_length();
    TEST_ASSERT_TRUE(total > prefix);

    size_t message_length = total - prefix - 1; // minus the trailing newline
    char* message = malloc(message_length + 1);
    TEST_ASSERT_NOT_NULL(message);
    memset(message, 'z', message_length);
    message[message_length] = '\0';

    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "%s", message);
    free(message);

    TEST_ASSERT_NOT_NULL(log);
    TEST_ASSERT_EQUAL_size_t(total, strlen(log));
    return log;
}

typedef void (*death_fn)(void);

// run fn in a child and require it to die on SIGABRT (a failed assert())
static void assert_aborts(death_fn fn) {
#ifdef NDEBUG
    (void)fn;
    TEST_IGNORE_MESSAGE("assert() compiled out under NDEBUG");
#else
    fflush(NULL);
    pid_t pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        fn();
        _exit(0);
    }

    int status = 0;
    TEST_ASSERT_EQUAL_INT(pid, waitpid(pid, &status, 0));
    TEST_ASSERT_TRUE_MESSAGE(WIFSIGNALED(status), "expected the child to die on a signal");
    TEST_ASSERT_EQUAL_INT(SIGABRT, WTERMSIG(status));
#endif
}

void setUp(void) {
    captured = NULL;
    logger_set_log_handler(logger_log_null);
}

void tearDown(void) {
    free(captured);
    captured = NULL;
    logger_set_log_handler(logger_log_null);
}

/* ------------------------------------------------------------------ */
/* time_t now                                                          */
/* ------------------------------------------------------------------ */

static void expect_date(time_t now, const char* expected) {
    char* log = _logger_make_log_at(now, LOG_INFO, "g", "a.c", 1, "m");
    TEST_ASSERT_NOT_NULL(log);
    char rendered[64];
    memcpy(rendered, log + 1, strlen(expected));
    rendered[strlen(expected)] = '\0';
    TEST_ASSERT_EQUAL_STRING(expected, rendered);
    free(log);
}

static void test_time_epoch(void) {
    expect_date((time_t)0, "00:00:00 01/01/70");
}

static void test_time_pre_epoch(void) {
    expect_date((time_t)-1, "23:59:59 12/31/69");
}

static void test_time_day_boundary(void) {
    expect_date((time_t)86399, "23:59:59 01/01/70");
    expect_date((time_t)86400, "00:00:00 01/02/70");
}

static void test_time_typical(void) {
    expect_date(FIXED_EPOCH, FIXED_DATE);
}

static void test_time_two_digit_year_is_ambiguous(void) {
    // known limitation: "%D" is "%m/%d/%y", so 2000 and 2100 are indistinguishable
    char* y2000 = _logger_make_log_at((time_t)946684800, LOG_INFO, "g", "a.c", 1, "m");
    char* y2100 = _logger_make_log_at((time_t)4102444800, LOG_INFO, "g", "a.c", 1, "m");
    TEST_ASSERT_EQUAL_STRING(y2000, y2100);
    TEST_ASSERT_EQUAL_STRING_LEN("[00:00:00 01/01/00]", y2000, strlen("[00:00:00 01/01/00]"));
    free(y2000);
    free(y2100);
}

static void test_time_max_four_digit_year(void) {
    expect_date((time_t)253402300799, "23:59:59 12/31/99");
}

static void test_time_gmtime_limit(void) {
    char* log = _logger_make_log_at(GMTIME_MAX, LOG_INFO, "g", "a.c", 1, "m");
    TEST_ASSERT_NOT_NULL_MESSAGE(log, "the last representable time_t must still format");
    free(log);
}

static void test_time_gmtime_overflow_returns_null(void) {
    TEST_ASSERT_NULL(_logger_make_log_at(GMTIME_MAX + 1, LOG_INFO, "g", "a.c", 1, "m"));
    TEST_ASSERT_NULL(_logger_make_log_at(INT64_MAX, LOG_INFO, "g", "a.c", 1, "m"));
    TEST_ASSERT_NULL(_logger_make_log_at(INT64_MIN, LOG_INFO, "g", "a.c", 1, "m"));
}

static void test_make_log_uses_current_time(void) {
    time_t before = time(NULL);
    char* log = _logger_make_log(LOG_INFO, "g", "a.c", 1, "m");
    time_t after = time(NULL);

    struct tm utc;
    char earliest[64];
    char latest[64];
    strftime(earliest, sizeof(earliest), "%T %D", gmtime_r(&before, &utc));
    strftime(latest, sizeof(latest), "%T %D", gmtime_r(&after, &utc));

    char rendered[64];
    memcpy(rendered, log + 1, strlen(earliest));
    rendered[strlen(earliest)] = '\0';

    // a second may tick over mid-call, so accept either boundary
    TEST_ASSERT_TRUE(strcmp(rendered, earliest) == 0 || strcmp(rendered, latest) == 0);
    free(log);
}

/* ------------------------------------------------------------------ */
/* enum LoggerSeverity severity                                        */
/* ------------------------------------------------------------------ */

static void test_severity_strings(void) {
    static const struct {
        LoggerSeverity severity;
        const char* name;
    } CASES[] = {
        {LOG_DEBUG, "DEBUG"},
        {LOG_INFO, "INFO"},
        {LOG_WARN, "WARN"},
        {LOG_ERROR, "ERROR"},
    };

    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "[a.c:1][%s][g] m\n", CASES[i].name);
        char* log = _logger_make_log_at(FIXED_EPOCH, CASES[i].severity, "g", "a.c", 1, "m");
        TEST_ASSERT_EQUAL_STRING(expected, body(log));
        free(log);
    }
}

static void call_with_severity_above_range(void) {
    free(_logger_make_log_at(FIXED_EPOCH, (LoggerSeverity)4, "g", "a.c", 1, "m"));
}

static void call_with_severity_below_range(void) {
    free(_logger_make_log_at(FIXED_EPOCH, (LoggerSeverity)-1, "g", "a.c", 1, "m"));
}

static void call_with_severity_int_max(void) {
    free(_logger_make_log_at(FIXED_EPOCH, (LoggerSeverity)INT_MAX, "g", "a.c", 1, "m"));
}

static void test_severity_out_of_range_aborts(void) {
    // severity_to_string's default arm is assert(false); the "?" return is unreachable
    assert_aborts(call_with_severity_above_range);
    assert_aborts(call_with_severity_below_range);
    assert_aborts(call_with_severity_int_max);
}

/* ------------------------------------------------------------------ */
/* const char* group                                                   */
/* ------------------------------------------------------------------ */

static void test_group_null_becomes_dash(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_DEBUG, NULL, "a.c", 1, "hi");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][DEBUG][-] hi\n", body(log));
    free(log);
}

static void test_group_empty(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "", "a.c", 1, "m");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][] m\n", body(log));
    free(log);
}

static void test_group_percent_is_not_a_format(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "%s%n%d", "a.c", 1, "m");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][%s%n%d] m\n", body(log));
    free(log);
}

static void test_group_brackets_render_literally(void) {
    // documents that a bracket-splitting log parser cannot trust field boundaries
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "a]b[c", "a.c", 1, "m");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][a]b[c] m\n", body(log));
    free(log);
}

static void test_group_newline_splits_the_record(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "a\nb", "a.c", 1, "m");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][a\nb] m\n", body(log));
    free(log);
}

static void test_group_long(void) {
    size_t length = 64 * 1024;
    char* group = malloc(length + 1);
    TEST_ASSERT_NOT_NULL(group);
    memset(group, 'g', length);
    group[length] = '\0';

    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, group, "a.c", 1, "m");
    TEST_ASSERT_NOT_NULL(log);
    TEST_ASSERT_NOT_NULL(strstr(log, group));
    TEST_ASSERT_EQUAL_CHAR('\n', log[strlen(log) - 1]);
    free(log);
    free(group);
}

/* ------------------------------------------------------------------ */
/* const char* file                                                    */
/* ------------------------------------------------------------------ */
/* NULL file is a contract violation (UB via "%s"); glibc renders       */
/* "(null)". Deliberately not tested.                                   */

static void test_file_empty(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "", 1, "m");
    TEST_ASSERT_EQUAL_STRING("[:1][INFO][g] m\n", body(log));
    free(log);
}

static void test_file_percent_is_not_a_format(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "%d.c", 1, "m");
    TEST_ASSERT_EQUAL_STRING("[%d.c:1][INFO][g] m\n", body(log));
    free(log);
}

static void test_file_path_max(void) {
    char* file = malloc(PATH_MAX + 1);
    TEST_ASSERT_NOT_NULL(file);
    memset(file, 'f', PATH_MAX);
    file[PATH_MAX] = '\0';

    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", file, 1, "m");
    TEST_ASSERT_NOT_NULL(log);
    TEST_ASSERT_NOT_NULL(strstr(log, file));
    free(log);
    free(file);
}

/* ------------------------------------------------------------------ */
/* int line                                                            */
/* ------------------------------------------------------------------ */

static void expect_line(int line, const char* expected) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", line, "m");
    TEST_ASSERT_NOT_NULL(log);
    TEST_ASSERT_EQUAL_STRING(expected, body(log));
    free(log);
}

static void test_line_values(void) {
    expect_line(0, "[a.c:0][INFO][g] m\n");
    expect_line(1, "[a.c:1][INFO][g] m\n");
    expect_line(-1, "[a.c:-1][INFO][g] m\n");
    expect_line(INT_MAX, "[a.c:2147483647][INFO][g] m\n");
    expect_line(INT_MIN, "[a.c:-2147483648][INFO][g] m\n");
}

/* ------------------------------------------------------------------ */
/* const char* fmt                                                     */
/* ------------------------------------------------------------------ */
/* NULL fmt, "%n", and a NULL argument to "%s" are contract violations. */
/* Deliberately not tested.                                             */

static void test_fmt_empty(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "%s", "");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][g] \n", body(log));
    free(log);
}

static void test_fmt_no_specifiers(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "plain text");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][g] plain text\n", body(log));
    free(log);
}

static void test_fmt_escaped_percent(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "100%%");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][g] 100%\n", body(log));
    free(log);
}

static void test_fmt_specifiers(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1,
                                    "%s %d %u %zu %c %x", "s", -5, 5u, (size_t)7, 'c', 0xabu);
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][g] s -5 5 7 c ab\n", body(log));
    free(log);
}

static void test_fmt_pointer(void) {
    int object = 0;
    char expected[128];
    snprintf(expected, sizeof(expected), "[a.c:1][INFO][g] %p\n", (void*)&object);
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "%p", (void*)&object);
    TEST_ASSERT_EQUAL_STRING(expected, body(log));
    free(log);
}

static void test_fmt_many_arguments(void) {
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1,
                                    "%d%d%d%d%d%d%d%d%d%d", 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][g] 0123456789\n", body(log));
    free(log);
}

/* ------------------------------------------------------------------ */
/* message length                                                      */
/* ------------------------------------------------------------------ */

static void test_length_zero_and_one(void) {
    char* empty = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "%s", "");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][g] \n", body(empty));
    free(empty);

    char* one = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "%s", "z");
    TEST_ASSERT_EQUAL_STRING("[a.c:1][INFO][g] z\n", body(one));
    free(one);
}

static void test_length_large_is_not_truncated(void) {
    // guards the two-snprintf split in logger_vmake_log
    char big[4096];
    memset(big, 'z', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_ERROR, "g", "a.c", 1, "%s", big);
    const char* head = "[" FIXED_DATE "][a.c:1][ERROR][g] ";
    TEST_ASSERT_EQUAL_size_t(strlen(head) + strlen(big) + 1, strlen(log));
    TEST_ASSERT_EQUAL_STRING_LEN(head, log, strlen(head));
    TEST_ASSERT_EQUAL_CHAR('z', log[strlen(log) - 2]);
    TEST_ASSERT_EQUAL_CHAR('\n', log[strlen(log) - 1]);
    free(log);
}

static void test_length_formatter_has_no_frame_limit(void) {
    // the formatter itself is happy past FRAME_MAX; only the ipc sink cares
    char* log = make_log_of_length(FRAME_MAX + 1);
    TEST_ASSERT_EQUAL_CHAR('\n', log[strlen(log) - 1]);
    free(log);
}

/* ------------------------------------------------------------------ */
/* handlers and sinks                                                  */
/* ------------------------------------------------------------------ */

static void test_default_handler_drops_the_line(void) {
    // logger_log_null is the sentinel handler: frees and reports failure
    TEST_ASSERT_EQUAL_INT(-1, LOGGER_LOG(LOG_ERROR, "grp", "dropped"));
}

static void test_log_handler_receives_formatted_line(void) {
    logger_set_log_handler(capture);
    TEST_ASSERT_EQUAL_INT(0, LOGGER_LOG(LOG_INFO, "grp", "v=%s", "ok"));
    TEST_ASSERT_NOT_NULL(captured);
    TEST_ASSERT_NOT_NULL(strstr(captured, "][INFO][grp] v=ok\n"));
}

static void test_perror_appends_strerror(void) {
    logger_set_log_handler(capture);
    errno = ENOENT;
    LOGGER_PERROR("grp", "open");
    TEST_ASSERT_NOT_NULL(captured);

    char expected[128];
    snprintf(expected, sizeof(expected), "][ERROR][grp] open: %s\n", strerror(ENOENT));
    TEST_ASSERT_NOT_NULL(strstr(captured, expected));
}

static void test_file_sink_writes_and_flushes(void) {
    FILE* sink = tmpfile();
    TEST_ASSERT_NOT_NULL(sink);
    logger_init_file(sink);

    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "to file");
    TEST_ASSERT_EQUAL_INT(0, _logger_log(log));

    // logger_log_file flushes, so the bytes are readable without closing
    char buf[256];
    rewind(sink);
    size_t n = fread(buf, 1, sizeof(buf) - 1, sink);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("[" FIXED_DATE "][a.c:1][INFO][g] to file\n", buf);

    TEST_ASSERT_EQUAL_INT(0, logger_free_file());
}

static void test_file_sink_reset_after_free(void) {
    FILE* sink = tmpfile();
    TEST_ASSERT_NOT_NULL(sink);
    logger_init_file(sink);
    TEST_ASSERT_EQUAL_INT(0, logger_free_file());

    // the handler must be back to the sentinel, not a dangling FILE*
    TEST_ASSERT_EQUAL_INT(-1, LOGGER_LOG(LOG_INFO, "grp", "after free"));
}

static void call_free_file_without_init(void) {
    logger_free_file();
}

static void call_free_ipc_without_init(void) {
    logger_free_ipc();
}

static void test_free_without_init_aborts(void) {
    assert_aborts(call_free_file_without_init);
    assert_aborts(call_free_ipc_without_init);
}

/* ------------------------------------------------------------------ */
/* ipc sink                                                            */
/* ------------------------------------------------------------------ */

static int listener_fd = -1;
static int peer_fd = -1;
static char socket_path[64];

static void ipc_start(void) {
    snprintf(socket_path, sizeof(socket_path), "/tmp/corestack-logger-test-%d.sock", (int)getpid());
    unlink(socket_path);

    listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    TEST_ASSERT_TRUE(listener_fd != -1);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, socket_path, strlen(socket_path));

    TEST_ASSERT_EQUAL_INT(0, bind(listener_fd, (struct sockaddr*)&addr, sizeof(addr)));
    TEST_ASSERT_EQUAL_INT(0, listen(listener_fd, 1));

    // the listen backlog lets connect() complete before accept()
    TEST_ASSERT_EQUAL_INT(0, logger_init_ipc(socket_path));
    peer_fd = accept(listener_fd, NULL, NULL);
    TEST_ASSERT_TRUE(peer_fd != -1);
}

static void ipc_stop(void) {
    TEST_ASSERT_EQUAL_INT(0, logger_free_ipc());
    close(peer_fd);
    close(listener_fd);
    unlink(socket_path);
    peer_fd = -1;
    listener_fd = -1;
}

static void read_exact(int fd, unsigned char* buf, size_t length) {
    size_t total = 0;
    while (total < length) {
        ssize_t n = read(fd, buf + total, length - total);
        TEST_ASSERT_TRUE_MESSAGE(n > 0, "short read from the ipc peer");
        total += (size_t)n;
    }
}

// receive one frame and assert it matches `expected`
static void expect_frame(const char* expected) {
    unsigned char header[4];
    read_exact(peer_fd, header, sizeof(header));
    uint32_t length = decode_u32_be(header);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(expected), length);

    unsigned char* payload = malloc(length + 1);
    TEST_ASSERT_NOT_NULL(payload);
    read_exact(peer_fd, payload, length);
    payload[length] = '\0';
    TEST_ASSERT_EQUAL_STRING(expected, (char*)payload);
    free(payload);
}

static void test_ipc_sink_frames_the_line(void) {
    ipc_start();
    char* log = _logger_make_log_at(FIXED_EPOCH, LOG_INFO, "g", "a.c", 1, "to ipc");
    TEST_ASSERT_EQUAL_INT(0, _logger_log(log));
    // the framed length includes the trailing newline
    expect_frame("[" FIXED_DATE "][a.c:1][INFO][g] to ipc\n");
    ipc_stop();
}

static void test_ipc_sink_frame_max_boundaries(void) {
    ipc_start();

    char* under = make_log_of_length(FRAME_MAX - 1);
    char* expected_under = strdup(under);
    TEST_ASSERT_NOT_NULL(expected_under);
    TEST_ASSERT_EQUAL_INT(0, _logger_log(under));
    expect_frame(expected_under);
    free(expected_under);

    char* exact = make_log_of_length(FRAME_MAX);
    char* expected_exact = strdup(exact);
    TEST_ASSERT_NOT_NULL(expected_exact);
    TEST_ASSERT_EQUAL_INT(0, _logger_log(exact));
    expect_frame(expected_exact);
    free(expected_exact);

    // one byte over FRAME_MAX is dropped by send_frame, nothing goes on the wire
    char* over = make_log_of_length(FRAME_MAX + 1);
    TEST_ASSERT_EQUAL_INT(-1, _logger_log(over));

    ipc_stop();
}

static void test_ipc_sink_reset_after_free(void) {
    ipc_start();
    ipc_stop();
    TEST_ASSERT_EQUAL_INT(-1, LOGGER_LOG(LOG_INFO, "grp", "after free"));
}

static void test_ipc_init_rejects_oversized_path(void) {
    char path[512];
    memset(path, 'p', sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, logger_init_ipc(path));
}

static void test_ipc_init_fails_on_missing_socket(void) {
    TEST_ASSERT_EQUAL_INT(-1, logger_init_ipc("/tmp/corestack-logger-test-does-not-exist.sock"));
}

/* ------------------------------------------------------------------ */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_time_epoch);
    RUN_TEST(test_time_pre_epoch);
    RUN_TEST(test_time_day_boundary);
    RUN_TEST(test_time_typical);
    RUN_TEST(test_time_two_digit_year_is_ambiguous);
    RUN_TEST(test_time_max_four_digit_year);
    RUN_TEST(test_time_gmtime_limit);
    RUN_TEST(test_time_gmtime_overflow_returns_null);
    RUN_TEST(test_make_log_uses_current_time);

    RUN_TEST(test_severity_strings);
    RUN_TEST(test_severity_out_of_range_aborts);

    RUN_TEST(test_group_null_becomes_dash);
    RUN_TEST(test_group_empty);
    RUN_TEST(test_group_percent_is_not_a_format);
    RUN_TEST(test_group_brackets_render_literally);
    RUN_TEST(test_group_newline_splits_the_record);
    RUN_TEST(test_group_long);

    RUN_TEST(test_file_empty);
    RUN_TEST(test_file_percent_is_not_a_format);
    RUN_TEST(test_file_path_max);

    RUN_TEST(test_line_values);

    RUN_TEST(test_fmt_empty);
    RUN_TEST(test_fmt_no_specifiers);
    RUN_TEST(test_fmt_escaped_percent);
    RUN_TEST(test_fmt_specifiers);
    RUN_TEST(test_fmt_pointer);
    RUN_TEST(test_fmt_many_arguments);

    RUN_TEST(test_length_zero_and_one);
    RUN_TEST(test_length_large_is_not_truncated);
    RUN_TEST(test_length_formatter_has_no_frame_limit);

    RUN_TEST(test_default_handler_drops_the_line);
    RUN_TEST(test_log_handler_receives_formatted_line);
    RUN_TEST(test_perror_appends_strerror);
    RUN_TEST(test_file_sink_writes_and_flushes);
    RUN_TEST(test_file_sink_reset_after_free);
    RUN_TEST(test_free_without_init_aborts);

    RUN_TEST(test_ipc_sink_frames_the_line);
    RUN_TEST(test_ipc_sink_frame_max_boundaries);
    RUN_TEST(test_ipc_sink_reset_after_free);
    RUN_TEST(test_ipc_init_rejects_oversized_path);
    RUN_TEST(test_ipc_init_fails_on_missing_socket);

    return UNITY_END();
}
