#include "ctest.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
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

////////// mem_block //////////

typedef struct mem_block {
    size_t len;
    size_t cap;
    void* data;
} mem_block;

mem_block* mem_block_init(mem_block* mem) {
    memset(mem, 0, sizeof(*mem));
    return mem;
}

void mem_block_destroy(mem_block* mem) {
    free(mem->data);
}

mem_block* mem_block_create() {
    return calloc(1, sizeof(mem_block));
}

void mem_block_free(mem_block* mem) {
    mem_block_destroy(mem);
    free(mem);
}

size_t mem_block_len(mem_block* mem) {
    return mem->len;
}

size_t mem_block_cap(mem_block* mem) {
    return mem->cap;
}

void* mem_block_data(mem_block* mem) {
    return mem->data;
}

/**
 * Clears data of mem but keeps it's capacity.
 */
void mem_block_reset(mem_block* mem) {
    mem->len = 0;
}

void* mem_block_detach(mem_block* mem) {
    void* p = mem->data;
    mem->len = mem->cap = 0;
    mem->data = NULL;
    return p;
}

/**
 * Grows the block's capacity, if necessary, to guarantee space for another n bytes.
 * After mem_block_grow(mem, n), at least n bytes can be written to mem without another allocation.
 */
void mem_block_grow(mem_block* mem, size_t n) {
    size_t cap = mem->len + n;
    if (mem->cap < cap) {
        mem->cap = cap < 1024 ? cap * 2 : cap * 3 / 2;
        mem->data = realloc(mem->data, mem->cap);
        if (!mem->data) ERROR_ABORT_MSG("mem_block_ensure_cap", "realloc");
    }
}

void mem_block_append(mem_block* mem, void* src, size_t src_size) {
    if (src == NULL || src_size == 0) {
        return;
    }
    mem_block_grow(mem, src_size);
    memcpy((uint8_t*)mem->data + mem->len, src, src_size);
    mem->len += src_size;
}

/**
 * Appends n random bytes to mem.
 * Returns the newly appended memory region.
 */
void* mem_block_expand(mem_block* mem, size_t n) {
    mem_block_grow(mem, n);
    void* p = (uint8_t*)mem->data + mem->len;
    mem->len += n;
    return p;
}

/**
 * Deletes a memory block segment starting from start(inclusive)
 * with a length of len bytes.
 *
 * If len < 0, the segment expands to the end.
 */
void mem_block_delete(mem_block* mem, size_t start, int len) {
    if (start >= mem->len)
        ERROR_ABORT_MSG("mem_block_delete", "index out of range");
    size_t del_len = len < 0 ? mem->len - start : len;
    size_t move_start = start + del_len;
    if (move_start > mem->len)
        ERROR_ABORT_MSG("mem_block_delete", "index out of range");
    memmove((uint8_t*)mem->data + start, (uint8_t*)mem->data + move_start, mem->len - move_start);
    mem->len -= del_len;
}

#define mem_block_append_t(mem, type, value)        \
    {                                               \
        type temp = value;                          \
        mem_block_append(mem, &temp, sizeof(temp)); \
    }

/**
 * See mem_block_append_sprintf for details.
 */
void mem_block_append_vsprintf(mem_block* mem, bool include0, const char* format, va_list args) {
    va_list args2;
    va_copy(args2, args);

    int n = vsnprintf(NULL, 0, format, args);
    if (n < 0)
        ERROR_ABORT_MSG("mem_block_snprintf", "vsnprintf");
    if (!include0 && n == 0) {
        va_end(args2);
        return;
    }

    mem_block_grow(mem, n + 1);
    n = vsnprintf((char*)mem->data + mem->len, n + 1, format, args2);
    if (n < 0)
        ERROR_ABORT_MSG("mem_block_snprintf", "vsnprintf");
    mem->len += (n + (include0 ? 1 : 0));
    va_end(args2);
}

/**
 * Appends the result of formatted string to mem.
 * If include0 is true, the terminating zero of formatted string is included.
 */
