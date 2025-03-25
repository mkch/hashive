#include "ctest.h"

CTEST_TEST_FUNC(test_fail) {
    CTEST_LOGF("log message");
    CTEST_FAIL();
    CTEST_LOGF("after fail\n");
}

CTEST_TEST_FUNC(test_pass) {
    CTEST_LOGF("1+2 = %d", 1 + 2);
}

int main() {
    ctest_test_suit* suit = ctest_create_test_suit();
    CTEST_TEST_SUIT_ADD(suit, test_fail);
    CTEST_TEST_SUIT_ADD(suit, test_pass);
    ctest_test_suit_run(suit, CTEST_FLAG_VERBOSE);

    ctest_test_suit_free(suit);

    return 0;
}