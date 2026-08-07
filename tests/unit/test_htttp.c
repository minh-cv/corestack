#include <stdio.h>      // Standard I/O functions (printf, fprintf)
#include <stdlib.h>     // Standard library functions (malloc, free)
#include <string.h>     // String manipulation (strncmp, strchr)
#include "../../src/libhtttp/htttp.c"   // include source directly to access static functions under test
#include <unistd.h>     // POSIX API (close)

#include "unity.h"      // Unity testing framework
#include "common.h"     // Common utility functions and definitions
#include "unity_internals.h"


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
// TEST 1: test if parse_response_line correctly parses a valid response line
static void test_parse_response_line_valid(void) {
    unsigned char buf[] = "HTTTP/1.0 200 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(0, parse_response_line(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_INT(200, msg.response.status);
    TEST_ASSERT_EQUAL_STRING("OK", msg.response.reason);
    TEST_ASSERT_EQUAL_PTR(buf + 18, cursor); // cursor should point to the end of the buffer after parsing
}

// TEST 2: no space after HTTTP/1.0
static void test_parse_response_line_invalid_no_space_after_version(void) {
    unsigned char buf[] = "HTTTP/1.0200 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 3: space is at the very last byte of the buffer
static void test_parse_response_line_invalid_space_last_byte(void) {
    unsigned char buf[] = "HTTTP/1.0200 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 4: check if range of required status code is respected (from 100-599). for this we shall do robust boundary testing
static void test_parse_response_line_valid_status_code_range(void) {
    unsigned char buf[] = "HTTTP/1.0 100 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(0, parse_response_line(&cursor, end, &msg));

    unsigned char buf2[] = "HTTTP/1.0 599 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    cursor = buf2;
    end = buf2 + sizeof(buf2) - 1;

    TEST_ASSERT_EQUAL_INT(0, parse_response_line(&cursor, end, &msg));
}

// TEST 5: check if range of required status code is just outside (99, 600)
static void test_parse_response_line_invalid_status_code_range(void) {
    unsigned char buf[] = "HTTTP/1.0 99 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));

    unsigned char buf2[] = "HTTTP/1.0 600 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    cursor = buf2;
    end = buf2 + sizeof(buf2) - 1;

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 6: check if strtol converts string to longint given a base properly (that is if code is a non-numeric string)
static void test_parse_response_line_invalid_status_code_non_numeric(void) {
    unsigned char buf[] = "HTTTP/1.0 ABC OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 7: check training garbage in token [checking this condition: (unsigned char*)endptr != buffer]
static void test_parse_response_line_invalid_status_code_garbage(void) {
    unsigned char buf[] = "HTTTP/1.0 200abc OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 8: check for overflow of string [checking this condition: errno == ERANGE]
static void test_parse_response_line_invalid_status_code_overflow(void) {
    unsigned char buf[] = "HTTTP/1.0 999999999999\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 9: check if theres no spacing between the status code
static void test_parse_response_line_invalid_no_space_after_status_code(void) {
    unsigned char buf[] = "HTTTP/1.0 200OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 10: what if there is a double space between the htttp version and the status code
static void test_parse_response_line_invalid_double_space_after_version(void) {
    unsigned char buf[] = "HTTTP/1.0  200 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 11: what if there are leading zeros infront of the status code?
static void test_parse_response_line_invalid_leading_zeros_in_status_code(void) {
    unsigned char buf[] = "HTTTP/1.0 00200 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 12: checking when it uses skip_until_str, will it fail? say for example "HTTTP/1.0 404 Not Found\r\n", which is valid
static void test_parse_response_line_valid_skip_until_str(void) {
    unsigned char buf[] = "HTTTP/1.0 404 Not Found\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(0, parse_response_line(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_INT(404, msg.response.status);
    TEST_ASSERT_EQUAL_STRING("Not Found", msg.response.reason);
}

// TEST 13: check for the invalid one rn compared to test 12, where this time round we dont have the reason-phrase
static void test_parse_response_line_invalid_skip_until_str(void) {
    unsigned char buf[] = "HTTTP/1.0 200 \r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 14: check for missing CRLF terminator after the status code and reason phrase. should get -1 from skip_until_str
static void test_parse_response_line_invalid_missing_crlf(void) {
    unsigned char buf[] = "HTTTP/1.0 200 OK";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}

// TEST 15: what if for status message, its not seperated by a newline (\n), but instead a carriage return (\r). should get -1 from skip_until_str
static void test_parse_response_line_invalid_missing_newline(void) {
    unsigned char buf[] = "HTTTP/1.0 200 OK\r";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {0};

    TEST_ASSERT_EQUAL_INT(-1, parse_response_line(&cursor, end, &msg));
}


// UNIT TEST `parse_header`. the focus is to test on the request part for this case
// TEST 1: test if parse_header correctly parses a valid header line
static void test_parse_header_valid(void) {
    unsigned char buf[] = "Host: tetrish.local\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {
        .is_request = true
    };
    HtttpRequest* request = &msg.request;

    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING("Host", msg.request.header[0].key);
    TEST_ASSERT_EQUAL_STRING(" tetrish.local", msg.request.header[0].value);
    TEST_ASSERT_EQUAL_INT(1, request->header_count);           // check if header_count is incremented correctly
    TEST_ASSERT_EQUAL_PTR(buf + sizeof(buf) - 1, cursor); // cursor should point to the end of the buffer after parsing
}

// TEST 2: now test if parse_header is working correctly for a valid header line with multiple values
static void test_parse_header_valid_ignores_headers(void) {
    unsigned char buf[] = "Host: tetrish.local\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {
        .is_request = true
    };
    HtttpRequest* request = &msg.request;

    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING("Host", msg.request.header[0].key);
    TEST_ASSERT_EQUAL_STRING(" tetrish.local", msg.request.header[0].value);
    TEST_ASSERT_EQUAL_INT(1, request->header_count);           // check if header_count is incremented correctly
    TEST_ASSERT_EQUAL_PTR(buf + sizeof(buf) - 1, cursor); // cursor should point to the end of the buffer after parsing
}

// TEST 3: check if what happens if there is a missing :
static void test_parse_header_invalid_missing_colon(void) {
    unsigned char buf[] = "Host tetrish.local\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {
        .is_request = true
    };

    TEST_ASSERT_EQUAL_INT(-1, parse_header(&cursor, end, &msg));
}

// TEST 4: check if what happens if the colon is the last byte of the buffer
static void test_parse_header_invalid_colon_last_byte(void) {
    unsigned char buf[] = "Host:";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {
        .is_request = true
    };

    TEST_ASSERT_EQUAL_INT(-1, parse_header(&cursor, end, &msg));
}

// TEST 5: check if there is no CLRF at all
static void test_parse_header_no_clrf(void) {
    unsigned char buf[] = "Host: tetrish.local";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {
        .is_request = true
    };

    TEST_ASSERT_EQUAL_INT(-1, parse_header(&cursor, end, &msg));
}

// TEST 6: checks what if there is no value
static void test_parse_header_empty_value(void) {
    unsigned char buf[] = "Host:\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {
        .is_request = true
    };

    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING("", msg.request.header[0].value);
}

// TEST 7: checks if there is no key instead
static void test_parse_header_empty_key(void) {
    unsigned char buf[] = ": tetrish.local\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {
        .is_request = true
    };

    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING("", msg.request.header[0].key);
}

// TEST 8: what if there is a colon (:) inside the value?
static void test_parse_header_colon_in_value(void) {
    unsigned char buf[] = "Host: tetrish.local:135\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = {
        .is_request = true
    };

    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING(" tetrish.local:135", msg.request.header[0].value);
}

// TEST 9: checks if the header count increments
static void test_parse_header_two_consecutive_headers(void) {
    unsigned char buf[] = "Host: tetrish.local\r\n"
    "User: alice\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = { 
        .is_request = true 
    };

    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_INT(1, msg.request.header_count);
    TEST_ASSERT_EQUAL_STRING("Host", msg.request.header[0].key);
    TEST_ASSERT_EQUAL_STRING(" tetrish.local", msg.request.header[0].value);

    // second call: reuses the same (already-advanced) cursor to parse the next header
    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_INT(2, msg.request.header_count);
    TEST_ASSERT_EQUAL_STRING("User", msg.request.header[1].key);
    TEST_ASSERT_EQUAL_STRING(" alice", msg.request.header[1].value);

    // cursor should now sit exactly at the end of the buffer -- nothing left unparsed
    TEST_ASSERT_EQUAL_PTR(end, cursor);
}

// TEST 10: checks if `end` lands exactly 1 byte short of :
static void test_parse_header_ends_until(void) {
    unsigned char buf[] = "Host: tetrish.local\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + 4; // "Host" — stops exactly 1 byte before the ':'
    HtttpMessage msg = { 
        .is_request = true 
    };

    TEST_ASSERT_EQUAL_INT(-1, parse_header(&cursor, end, &msg));
}

// TEST 11: checks if parse_header fills last valid slot
static void test_parse_header_fills_last_valid_slot(void) {
    HtttpMessage msg = { .is_request = true };
    msg.request.header_count = HTTTP_HEADER_MAX - 1; // pretend we already have this many
    unsigned char buf[] = "Last: header\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;

    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_INT(HTTTP_HEADER_MAX, msg.request.header_count);
    TEST_ASSERT_EQUAL_STRING("Last", msg.request.header[HTTTP_HEADER_MAX - 1].key);
}

// TEST 12: checks if the retry loop for skip_until_str works as intended
static void test_parse_header_value_skips_false_positive_cr(void) {
    unsigned char buf[] = "Host: tet\rrish.local\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = { .is_request = true };

    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING(" tet\rrish.local", msg.request.header[0].value);
}

// TEST 13: checks if no trimming happens
static void test_parse_header_no_trimming_before_colon(void) {
    unsigned char buf[] = "Host :tetrish.local\r\n";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;
    HtttpMessage msg = { .is_request = true };

    TEST_ASSERT_EQUAL_INT(0, parse_header(&cursor, end, &msg));
    TEST_ASSERT_EQUAL_STRING("Host ", msg.request.header[0].key); // trailing space kept, not trimmed
}


// UNIT TEST: `parse_body`. nothing much just a few tests
// TEST 1: check if parse_body works for a normal body. this for request, body_len should equate to end-buffer, body should equate to buffer
static void test_parse_body_valid_request(void) {
    unsigned char buf[] = 
    "{"
        "\"arena\":         \"main\","
        "\"player_id\":     \"p17\","
        "\"board_width\":   \"10\","
        "\"board_height\":  \"20,"
        "\"next_tick\":     \"48122"
    "}";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;   // excludes training NUL
    HtttpMessage msg = { 
        .is_request = true 
    };

    parse_body(&cursor, end, &msg);

    TEST_ASSERT_EQUAL_UINT(sizeof(buf) - 1, msg.request.body_len);
    TEST_ASSERT_EQUAL_PTR(buf, msg.request.body);
}

// TEST 2: check if parse_body works for a normal body. this for response
static void test_parse_body_valid_response(void) {
    unsigned char buf[] = 
    "{"
        "\"arena\":         \"main\","
        "\"player_id\":     \"p17\","
        "\"board_width\":   \"10\","
        "\"board_height\":  \"20,"
        "\"next_tick\":     \"48122"
    "}";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;   // excludes training NUL
    HtttpMessage msg = { 
        .is_request = false 
    };

    parse_body(&cursor, end, &msg);

    TEST_ASSERT_EQUAL_UINT(sizeof(buf) - 1, msg.response.body_len);
    TEST_ASSERT_EQUAL_PTR(buf, msg.response.body);
}

// TEST 3: what happens if body is empty?
static void test_parse_body_empty_body(void) {
    unsigned char buf[] = "";
    unsigned char* cursor = buf;
    const unsigned char* end = buf;   // excludes training NUL
    HtttpMessage msg = { 
        .is_request = true 
    };

    parse_body(&cursor, end, &msg);

    TEST_ASSERT_EQUAL_UINT(0, msg.request.body_len);
    TEST_ASSERT_EQUAL_PTR(buf, msg.request.body);
}

// TEST 4: what happens if theres a NUL byte in the body? 
static void test_parse_body_contains_embedded_nul(void) {
    unsigned char buf[] = 
    "{"
        "\"arena\":         \"main\","
        "\"player_id\":     \"p17\","
        "\"board_width\":   \"10\","
        "\0"
        "\"board_height\":  \"20,"
        "\"next_tick\":     \"48122"
    "}";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;   // excludes training NUL
    HtttpMessage msg = { 
        .is_request = true 
    };

    parse_body(&cursor, end, &msg);

    TEST_ASSERT_EQUAL_UINT(sizeof(buf) - 1, msg.request.body_len);
    TEST_ASSERT_EQUAL_PTR(buf, msg.request.body); // check if pointers points to same memory location
    TEST_ASSERT_EQUAL_MEMORY(buf, msg.request.body, msg.request.body_len);
}

// TEST 5: what happens if the body includes clrf in the value?
static void test_parse_body_contains_clrf_in_value(void) {
    unsigned char buf[] = 
    "{"
        "\"arena\":         \"main\","
        "\"player_id\":     \"p17\r\n\","
        "\"board_width\":   \"10\","
        "\"board_height\":  \"20,"
        "\"next_tick\":     \"48122"
    "}";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;   // excludes training NUL
    HtttpMessage msg = { 
        .is_request = true 
    };

    parse_body(&cursor, end, &msg);

    TEST_ASSERT_EQUAL_UINT(sizeof(buf) - 1, msg.request.body_len);
    TEST_ASSERT_EQUAL_PTR(buf, msg.request.body); // check if pointers points to same memory location
    TEST_ASSERT_EQUAL_MEMORY(buf, msg.request.body, msg.request.body_len);
}

// TEST 6: what happens if the body includes clrf?
static void test_parse_body_contains_clrf(void) {
    unsigned char buf[] = 
    "{"
        "\"arena\":         \"main\","
        "\"player_id\":     \"p17\","
        "\r\n,"
        "\"board_width\":   \"10\","
        "\"board_height\":  \"20,"
        "\"next_tick\":     \"48122"
    "}";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;   // excludes training NUL
    HtttpMessage msg = { 
        .is_request = true 
    };

    parse_body(&cursor, end, &msg);

    TEST_ASSERT_EQUAL_UINT(sizeof(buf) - 1, msg.request.body_len);
    TEST_ASSERT_EQUAL_PTR(buf, msg.request.body); // check if pointers points to same memory location
    TEST_ASSERT_EQUAL_MEMORY(buf, msg.request.body, msg.request.body_len);
}

// TEST 7: checks if *buffer_ref updates after calling parse_body
static void test_parse_body_does_not_advance_cursor(void) {
    unsigned char buf[] = 
    "{"
        "\"arena\":         \"main\","
        "\"player_id\":     \"p17\","
        "\"board_width\":   \"10\","
        "\"board_height\":  \"20,"
        "\"next_tick\":     \"48122"
    "}";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;   // excludes training NUL
    HtttpMessage msg = { 
        .is_request = true 
    };

    parse_body(&cursor, end, &msg);

    TEST_ASSERT_EQUAL_PTR(buf, cursor);
    TEST_ASSERT_EQUAL_UINT(sizeof(buf) - 1, msg.request.body_len);
}

// TEST 8: checks if msg.request.body also changes if i change a byte in buf after calling parse_bpdy
static void test_parse_body_is_a_view_not_a_copy(void) {
    unsigned char buf[] = 
    "{"
        "\"arena\":         \"main\","
        "\"player_id\":     \"p17\","
        "\"board_width\":   \"10\","
        "\"board_height\":  \"20,"
        "\"next_tick\":     \"48122"
    "}";
    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf) - 1;   // excludes training NUL
    HtttpMessage msg = { 
        .is_request = true 
    };

    parse_body(&cursor, end, &msg);

    TEST_ASSERT_EQUAL_PTR(buf, msg.request.body); // same address, not a malloc copy

    // now i change one of the buf
    buf[2] = 'X';

    TEST_ASSERT_EQUAL_CHAR('X', ((const char*)msg.request.body)[2]);
}

// TEST 9: what if our body is big af, then does sanity-check trucates/or have weird behaviours?
static void test_parse_body_large_buffer(void) {
    static unsigned char buf[9000];

    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (unsigned char)('a' + (i % 26)); // repeating a-z pattern, no special chars needed
    }

    unsigned char* cursor = buf;
    const unsigned char* end = buf + sizeof(buf);
    HtttpMessage msg = { 
        .is_request = true 
    };

    parse_body(&cursor, end, &msg);

    TEST_ASSERT_EQUAL_UINT(sizeof(buf), msg.request.body_len);
    TEST_ASSERT_EQUAL_PTR(buf, msg.request.body);
    TEST_ASSERT_EQUAL_MEMORY(buf, msg.request.body, msg.request.body_len);
}


// UNIT TEST: `htttp_parse`
// TEST 1: test if the parsing is right for request
static void test_htttp_parse_valid_request(void) {
    unsigned char buffer[] = 
    "JOIN /arena/main HTTTP/1.0\r\n"
    "Host: tetrish.local\r\n"
    "User: alice\r\n"
    "Mode: battle-royale\r\n"
    "Client-Version: 1.0\r\n"
    "Content-Length: 35\r\n"
    "\r\n"
    "{\"skin\":\"cyan\",\"start_level\":0}";

    HtttpMessage msg = {0};

    int result = htttp_parse(buffer, sizeof(buffer) - 1, &msg); // omit training NUL

    // result
    TEST_ASSERT_EQUAL_INT(0, result);

    // request line
    TEST_ASSERT_TRUE(msg.is_request);
    TEST_ASSERT_EQUAL_STRING("JOIN", msg.request.method);
    TEST_ASSERT_EQUAL_STRING("/arena/main", msg.request.path);

    // request headers
    TEST_ASSERT_EQUAL_INT(5, msg.request.header_count);
    TEST_ASSERT_EQUAL_STRING("Host", msg.request.header[0].key);
    TEST_ASSERT_EQUAL_STRING(" tetrish.local", msg.request.header[0].value);

    TEST_ASSERT_EQUAL_STRING("User", msg.request.header[1].key);
    TEST_ASSERT_EQUAL_STRING(" alice", msg.request.header[1].value);

    TEST_ASSERT_EQUAL_STRING("Mode", msg.request.header[2].key);
    TEST_ASSERT_EQUAL_STRING(" battle-royale", msg.request.header[2].value);

    TEST_ASSERT_EQUAL_STRING("Client-Version", msg.request.header[3].key);
    TEST_ASSERT_EQUAL_STRING(" 1.0", msg.request.header[3].value);

    TEST_ASSERT_EQUAL_STRING("Content-Length", msg.request.header[4].key);
    TEST_ASSERT_EQUAL_STRING(" 35", msg.request.header[4].value);

    // request message body
    const char* expected_body = "{\"skin\":\"cyan\",\"start_level\":0}";
    TEST_ASSERT_EQUAL_UINT(strlen(expected_body), msg.request.body_len);
    TEST_ASSERT_EQUAL_MEMORY(expected_body, msg.request.body, msg.request.body_len);
}

// TEST 2: test if the parsing is right for response
static void test_htttp_parse_valid_response(void) {
    unsigned char buf[] =
    "HTTTP/1.0 200 OK\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Player-Id: p17\r\n"
    "Tick-Rate: 20\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 94\r\n"
    "\r\n"
    "{\"arena\":\"main\",\"player_id\":\"p17\",\"board_width\":10,\"board_height\":20,\"next_tick\":48122}";

    HtttpMessage msg = {0};

    int result = htttp_parse(buffer, sizeof(buffer) - 1, &msg); // omit training NUL

    // result
    TEST_ASSERT_EQUAL_INT(0, result);

    // status line
    TEST_ASSERT_FALSE(msg.is_request);
    TEST_ASSERT_EQUAL_INT(200, msg.response.status);
    TEST_ASSERT_EQUAL_STRING("OK", msg.response.reason);

    // response headers
    TEST_ASSERT_EQUAL_INT(5, msg.response.header_count);

    TEST_ASSERT_EQUAL_STRING("Session-Id", msg.response.header[0].key);
    TEST_ASSERT_EQUAL_STRING(" s-8f31a2", msg.response.header[0].value);

    





}

// TEST 3: checks if header parse cleanly but buffer is exactly consumed after last header
// expect -1
static void test_htttp_parse_headers_consume_everything_no_terminator(void) {
    unsigned char* buf[] = 
    "HTTTP/1.0 409 INVALID_MOVE\r\n"
    "Seq: 43\r\n"
    "Server-Tick: 48124\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 82\r\n"
    "\r\n"
    "{\"accepted\":false,\"reason\":\"collision\",\"authoritative_x\":3,\"authoritative_y\":14}";
}

// TEST 4: test if the parsing is right.
static void test_htttp_parse_one_stray_byte_after_headers(void) {
    unsigned char& buf[] =
    "GET /arena/main/state HTTTP/1.0\r\n"
    "Session-Id: s-8f31a2\r\n"
    "Accept: application/json\r\n"
    "\r\n";
}

// TEST 5: test if the parsing is right.
static void test_htttp_parse_zero_headers_straight_to_blank_line(void) {
    ...;
}

// TEST 6: test if the parsing is right.
static void test_htttp_parse_exactly_max_headers(void) {
    ...;
}

// TEST 7: test if the parsing is right.
static void test_htttp_parse_over_max_headers_fails(void) {
    ...;
}

// TEST 8: test if the parsing is right.
static void test_htttp_parse_dispatches_request(void) {
    ...;
}

// TEST 9: test if the parsing is right.
static void test_htttp_parse_dispatch_set_before_line_parse_failure(void) {
    ...;
}

// TEST 10: test if the parsing is right.
static void test_htttp_parse_invalid_request_line_propagates(void) {
    ...;
}

// TEST 11: test if the parsing is right.
static void test_htttp_parse_invalid_response_line_propagates(void) {
    ...;
}

// TEST 12: test if the parsing is right.
static void test_htttp_parse_missing_blank_line_terminator(void) {
    ...;
}

// TEST 13: test if the parsing is right.
static void test_htttp_parse_blank_line_terminator_present(void) {
    ...;
}

// TEST 14: test if the parsing is right.
static void test_htttp_parse_resets_stale_header_count(void) {
    ...;
}

// TEST 15: test if the parsing is right.
static void test_htttp_parse_invalid_header_propagates(void) {
    ...;
}

// TEST 16: test if the parsing is right.
static void test_htttp_parse_body_sliced_correctly(void) {
    ...;
}

// TEST 17: test if the parsing is right.
static void test_htttp_parse_empty_body(void) {
    ...;
}

// TEST 18: test if the parsing is right.
static void test_htttp_parse_empty_input(void) {
    ...;
}



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
    RUN_TEST(test_parse_response_line_valid);
    RUN_TEST(test_parse_response_line_invalid_no_space_after_version);
    RUN_TEST(test_parse_response_line_invalid_space_last_byte);
    RUN_TEST(test_parse_response_line_valid_status_code_range);
    RUN_TEST(test_parse_response_line_invalid_status_code_range);
    RUN_TEST(test_parse_response_line_invalid_status_code_non_numeric);
    RUN_TEST(test_parse_response_line_invalid_status_code_garbage);
    RUN_TEST(test_parse_response_line_invalid_status_code_overflow);
    RUN_TEST(test_parse_response_line_invalid_no_space_after_status_code);
    RUN_TEST(test_parse_response_line_invalid_double_space_after_version);
    RUN_TEST(test_parse_response_line_valid_skip_until_str);
    RUN_TEST(test_parse_response_line_invalid_skip_until_str);
    RUN_TEST(test_parse_response_line_invalid_missing_crlf);
    RUN_TEST(test_parse_response_line_invalid_missing_newline);
    RUN_TEST(test_parse_response_line_invalid_leading_zeros_in_status_code);
    RUN_TEST(test_parse_header_valid);
    RUN_TEST(test_parse_header_valid_ignores_headers);
    RUN_TEST(test_parse_header_invalid_missing_colon);
    RUN_TEST(test_parse_header_invalid_colon_last_byte);
    RUN_TEST(test_parse_header_no_clrf);
    RUN_TEST(test_parse_header_empty_value);
    RUN_TEST(test_parse_header_empty_key);
    RUN_TEST(test_parse_header_colon_in_value);
    RUN_TEST(test_parse_header_two_consecutive_headers);
    RUN_TEST(test_parse_header_ends_until);
    RUN_TEST(test_parse_header_fills_last_valid_slot);
    RUN_TEST(test_parse_header_value_skips_false_positive_cr);
    RUN_TEST(test_parse_header_no_trimming_before_colon);
    RUN_TEST(test_parse_body_valid_request);
    RUN_TEST(test_parse_body_valid_response);
    RUN_TEST(test_parse_body_empty_body);
    RUN_TEST(test_parse_body_contains_embedded_nul);
    RUN_TEST(test_parse_body_contains_clrf_in_value);
    RUN_TEST(test_parse_body_contains_clrf);
    RUN_TEST(test_parse_body_does_not_advance_cursor);
    RUN_TEST(test_parse_body_is_a_view_not_a_copy);
    RUN_TEST(test_parse_body_large_buffer);

    RUN_TEST(test_htttp_parse_valid_request);
    RUN_TEST(test_htttp_parse_valid_response);
    RUN_TEST(test_htttp_parse_headers_consume_everything_no_terminator);
    RUN_TEST(test_htttp_parse_one_stray_byte_after_headers);
    RUN_TEST(test_htttp_parse_zero_headers_straight_to_blank_line);
    RUN_TEST(test_htttp_parse_exactly_max_headers);
    RUN_TEST(test_htttp_parse_over_max_headers_fails);
    RUN_TEST(test_htttp_parse_dispatches_request);
    RUN_TEST(test_htttp_parse_dispatch_set_before_line_parse_failure);
    RUN_TEST(test_htttp_parse_invalid_request_line_propagates);
    RUN_TEST(test_htttp_parse_invalid_response_line_propagates);
    RUN_TEST(test_htttp_parse_missing_blank_line_terminator);
    RUN_TEST(test_htttp_parse_blank_line_terminator_present);
    RUN_TEST(test_htttp_parse_resets_stale_header_count);
    RUN_TEST(test_htttp_parse_invalid_header_propagates);
    RUN_TEST(test_htttp_parse_body_sliced_correctly);
    RUN_TEST(test_htttp_parse_empty_body);
    RUN_TEST(test_htttp_parse_empty_input);

    return UNITY_END();
}