void mem_block_append_sprintf(mem_block* mem, bool include0, const char* format, ...) {
    va_list args;
    va_start(args, format);
    mem_block_append_vsprintf(mem, include0, format, args);
    va_end(args);
}

#define mem_block_array_size_t(mem, type) ((mem)->len / sizeof(type))

void* mem_block_array_index(mem_block* mem, size_t elem_size, size_t i) {
    if (i >= mem->len / elem_size)
        ERROR_ABORT_MSG("mem_block_array_index", "index out of range");
    return (uint8_t*)mem->data + elem_size * i;
}

#define mem_block_array_index_t(mem, type, i) (*(type*)mem_block_array_index((mem), sizeof(type), (i)))

////////// time //////////

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>

typedef LARGE_INTEGER time_spec;

// The frequency of the performance counter is fixed at system boot
// and is consistent across all processors.
// Therefore, the frequency need only be queried upon application
// initialization, and the result can be cached.
static LARGE_INTEGER performance_freq = {0};

void get_time(time_spec* t) {
    if (performance_freq.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&performance_freq)) {
            char msg_buf[64] = {0};
            snprintf(msg_buf, sizeof(msg_buf) / sizeof(msg_buf[0]), "Last Error=0x%lx", GetLastError());
            ERROR_ABORT_MSG("QueryPerformanceFrequency", msg_buf);
        }
    }

    if (!QueryPerformanceCounter(t)) {
        char msg_buf[64] = {0};
        snprintf(msg_buf, sizeof(msg_buf) / sizeof(msg_buf[0]), "Last Error=0x%lx", GetLastError());
        ERROR_ABORT_MSG("QueryPerformanceFrequency", msg_buf);
    }
}

/**
 * Calculates the duration of t1 - t2, in nanoseconds.
 */
int64_t time_sub_nsec(time_spec* t1, time_spec* t2) {
    return (t1->QuadPart - t2->QuadPart) * 1000000000 / performance_freq.QuadPart;
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
int64_t time_sub_nsec(time_spec* t1, time_spec* t2) {
    int64_t result = 0;
    if ((t1->tv_nsec - t2->tv_nsec) < 0) {
        result = ((int64_t)(t1->tv_sec) - 1 - (t2->tv_sec)) * 1000000000;
        result += (int64_t)(1000000000 + t1->tv_nsec - t2->tv_nsec);
    } else {
        result = ((int64_t)(t1->tv_sec) - (t2->tv_sec)) * 1000000000;
        result += (int64_t)(t1->tv_nsec - t2->tv_nsec);
    }
    return result;
}

#endif

/**
 * Format nanoseconds duration d.
 */
int time_format_nsec(int64_t d, char* buf, size_t buf_size) {
    if (d < 1000) {  // 1000ns
        return snprintf(buf, buf_size, "%dns", (int)d);
    } else if (d < 100000000) {  // 0.1s
        return snprintf(buf, buf_size, "%0.3fms", (double)d / 1000000);
    } else {
        return snprintf(buf, buf_size, "%0.3fs", (double)d / 1000000000);
    }
}

////////// ctest encoders //////////

static void* text_encoder_on_test_suit_begin(ctest_printer print, void* printer_cookie,
                                             const char* name, size_t test_count) {
    print(printer_cookie, "*** %s\n", name);
    return NULL;
}

static void text_encoder_on_test_suit_end(ctest_printer print, void* printer_cookie,
                                          const char* name, void* suit_cookie,
                                          size_t test_count, size_t failed_count, int64_t duration) {
    char duration_str[64] = {0};
    time_format_nsec(duration, duration_str, sizeof(duration_str) / sizeof(duration_str[0]));
    print(printer_cookie, "%s\t%s %s\n", failed_count > 0 ? "FAIL" : "PASS", name, duration_str);
}

static void* text_encoder_on_test_begin(ctest_printer print, void* printer_cookie,
                                        const char* name, size_t count, size_t index) {
    print(printer_cookie, "    === RUN   %s\n", name);
    return NULL;
}

static void text_encoder_on_test_end(ctest_printer print, void* printer_cookie,
                                     const char* name, void* test_cookie,
                                     size_t count, size_t index, bool failed) {
    if (failed) {
        print(printer_cookie, "    --- FAIL: %s\n", name);
    } else {
        print(printer_cookie, "    --- PASS: %s\n", name);
    }
}

void static text_encoder_on_log_message(ctest_printer print, void* printer_cookie,
                                        const char* test_name, void* test_cookie,
                                        const char* file, int line, const char* message) {
    size_t msg_len = strlen(message);
    print(printer_cookie, "        %s:%d: %s%s",
          file, line,
          message, msg_len > 0 && message[msg_len - 1] == '\n' ? "" : "\n");
}

ctest_options* ctest_options_set_text_encoder(ctest_options* options) {
    options->encoder.on_test_suit_begin = text_encoder_on_test_suit_begin;
    options->encoder.on_test_suit_end = text_encoder_on_test_suit_end;
    options->encoder.on_test_begin = text_encoder_on_test_begin;
    options->encoder.on_test_end = text_encoder_on_test_end;
    options->encoder.on_log_message = text_encoder_on_log_message;
    return options;
}

static char* escape_json_string(mem_block* mem, const char* str) {
    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '"':
                mem_block_append(mem, "\\\"", 2);
                break;
            case '\\':
                mem_block_append(mem, "\\\\", 2);
                break;
            case '/':
                mem_block_append(mem, "\\/", 2);
                break;
            case '\r':
                mem_block_append(mem, "\\r", 2);
                break;
            case '\n':
                mem_block_append(mem, "\\n", 2);
                break;
            default:
                mem_block_append_t(mem, char, *p);
        }
    }
    mem_block_append_t(mem, char, 0);  // terminating zero.
    return (char*)mem->data;
}

