#include <stdio.h>      // Standard I/O functions (printf, fprintf)
#include <stdlib.h>     // Standard library functions (malloc, free)
#include <string.h>     // String manipulation (strncmp, strchr)
#include <htttp.h>      // The htttp library header
#include <unistd.h>     // POSIX API (close)

#include "unity.h"      // Unity testing framework
#include "common.h"     // Common utility functions and definitions


// can contain anything to run before each test
void setUp(void) {}

// can contain anything to run after each test
void tearDown(void) {}


// UNIT TEST `skip_until`
// TEST 1: test if skip_until correctly finds the target character in a buffer
static void test_skip_until_finds_target(void) {
    unsigned char buf[] = "Header:HJ8DLkFZ\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1; // exclude trailing NUL

    TEST_ASSERT_EQUAL_INT(0, skip_until(&cursor, end, ':'));
    TEST_ASSERT_EQUAL_PTR(buf + 6, cursor); // points AT the ':', not past it
}

// TEST 2: test if skip_until correctly reports a missing target character
static void test_skip_until_reports_missing_target(void) {
    unsigned char buf[] = "HeaderHJ8DLkFZ\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;

    TEST_ASSERT_EQUAL_INT(-1, skip_until(&cursor, end, ':'));
    TEST_ASSERT_EQUAL_PTR(buf, cursor); // unchanged on failure
}

// TEST 3: test if skip_until checks for buffer overflow and returns -1 when the target character is not found
static void test_skip_until_respects_end_boundary(void) {
    unsigned char buf[] = "Header:HJ8DLkFZ\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + 6; // stops before the ':' character

    TEST_ASSERT_EQUAL_INT(-1, skip_until(&cursor, end, ':'));
    TEST_ASSERT_EQUAL_PTR(buf, cursor); // unchanged on failure
}

// TEST 4: test if skip_until correctly reports NUL
static void test_skip_until_reports_nul(void) {
    unsigned char buf[] = {'a', 'b', 'c', '\0', 'd', 'e', 'f'};
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf);

    TEST_ASSERT_EQUAL_INT(0, skip_until(&cursor, end, 'e'));
    TEST_ASSERT_EQUAL_PTR(buf + 5, cursor); // found past NUL
}


// UNIT TEST `skip_until_str`
// TEST 1: test if skip_until_str correctly finds the target string in a buffer
static void test_skip_until_str_finds_target(void) {
    unsigned char buf[] = "Header:HJ8DLkFZ\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;

    TEST_ASSERT_EQUAL_INT(0, skip_until_str(&cursor, end, "\r\n"));
    TEST_ASSERT_EQUAL_PTR(buf + 15, cursor); // points AT the '\r', not past it
}

// TEST 2: test if skip_until_str correctly reports a missing target string
static void test_skip_until_str_reports_missing_target(void) {
    unsigned char buf[] = "Header:HJ8DLkFZ";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;

    TEST_ASSERT_EQUAL_INT(-1, skip_until_str(&cursor, end, "\r\n"));
    TEST_ASSERT_EQUAL_PTR(buf, cursor); // unchanged on failure
}

// TEST 3: test if skip_until_str correctly reports NUL
// cant use string literal since NUL will truncate itself
static void test_skip_until_str_reports_nul(void) {
    unsigned char buf[] = {'a', 'b', 'c', '\0', 'd', '\r', '\n'};
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf);

    TEST_ASSERT_EQUAL_INT(0, skip_until_str(&cursor, end, "\r\n"));
    TEST_ASSERT_EQUAL_PTR(buf + 5, cursor); // found past NUL
}

// TEST 4: test if skip_until_str checks for buffer overflow and returns -1 when the target string is not found
static void test_skip_until_str_checks_buffer_overflow(void) {
    unsigned char buf[] = "Header:HJ8DLkFZ\r";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;

    TEST_ASSERT_EQUAL_INT(-1, skip_until_str(&cursor, end, "\r\n"));
}

// TEST 5: test if skip_until_str recusive loop works
static void test_skip_until_str_skips_false_positives(void) {
    unsigned char buf[] = "ab\rcd\r\nxx";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;

    TEST_ASSERT_EQUAL_INT(0, skip_until_str(&cursor, end, "\r\n"));
    TEST_ASSERT_EQUAL_PTR(buf + 5, cursor); // skipped to the '\r' of the "\r\n" sequence
}


// UNIT TEST `does_start_with_htttp_1_0`
// TEST 1: test if does_start_with_htttp_1_0 correctly identifies a valid HTTTP/1.0 start
static void test_does_start_with_htttp_1_0_valid(void) {
    unsigned char buf[] = "HTTTP/1.0 200 OK\r\n";
    const unsigned char* end = buf + sizeof(buf) - 1;
    TEST_ASSERT_TRUE(does_start_with_htttp_1_0(buf, end));
}

// TEST 2: test if does_start_with_htttp_1_0 correctly identifies an invalid start
static void test_does_start_with_htttp_1_0_invalid(void) {
    unsigned char buf[] = "HTTTP/1.1 200 OK\r\n";
    const unsigned char* end = buf + sizeof(buf) - 1;
    TEST_ASSERT_FALSE(does_start_with_htttp_1_0(buf, end));
}


