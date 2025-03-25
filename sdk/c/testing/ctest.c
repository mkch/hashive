#include "ctest.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG

#define ERROR_ABORT_MSG(func, msg)                               \
    {                                                            \
        fprintf(stderr, "[ERROR] '%s' failed: %s\n", func, msg); \
        abort();                                                 \
    }

#define ERROR_ABORT(func)                               \
    {                                                   \
        fprintf(stderr, "[ERROR] '%s' failed\n", func); \
        abort();                                        \
    }

#else

#define ERROR_ABORT_MSG(func, msg)                                                          \
    {                                                                                       \
        fprintf(stderr, "[ERROR] %s:%d: '%s' failed: %s\n", __FILE__, __LINE__, func, msg); \
        abort();                                                                            \
    }

#define ERROR_ABORT(func)                                                          \
    {                                                                              \
        fprintf(stderr, "[ERROR] %s:%d: '%s' failed\n", __FILE__, __LINE__, func); \
        abort();                                                                   \
    }

#endif

typedef int64_t duration;

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>

typedef LARGE_INTEGER time_spec;

// The frequency of the performance counter is fixed at system boot
// and is consistent across all processors. 
// Therefore, the frequency need only be queried upon application
// initialization, and the result can be cached.
static LARGE_INTEGER performance_freq = {0};

void get_time(time_spec* t) {
    if(performance_freq.QuadPart == 0) {
        if(!QueryPerformanceFrequency(&performance_freq)) {
            char msg_buf[64] = {0};
            snprintf(msg_buf, sizeof(msg_buf)/sizeof(msg_buf[0]),"Last Error=0x%lx", GetLastError());
            ERROR_ABORT_MSG("QueryPerformanceFrequency", msg_buf);
        }
    }

    if(!QueryPerformanceCounter(t)) {
        char msg_buf[64] = {0};
        snprintf(msg_buf, sizeof(msg_buf)/sizeof(msg_buf[0]),"Last Error=0x%lx", GetLastError());
        ERROR_ABORT_MSG("QueryPerformanceFrequency", msg_buf);
    }
}

/**
 * Calculates the duration of t1 - t2, in nanoseconds.
 */
duration time_sub_nsec(time_spec* t1, time_spec* t2) {
    return (t1->QuadPart - t2->QuadPart)*1000000000 / performance_freq.QuadPart;
}


#else

#include <time.h>
typedef struct timespec time_spec;

void get_time(time_spec* t) {
    if (clock_gettime(CLOCK_MONOTONIC, t) != 0) {
        ERROR_ABORT_MSG("clock_gettime", strerror(errno));
    }
}

/**
 * Calculates the duration of t1 - t2, in nanoseconds.
 */
duration time_sub_nsec(time_spec* t1, time_spec* t2) {
    duration result = 0;
    if ((t1->tv_nsec - t2->tv_nsec) < 0) {
        result = ((duration)(t1->tv_sec) - 1 - (t2->tv_sec)) * 1000000000;
        result += (duration)(1000000000 + t1->tv_nsec - t2->tv_nsec);
    } else {
        result = ((duration)(t1->tv_sec) - (t2->tv_sec)) * 1000000000;
        result += (duration)(t1->tv_nsec - t2->tv_nsec);
    }
    return result;
}

#endif

/**
 * Format duration d.
 */
int duration_format_nsec(duration d, char* buf, size_t buf_size) {
    if (d < 100000) {  // 0.1ms
        return snprintf(buf, buf_size, "%dns", (int)d);
    } else if (d < 100000000) {  // 0.1s
        return snprintf(buf, buf_size, "%0.3fms", (double)d / 1000000);
    } else {
        return snprintf(buf, buf_size, "%0.3fs", (double)d / 1000000000);
    }
}

typedef struct ctest_test {
    const char* name;  // name of test. WILL NOT FREE.
    ctest_test_func f;
    bool failed;
    size_t log_message_count;
    char** log_messages;
} ctest_test;

static ctest_test* ctest_test_create() {
    return calloc(1, sizeof(ctest_test));
}

static void ctest_test_free(ctest_test* test) {
    for (size_t i = 0; i < test->log_message_count; i++) {
        free(test->log_messages[i]);
    }
    free(test);
}