static void* json_encoder_on_test_suit_begin(ctest_printer print, void* printer_cookie,
                                             const char* name, size_t test_count) {
    mem_block escape_buf;
    mem_block_init(&escape_buf);
    print(printer_cookie, "{\"name\":\"%s\"%s", name ? escape_json_string(&escape_buf, name) : "NONAME", test_count ? ",\"tests\":[" : "");
    mem_block_destroy(&escape_buf);
    return NULL;
}

static void json_encoder_on_test_suit_end(ctest_printer print, void* printer_cookie,
                                          const char* name, void* suit_cookie,
                                          size_t test_count, size_t failed_count, int64_t duration) {
    print(printer_cookie,
          "%s,\"failed_count\":%d, \"duration\":%" PRId64 "}\n",
          test_count ? "]" : "", failed_count, duration);
}

// Test cookie used by JSON encoder.
typedef struct json_encoder_test_cookie {
    bool has_log_message;
    mem_block escape_buf;
} json_encoder_test_cookie;

static json_encoder_test_cookie* json_encoder_test_cookie_create() {
    json_encoder_test_cookie* cookie = calloc(1, sizeof(json_encoder_test_cookie));
    mem_block_init(&cookie->escape_buf);
    return cookie;
}

static void json_encoder_test_cookie_free(json_encoder_test_cookie* cookie) {
    mem_block_destroy(&cookie->escape_buf);
    free(cookie);
}

static void* json_encoder_on_test_begin(ctest_printer print, void* printer_cookie, const char* name, size_t test_count, size_t index) {
    json_encoder_test_cookie* cookie = json_encoder_test_cookie_create();
    print(printer_cookie, "%s{\"name\":\"%s\"", index ? "," : "", escape_json_string(&cookie->escape_buf, name));
    return cookie;
}

static void json_encoder_on_test_end(ctest_printer print, void* printer_cookie, const char* name, void* test, size_t test_count, size_t index, bool failed) {
    print(printer_cookie, "%s,\"pass\":%s}",
          ((json_encoder_test_cookie*)test)->has_log_message ? "]" : "",
          failed ? "false" : "true");
    json_encoder_test_cookie_free(test);
}

