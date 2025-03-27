#include <stdint.h>

#include "ctest.h"

CTEST_TEST_FUNC(test_fail) {
    CTEST_LOGF("log message");
    CTEST_FAIL();
    CTEST_LOGF("after fail\n");
}

CTEST_TEST_FUNC(test_pass) {
    CTEST_LOGF("1+2 = %d", 1 + 2);
}

#if defined(_WIN32) || defined(_WIN64)

#include <Windows.h>

typedef LARGE_INTEGER time_spec;

void sleep_ms(uint32_t ms) {
    Sleep(ms);
}

#else

#include <time.h>
#include <unistd.h>

typedef struct timespec time_spec;

void sleep_ms(uint32_t ms) {
    usleep(ms * 1000);
}
#endif

typedef int64_t duration;

void get_time(time_spec* t);
duration time_sub_nsec(time_spec* t1, time_spec* t2);

CTEST_TEST_FUNC(test_get_time) {
    time_spec start;
    get_time(&start);
    sleep_ms(1000);
    time_spec end;
    get_time(&end);
    duration d = time_sub_nsec(&end, &start);
    if (d < 1000000000 || d > 1050000000) {
        CTEST_FATALF("got %ld, want [1000000000, 1050000000]", (long)d);
    }
}

int main() {
    ctest_test_suit* suit = ctest_test_suit_create();
    CTEST_TEST_SUIT_ADD(suit, test_fail);
    CTEST_TEST_SUIT_ADD(suit, test_pass);
    CTEST_TEST_SUIT_ADD(suit, test_get_time);
    ctest_test_suit_run(suit, CTEST_FLAG_VERBOSE);

    ctest_test_suit_free(suit);

    return 0;
}