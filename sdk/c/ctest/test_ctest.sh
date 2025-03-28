set -e
# $CC --analyzer -Werror -g -DENABLE_LKDBG -DTEST_CTEST ctest.c
export ASAN_OPTIONS="detect_leaks=1"
$CC -Werror -fsanitize=address,leak,undefined -g -DTEST_CTEST -DENABLE_LKDBG -o a.out ctest.c && ./a.out

# USAGE: export CC="/path/to/gcc"; ./test_ctest.sh