static void json_encoder_on_log_message(ctest_printer print, void* printer_cookie, const char* test_name, void* test, const char* file, int line, const char* message) {
    json_encoder_test_cookie* cookie = test;
    bool* has_log_message = &cookie->has_log_message;
    mem_block_reset(&cookie->escape_buf);
    char* file_str = strdup(escape_json_string(&cookie->escape_buf, file));
    mem_block_reset(&cookie->escape_buf);
    print(printer_cookie, ",%s{\"file\":\"%s\",\"line\":%d,\"message\":\"%s\"}",
          *has_log_message ? "" : "\"log\":[",
          file_str, line,
          escape_json_string(&cookie->escape_buf, message));
    free(file_str);
    *has_log_message = true;
}

ctest_options* ctest_options_set_json_encoder(ctest_options* options) {
    options->encoder.on_test_suit_begin = json_encoder_on_test_suit_begin;
    options->encoder.on_test_suit_end = json_encoder_on_test_suit_end;
    options->encoder.on_test_begin = json_encoder_on_test_begin;
    options->encoder.on_test_end = json_encoder_on_test_end;
    options->encoder.on_log_message = json_encoder_on_log_message;
    return options;
}

////////// printers //////////

static void console_printer(void* cookie, const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (vprintf(format, args) < 0)
        ERROR_ABORT_MSG("console_print", "vprintf");
    va_end(args);
}

void ctest_set_console_printer(ctest_options* options) {
    options->printer = console_printer;
    options->printer_cookie = NULL;
}

typedef struct string_printer {
    mem_block mem;
    // DO NOT ADD MORE MEMBERS!
    // SEE string_printer_free.
} string_printer;

static string_printer* string_printer_create() {
    string_printer* printer = calloc(1, sizeof(string_printer));
    mem_block_init(&printer->mem);
    return printer;
}

void string_printer_free(string_printer* printer) {
    mem_block_free((mem_block*)printer);
}

char* string_printer_str(string_printer* printer) {
    mem_block_append_t((mem_block*)printer, char, 0);
    return (char*)mem_block_data(&printer->mem);
}

static void string_printer_(void* cookie, const char* format, ...) {
    string_printer* mem = cookie;
    va_list args;
    va_start(args, format);
    mem_block_append_vsprintf(&mem->mem, false, format, args);
    va_end(args);
}

string_printer* ctest_options_create_string_printer(ctest_options* options) {
    string_printer* printer = string_printer_create();
    options->printer_cookie = printer;
    options->printer = string_printer_;
    return printer;
}

////////// ctest_test //////////

typedef struct log_message {
    const char* file;
    int line;
    char* message;
} log_message;

static void log_message_init(log_message* msg, const char* file, int line, char* message) {
    msg->file = file;
    msg->line = line;
    msg->message = message;
}

static void log_message_destroy(log_message* msg) {
    free(msg->message);
}

typedef struct ctest_test {
    const char* name;  // name of test. WILL NOT FREE.
    ctest_test_func f;
    bool failed;
    mem_block log_messages;
    void* cookie;
} ctest_test;

static ctest_test* ctest_test_create() {
    ctest_test* test = calloc(1, sizeof(ctest_test));
    mem_block_init(&test->log_messages);
    return test;
}

static void ctest_test_free(ctest_test* test) {
    for (size_t i = 0; i < mem_block_array_size_t(&test->log_messages, log_message); i++) {
        log_message_destroy(&mem_block_array_index_t(&test->log_messages, log_message, i));
    }
    free(test);
}

void ctest_test_log(ctest_test* test, ctest_options* options, const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    mem_block mem;
    mem_block_init(&mem);
    mem_block_append_vsprintf(&mem, true, format, args);
    char* message = mem_block_detach(&mem);
    if (options->verbose || test->failed) {
        options->encoder.on_log_message(options->printer, options->printer_cookie,
                                        test->name, test->cookie,
                                        file, line, message);
        free(message);
    } else {
        // save it.
        log_message_init(mem_block_expand(&test->log_messages, sizeof(log_message)), file, line, message);
    }
    va_end(args);
}