static char* vlog_ln(const char* file, int line, const char* format, va_list args) {
    va_list args2;
    va_copy(args2, args);
    int content_len = vsnprintf(NULL, 0, format, args);
    if (content_len < 0)
        ERROR_ABORT("vsnprintf");
    char* content = calloc(content_len + 1, sizeof(char));
    if (vsnprintf(content, content_len + 1, format, args2) < 0)
        ERROR_ABORT("vsnprintf");
    va_end(args2);
    const bool end_with_n = content_len > 0 && content[content_len - 1] == '\n';
    const char* fmt = end_with_n ? "    %s:%d: %s" : "    %s:%d: %s\n";
    int output_len = snprintf(NULL, 0, fmt, file, line, content);
    char* output = calloc(output_len + 1, sizeof(char));
    if (!output)
        ERROR_ABORT("calloc");
    if (snprintf(output, output_len + 1, fmt, file, line, content) < 0)
        ERROR_ABORT("snprintf");
    free(content);
    return output;
}

static void ctest_test_vlog(ctest_test* test, bool verbose, const char* file, int line, const char* format, va_list args) {
    char* message = vlog_ln(file, line, format, args);
    if (verbose || test->failed) {
        printf("%s", message);
        free(message);
    } else {
        // save it.
        test->log_message_count++;
        test->log_messages = realloc(test->log_messages,
                                     sizeof(test->log_messages[0]) * test->log_message_count);
        test->log_messages[test->log_message_count - 1] = message;
    }
}

void ctest_test_log(ctest_test* test, bool verbose, const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    ctest_test_vlog(test, verbose, file, line, format, args);
    va_end(args);
}

void ctest_test_fail(ctest_test* test) {
    test->failed = true;
    // Print accumulated log messages.
    for (size_t i = 0; i < test->log_message_count; i++) {
        printf("%s", test->log_messages[i]);
        free(test->log_messages[i]);
    }
    test->log_message_count = 0;
    free(test->log_messages);
    test->log_messages = NULL;
}

typedef struct ctest_test_suit {
    size_t test_count;
    ctest_test** tests;
} ctest_test_suit;

ctest_test_suit* ctest_create_test_suit() {
    ctest_test_suit* suit = (ctest_test_suit*)calloc(1, sizeof(ctest_test_suit));
    return suit;
}

void ctest_test_suit_free(ctest_test_suit* suit) {
    assert(suit);
    for (size_t i = 0; i < suit->test_count; i++) {
        ctest_test_free(suit->tests[i]);
    }
    free(suit);
}

void ctest_test_suit_add(ctest_test_suit* suit, char* name, ctest_test_func f) {
    assert(f);
    suit->test_count++;
    suit->tests = realloc(suit->tests, sizeof(suit->tests[0]) * suit->test_count);
    if (!suit->tests)
        ERROR_ABORT("realloc");
    ctest_test* test = ctest_test_create();
    if (!test)
        ERROR_ABORT("ctest_test_create");
    suit->tests[suit->test_count - 1] = test;
    test->name = name;
    test->f = f;
    test->failed = false;
}

bool ctest_test_suit_run(ctest_test_suit* suit, CTEST_FLAG flags) {
    time_spec start;
    get_time(&start);

    for (size_t i = 0; i < suit->test_count; i++) {
        ctest_test* test = suit->tests[i];
        printf("=== RUN\t%s\n", test->name);
        test->f(test, flags & CTEST_FLAG_VERBOSE);
        if (test->failed) {
            printf("--- FAIL: %s\n", test->name);
        } else {
            printf("--- PASS: %s\n", test->name);
        }
    }
    size_t failure_count = 0;
    for (size_t i = 0; i < suit->test_count; i++) {
        if (suit->tests[i]->failed) {
            failure_count++;
        }
    }
    if (failure_count) {
        printf("FAIL");
    } else {
        printf("PASS");
    }

    time_spec end;
    get_time(&end);

    char buf[255] = {0};
    duration_format_nsec(time_sub_nsec(&end, &start), buf, sizeof(buf) / sizeof(buf[0]));
    printf("\t%s\n", buf);
    return failure_count == 0;
}
