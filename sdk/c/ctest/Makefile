CFLAGS = -Werror -DTEST_CTEST
CFLAGS_ASAN = $(CFLAGS) -fsanitize=address,leak,undefined
CFLAGS_LKDBG = $(CFLAGS) -DENABLE_LKDBG

$(info $(CFLAGS_ASAN))

ifeq ($(OS),Windows_NT)
    EXE_SUFFIX := .exe
else
    EXE_SUFFIX :=
endif

TARGET_BASE = test_ctest
TARGET = $(TARGET_BASE)$(EXE_SUFFIX)
TARGET_ASAN = $(TARGET_BASE)_asan$(EXE_SUFFIX)
TARGET_LKDBG = $(TARGET_BASE)_lkdbg$(EXE_SUFFIX)

SOURCES := ctest.c

# test ctest
test: $(TARGET)
	./$(TARGET)

# test ctest with AddressSanitizer
test_asan: $(TARGET_ASAN)
	./$(TARGET_ASAN)

# test ctest with lkdbg
test_lkdbg: $(TARGET_LKDBG)
	./$(TARGET_LKDBG)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

$(TARGET_ASAN): $(SOURCES)
	$(CC) $(CFLAGS_ASAN) -o $(TARGET_ASAN) $(SOURCES)

$(TARGET_LKDBG): $(SOURCES)
	$(CC) $(CFLAGS_LKDBG) -o $(TARGET_LKDBG) $(SOURCES)

clean:
	rm -f $(TARGET)
	rm -f $(TARGET_ASAN)
	rm -f $(TARGET_LKDBG)