void ctest_test_fail(ctest_test* test, ctest_options* options) {
    test->failed = true;
    // Print accumulated log messages.
    for (size_t i = 0; i < mem_block_array_size_t(&test->log_messages, log_message); i++) {
        log_message* msg = &mem_block_array_index_t(&test->log_messages, log_message, i);
        options->encoder.on_log_message(options->printer, options->printer_cookie,
                                        test->name, test->cookie,
                                        msg->file, msg->line, msg->message);
        log_message_destroy(msg);
    }
    mem_block_reset(&test->log_messages);
}

////////// ctest_test_suit //////////

typedef struct ctest_test_suit {
    const char* name;
    size_t test_count;
    ctest_test** tests;
    void* cookie;
} ctest_test_suit;

ctest_test_suit* ctest_test_suit_create(const char* name) {
    ctest_test_suit* suit = (ctest_test_suit*)calloc(1, sizeof(ctest_test_suit));
    suit->name = name;
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

bool ctest_test_suit_run(ctest_test_suit* suit, ctest_options* options) {
    if (!options->printer) {
        options->printer = console_printer;
        options->printer_cookie = NULL;
    }

    if (options->encoder.on_test_suit_begin) {
        suit->cookie = options->encoder.on_test_suit_begin(options->printer, options->printer_cookie, suit->name, suit->test_count);
    }

    time_spec start;
    get_time(&start);

    for (size_t i = 0; i < suit->test_count; i++) {
        ctest_test* test = suit->tests[i];
        if (options->encoder.on_test_begin) {
            test->cookie = options->encoder.on_test_begin(options->printer, options->printer_cookie,
                                                          test->name,
                                                          suit->test_count, i);
        }
        test->f(test, options);
        if (options->encoder.on_test_end) {
            options->encoder.on_test_end(options->printer, options->printer_cookie,
                                         test->name, test->cookie,
                                         suit->test_count, i,
                                         test->failed);
        }
    }
    time_spec end;
    get_time(&end);

    size_t failure_count = 0;
    for (size_t i = 0; i < suit->test_count; i++) {
        if (suit->tests[i]->failed) {
            failure_count++;
        }
    }

    if (options->encoder.on_test_suit_end) {
        options->encoder.on_test_suit_end(options->printer, options->printer_cookie,
                                          suit->name, suit->cookie,
                                          suit->test_count, failure_count,
                                          time_sub_nsec(&end, &start));
    }
    return failure_count == 0;
}

////////// Test ctest itself //////////
#ifdef TEST_CTEST

CTEST_TEST_FUNC(test_mem_block_create) {
    mem_block* mem = mem_block_create();
    uint8_t zero_block[sizeof(mem_block)] = {0};
    if (memcmp(mem, zero_block, sizeof(mem_block)) != 0) {
        CTEST_FAIL();
    }
    mem_block_free(mem);
}

CTEST_TEST_FUNC(test_mem_block_init) {
    mem_block mem;
    mem_block_init(&mem);
    uint8_t zero_block[sizeof(mem_block)] = {0};
    if (memcmp(&mem, zero_block, sizeof(mem_block)) != 0) {
        CTEST_FAIL();
    }
    mem_block_destroy(&mem);
}

CTEST_TEST_FUNC(test_mem_block_append) {
    mem_block* mem = mem_block_create();
    mem_block_append(mem, "abc", 3);

    size_t len = mem_block_len(mem);
    if (len != 3) {
        CTEST_FATALF("want %d, got %d", 3, len);
    }

    size_t cap = mem_block_cap(mem);
    if (cap != 6) {
        CTEST_FATALF("want %d, got %d", 6, cap);
    }

    char c0 = mem_block_array_index_t(mem, char, 0);
    if (c0 != 'a') {
        CTEST_FATALF("want %c, got %c", 'a', c0);
    }
    char c1 = mem_block_array_index_t(mem, char, 1);
    if (c1 != 'b') {
        CTEST_FATALF("want %c, got %c", 'b', c1);
    }
    char c2 = mem_block_array_index_t(mem, char, 2);
    if (c2 != 'c') {
        CTEST_FATALF("want %c, got %c", 'c', c2);
    }
    mem_block_free(mem);
}

CTEST_TEST_FUNC(test_mem_block_sprintf) {
    mem_block* mem = mem_block_create();
    mem_block_append_t(mem, char, '-');
    mem_block_append_sprintf(mem, false, "%s%d", "abc", 123);

    size_t len = mem_block_len(mem);
    if (len != 7) {
        CTEST_FATALF("want %d, got %d", 7, len);
    }

    size_t cap = mem_block_cap(mem);
    if (cap != 16) {
        CTEST_FATALF("want %d, got %d", 16, cap);
    }
    mem_block_append_t(mem, char, 0);  // makes it zero-terminated.
    const char* str = mem_block_data(mem);
    if (strcmp(str, "-abc123") != 0) {
        CTEST_FATALF("want %s, got %s", "-abc123", str);
    }

    mem_block_delete(mem, mem_block_len(mem) - 1, 1);
    mem_block_append_sprintf(mem, true, "|");
    len = mem_block_len(mem);
    if (len != 9) {
        CTEST_FATALF("want %d, got %d", 9, len);
    }
    str = mem_block_data(mem);
    if (strcmp(str, "-abc123|") != 0) {
        CTEST_FATALF("want %s, got %s", "-abc123", str);
    }

    mem_block_free(mem);
}

CTEST_TEST_FUNC(test_mem_block_delete) {
    mem_block* mem = mem_block_create();
    mem_block_append(mem, "0123456789", 10);
    mem_block_delete(mem, 8, 2);
    size_t len = mem_block_len(mem);
    if (len != 8) {
        CTEST_FATALF("want %d, got %d", 8, len);
    }
    if (memcmp(mem_block_data(mem), "01234567", len) != 0) {
        CTEST_FAIL();
    }

    mem_block_delete(mem, 0, 1);
    len = mem_block_len(mem);
    if (len != 7) {
        CTEST_FATALF("want %d, got %d", 7, len);
    }
    if (memcmp(mem_block_data(mem), "1234567", len) != 0) {
        CTEST_FAIL();
    }

    mem_block_delete(mem, 1, 2);
    len = mem_block_len(mem);
    if (len != 5) {
        CTEST_FATALF("want %d, got %d", 5, len);
    }
    if (memcmp(mem_block_data(mem), "14567", len) != 0) {
        CTEST_FAIL();
    }

    mem_block_delete(mem, 0, -1);
    len = mem_block_len(mem);
    if (len != 0) {
        CTEST_FATALF("want %d, got %d", 0, len);
    }
}

CTEST_TEST_FUNC(test_log) {
    CTEST_LOGF("1+1=%d", 1 + 1);
    CTEST_LOGF("line%d: abc\nline%d:def\t.", 1, 2);
}

int main() {
    ctest_test_suit* suit = ctest_test_suit_create("a/b");
    CTEST_TEST_SUIT_ADD(suit, test_mem_block_create);
    CTEST_TEST_SUIT_ADD(suit, test_mem_block_init);
    CTEST_TEST_SUIT_ADD(suit, test_mem_block_append);
    CTEST_TEST_SUIT_ADD(suit, test_mem_block_sprintf);
    CTEST_TEST_SUIT_ADD(suit, test_mem_block_delete);
    CTEST_TEST_SUIT_ADD(suit, test_log);

    ctest_options options = {0};
    options.verbose = true;
    ctest_options_set_text_encoder(&options);
    ctest_test_suit_run(suit, &options);

    ctest_options_set_json_encoder(&options);
    string_printer* printer = ctest_options_create_string_printer(&options);
    ctest_test_suit_run(suit, &options);

    printf("JSON OUTPUT:\n%s\n", string_printer_str(printer));
    string_printer_free(printer);
    return 0;
}

#endif