// UNIT TEST `parse_request_line`
// TEST 1: test if parse_request_line correctly parses a valid request line
static void test_parse_request_line_valid(void) {
    // req line in this form: METHOD SP PATH SP "HTTTP/1.0" CRLF
    unsigned char buf[] = "ROTATE /room/67/player/69 HTTTP/1.0\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(0, parse_request_line(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING("ROTATE", msg.request.method);
    TEST_ASSERT_EQUAL_STRING("/room/67/player/69", msg.request.path);
    TEST_ASSERT_EQUAL_PTR(buf + sizeof(buf) - 1, cursor); // cursor should point to the end of the buffer after parsing
}

// TEST 2: test if parse_request_line correctly handles an invalid request line
static void test_parse_request_line_invalid_method(void) {
    unsigned char buf[] = "NUCLEARIZE /room/67/player/69 HTTTP/1.0\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(0, parse_request_line(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING("NUCLEARIZE", msg.request.method);
}

// TEST 3: test if parse_request_line correctly handles no method seperators
static void test_parse_request_line_invalid_no_separators(void) {
    unsigned char buf[] = "ROTATE/room/67/player/69HTTTP/1.0\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_request_line(&cursor, end, &msg));
}

// TEST 4: test if parse_request_line handles properly if the last byte is a space
static void test_parse_request_line_invalid_trailing_space(void) {
    unsigned char buf[] = "ROTATE /room/67/player/69 HTTTP/1.0 \r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_request_line(&cursor, end, &msg));
}

// TEST 5: test if parse_request_line correctly handles if theres no space after path
static void test_parse_request_line_invalid_no_space_after_path(void) {
    unsigned char buf[] = "ROTATE /room/67/player/69HTTTP/1.0\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_request_line(&cursor, end, &msg));
}

// TEST 6: test if parse_request_line correctly handles if 2nd spacing is actually the last byte
static void test_parse_request_line_invalid_2nd_space_last_byte(void) {
    unsigned char buf[] = "ROTATE /room/67/player/69 ";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_request_line(&cursor, end, &msg));
}

// TEST 7: test if parse_request_line correctly handles messed up req line
static void test_parse_request_line_invalid_messed_up(void) {
    unsigned char buf[] = "ROTATE HTTTP/1.0 /room/67/player/69 \r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_request_line(&cursor, end, &msg));
}

// TEST 8: too few bytes for version check
static void test_parse_request_line_invalid_too_few_bytes(void) {
    unsigned char buf[] = "ROTATE /room/67/player/69 HTTTP/1.0\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + 30;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_request_line(&cursor, end, &msg));
}

// TEST 9: no CRLF
static void test_parse_request_line_invalid_no_crlf(void) {
    unsigned char buf[] = "ROTATE /room/67/player/69 HTTTP/1.0";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_request_line(&cursor, end, &msg));
}

// TEST 10: check case sensitive
static void test_parse_request_line_invalid_case_sensitive(void) {
    unsigned char buf[] = "rotate /room/67/player/69 httTP/1.0\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_request_line(&cursor, end, &msg));
}

// TEST 11: check what if the request line got mashed up with the required headers
static void test_parse_request_line_valid_mashed_up_with_headers(void) {
    unsigned char buf[] = "ROTATE /room/67/player/69 HTTTP/1.0\r\nHeader:tetrish.local\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(0, parse_request_line(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING("ROTATE", msg.request.method);
    TEST_ASSERT_EQUAL_STRING("/room/67/player/69", msg.request.path);
    TEST_ASSERT_EQUAL_PTR(buf + 37, cursor); // cursor should point to the end
}


// UNIT TEST `parse_response_line`




int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_skip_until_finds_target);
    RUN_TEST(test_skip_until_reports_missing_target);
    RUN_TEST(test_skip_until_reports_nul);
    RUN_TEST(test_skip_until_respects_end_boundary);
    RUN_TEST(test_skip_until_str_finds_target);
    RUN_TEST(test_skip_until_str_reports_missing_target);
    RUN_TEST(test_skip_until_str_reports_nul);
    RUN_TEST(test_skip_until_str_checks_buffer_overflow);
    RUN_TEST(test_skip_until_str_skips_false_positives);
    RUN_TEST(test_does_start_with_htttp_1_0_valid);
    RUN_TEST(test_does_start_with_htttp_1_0_invalid);
    RUN_TEST(test_parse_request_line_valid);
    RUN_TEST(test_parse_request_line_invalid_method);
    RUN_TEST(test_parse_request_line_invalid_no_separators);
    RUN_TEST(test_parse_request_line_invalid_trailing_space);
    RUN_TEST(test_parse_request_line_invalid_no_space_after_path);
    RUN_TEST(test_parse_request_line_invalid_2nd_space_last_byte);
    RUN_TEST(test_parse_request_line_invalid_messed_up);
    RUN_TEST(test_parse_request_line_invalid_too_few_bytes);
    RUN_TEST(test_parse_request_line_invalid_no_crlf);
    RUN_TEST(test_parse_request_line_invalid_case_sensitive);
    RUN_TEST(test_parse_request_line_valid_mashed_up_with_headers);
    return UNITY_END();
}
