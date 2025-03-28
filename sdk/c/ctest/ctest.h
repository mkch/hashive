#ifndef CTEST_CTEST_H
#define CTEST_CTEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ctest_benchmark_data ctest_benchmark_data;

/**
 * A function to print formatted strings.
 * Argument cookie is the cookie set in ctest_options.
 */
typedef void (*ctest_printer)(void* cookie, const char* format, ...);

// Encoder to encode ctest output.
typedef struct ctest_output_encoder {
    /**
     * Called before running a test suit.
     *
     * Argument name is the name of current test suit.
     * Argument test_count is the count of tests in this test suit.
     *
     * Returns a cookie which will be passed to on_test_suit_end.
     */
    void* (*on_test_suit_begin)(ctest_printer print, void* printer_cookie, const char* name, size_t test_count);
    /**
     * Called after running a test suit.
     *
     * Argument failed_count is the count of failed tests. 0 if all tests passed.
     * Argument duration is the execution time of this test suit.
     * Argument suit_cookie is the cookie returned by on_test_suit_begin.
     *
     */
    void (*on_test_suit_end)(ctest_printer print, void* printer_cookie, const char* name, void* suit_cookie, size_t test_count, size_t failed_count, int64_t duration);
    /**
     * Called before running a test.
     *
     * Argument name is the name of current test.
     * Argument test_count is the count of tests in belonging test suit.
     * Argument index is the index of current test.
     *
     * Returns a cookie which will be passed to on_test_end and on_test_log_message.
     */
    void* (*on_test_begin)(ctest_printer print, void* printer_cookie, const char* name, size_t test_count, size_t index);
    /**
     * Called after running a test.
     *
     * Argument failed indicates whether the test has failed.
     */
    void (*on_test_end)(ctest_printer print, void* printer_cookie, const char* name, void* test_cookie, size_t count, size_t index, bool failed);
    /**
     * Called when the running test logs a message.
     * Argument file and line is the source code file location where the logging occurs.
     */
    void (*on_test_log_message)(ctest_printer print, void* printer_cookie, const char* test_name, void* test_cookie, const char* file, int line, const char* message);
    /**
     * Called before running any benchmarks.
     *
     * Argument benchmark_count is the count of benchmarks in belonging test suit.
     *
     * Returns a cookie which will be passed to on_teardown_benchmarks.
     */
    void* (*on_prepare_benchmarks)(ctest_printer print, void* printer_cookie, size_t benchmark_count);
    /**
     * Called after running all the benchmarks.
     *
     * Argument cookie is the cookie returned by on_prepare_benchmarks.
     *
     */
    void (*on_teardown_benchmarks)(ctest_printer print, void* printer_cookie, void* cookie, size_t benchmark_count);
    /**
     * Called before running a benchmark.
     *
     * Argument name is the name of current benchmark.
     * Argument benchmark_count is the count of benchmarks in belonging test suit.
     * Argument index is the index of currentbenchmark.
     *
     * Returns a cookie which will be passed to on_benchmark_end and on_benchmark_log_message.
     */
    void* (*on_benchmark_begin)(ctest_printer print, void* printer_cookie, const char* name, size_t benchmark_count, size_t index);
    /**
     * Called after running a benchmark.
     *
     */
    void (*on_benchmark_end)(ctest_printer print, void* printer_cookie, const char* name, void* benchmark_cookie, ctest_benchmark_data* data, size_t count, size_t index);
    /**
     * Called when the running benchmark logs a message.
     * Argument file and line is the source code file location where the logging occurs.
     */
    void (*on_benchmark_log_message)(ctest_printer print, void* printer_cookie, const char* benchmark_name, void* benchmark_cookie, const char* file, int line, const char* message);
} ctest_output_encoder;
typedef struct ctest_options {
    bool verbose;
    void* printer_cookie;
    ctest_printer printer;
    ctest_output_encoder encoder;
} ctest_options;

// Sets options to use text encoder.
ctest_options* ctest_options_set_text_encoder(ctest_options* options);
// Sets options to use JSON encoder.
ctest_options* ctest_options_set_json_encoder(ctest_options* options);

// Sets options to use console printer.
void ctest_set_console_printer(ctest_options* options);

// A printer accumulates printings to string.
typedef struct string_printer string_printer;
// Frees resources used by printer.
void string_printer_free(string_printer* printer);
// Retrieves the accumulated string in printer.
// After calling this function, the printer should not be used to print any more.
char* string_printer_str(string_printer* printer);
// Sets options to use string printer.
// The returned printer should be freed using string_printer_free.
string_printer* ctest_options_create_string_printer(ctest_options* options);

typedef struct ctest_test_suit ctest_test_suit;
typedef struct ctest_test_base ctest_test_base;
typedef struct ctest_benchmark_loop_args ctest_benchmark_loop_args;

typedef void (*ctest_test_func)(ctest_test_base* base, ctest_options* options);
typedef bool (*ctest_benchmark_loop_func)(ctest_benchmark_loop_args* cookie);
typedef void (*ctest_benchmark_func)(ctest_test_base* base, ctest_options* options, ctest_benchmark_loop_func loop, ctest_benchmark_loop_args* args);

ctest_test_suit* ctest_test_suit_create(const char* name);
void ctest_test_suit_free(ctest_test_suit* suit);
void ctest_test_suit_add_test(ctest_test_suit* suit, char* name, ctest_test_func f);
/**
 * Runs all the tests in suit.
 *
 * Returns whether all the tests pass.
 */
bool ctest_test_suit_run(ctest_test_suit* suit, ctest_options* options);

void ctest_test_base_log(ctest_test_base* base, ctest_options* options, const char* file, int line, const char* format, ...);
void ctest_test_base_fail(ctest_test_base* base, ctest_options* options);

/**
 * Declares a testing function.
 *
 * CTEST_TEST_FUNC(test_func1) {
 *    // Test code here.
 * }
 */
#define CTEST_TEST_FUNC(NAME) void NAME(ctest_test_base* base_, ctest_options* options_)
/**
 * Formats arguments using default formatting, analogous to printf, and records the text in the error log.
 * The text will be printed only if the test fails or ctest_options.verbose is set.
 */
#define CTEST_LOGF(fmt, ...) ctest_test_base_log(base_, options_, __FILE__, __LINE__, fmt, ##__VA_ARGS__);
/**
 * Marks the test as having failed but continues execution.
 */
#define CTEST_FAIL() ctest_test_base_fail(base_, options_);
/**
 * Equivalent to CTEST_LOG() followed by CTEST_FAIL().
 */
#define CTEST_FAILF(fmt, ...)           \
    {                                   \
        CTEST_LOGF(fmt, ##__VA_ARGS__); \
        CTEST_FAIL();                   \
    }
/**
 * Marks the function as having failed and stops its execution using a return statement.
 */
#define CTEST_FAIL_NOW()                       \
    {                                          \
        ctest_test_base_fail(base_, options_); \
        return;                                \
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
#define CTEST_TEST_SUIT_ADD_TEST(suit, f) ctest_test_suit_add_test(suit, #f, f)

/**
 * Declares a testing function.
 *
 * CTEST_BENCHMARK_FUNC(benchmark_func1) {
 *    // Test code here.
 * }
 */
#define CTEST_BENCHMARK_FUNC(NAME) void NAME(ctest_test_base* base_, ctest_options* options_, ctest_benchmark_loop_func loop_, ctest_benchmark_loop_args* args_)
#define CTEST_LOOP (loop_(args_))
#define CTEST_TEST_SUIT_ADD_BENCHMARK(suit, f) ctest_test_suit_add_benchmark(suit, #f, f)

#endif