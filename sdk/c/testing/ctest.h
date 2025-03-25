#ifndef CTEST_CTEST_H
#define CTEST_CTEST_H

#include <stdbool.h>

typedef enum CTEST_FLAG {
    // Print log messages.
    CTEST_FLAG_VERBOSE = 1,
} CTEST_FLAG;

typedef struct ctest_test_suit ctest_test_suit;
typedef struct ctest_test ctest_test;

typedef void (*ctest_test_func)(ctest_test* test, bool verbose);

ctest_test_suit* ctest_create_test_suit();
void ctest_test_suit_free(ctest_test_suit* suit);
void ctest_test_suit_add(ctest_test_suit* suit, char* name, ctest_test_func f);
/**
 * Runs all the tests in suit.
 * 
 * Returns whether all the tests pass.
 */
bool ctest_test_suit_run(ctest_test_suit* suit, CTEST_FLAG flags);

void ctest_test_log(ctest_test* test, bool verbose, const char* file, int line, const char* format, ...);
void ctest_test_fail(ctest_test* test);
void ctest_test_fatal(ctest_test* test, bool verbose, const char* file, int line, const char* format, ...);

/**
 * Declares a testing function.
 *
 * CTEST_TEST_FUNC(test_func1) {
 *    // Test code here.
 * }
 */
#define CTEST_TEST_FUNC(NAME) void NAME(ctest_test* test_, bool verbose_)
/**
 * Formats arguments using default formatting, analogous to printf, and records the text in the error log.
 * The text will be printed only if the test fails or CTEST_FLAG_VERBOSE is set.
 */
#define CTEST_LOGF(fmt, ...) ctest_test_log(test_, verbose_, __FILE__, __LINE__, fmt, ##__VA_ARGS__);
/**
 * Marks the test as having failed but continues execution.
 */
#define CTEST_FAIL() ctest_test_fail(test_);
/**
 * Marks the function as having failed and stops its execution using a return statement.
 */
#define CTEST_FAIL_NOW()        \
    {                           \
        ctest_test_fail(test_); \
        return;                 \
    }
/**
 * Equivalent to CTEST_LOG() followed by CTEST_FAIL_NOW().
 */
#define CTEST_FATALF(fmt, ...)          \
    {                                   \
        CTEST_LOGF(fmt, ##__VA_ARGS__); \
        CTEST_FAIL_NOW();               \
    }

/**
 * Calls ctest_test_suit_add with the stringified f as test name.
 */
#define CTEST_TEST_SUIT_ADD(suit, f) ctest_test_suit_add(suit, #f, f)

#endif