CFLAGS = -Wall -Werror -DTEST_CTEST -g
CFLAGS_ASAN = $(CFLAGS) -fsanitize=address,leak,undefined
CFLAGS_LKDBG = $(CFLAGS) -DENABLE_LKDBG
CFLAGS_LKDBG_ASAN = $(CFLAGS) -DENABLE_LKDBG -fsanitize=address,leak,undefined

ifeq ($(OS),Windows_NT)
    EXE_SUFFIX := .exe
else
    EXE_SUFFIX :=
endif

TEMP_DIR = ./temp
TARGET_BASE = $(TEMP_DIR)/test_ctest
TARGET = $(TARGET_BASE)$(EXE_SUFFIX)
TARGET_ASAN = $(TARGET_BASE)_asan$(EXE_SUFFIX)
TARGET_LKDBG = $(TARGET_BASE)_lkdbg$(EXE_SUFFIX)
TARGET_LKDBG_ASAN = $(TARGET_BASE)_lkdbg_asan$(EXE_SUFFIX)

$(info $(TARGET_LKDBG_ASAN))

SOURCES := ctest.c

all: test test_asan test_lkdbg test_lkdbg_asan

$(TEMP_DIR):
	mkdir -p $@

# test ctest
test: $(TEMP_DIR) $(TARGET)
	./$(TARGET)

# test ctest with AddressSanitizer
test_asan: $(TEMP_DIR) $(TARGET_ASAN)
	export ASAN_OPTIONS="detect_leaks=1" && ./$(TARGET_ASAN)

# test ctest with lkdbg
test_lkdbg: $(TEMP_DIR) $(TARGET_LKDBG)
	./$(TARGET_LKDBG)

# test ctest with lkdbg and AddressSanitizer
test_lkdbg_asan: $(TEMP_DIR) $(TARGET_LKDBG_ASAN)
	export ASAN_OPTIONS="detect_leaks=1" && ./$(TARGET_LKDBG_ASAN)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

$(TARGET_ASAN): $(SOURCES)
	$(CC) $(CFLAGS_ASAN) -o $(TARGET_ASAN) $(SOURCES)

$(TARGET_LKDBG): $(SOURCES)
	$(CC) $(CFLAGS_LKDBG) -o $(TARGET_LKDBG) $(SOURCES)

$(TARGET_LKDBG_ASAN): $(SOURCES)
	$(CC) $(CFLAGS_LKDBG_ASAN) -o $(TARGET_LKDBG_ASAN) $(SOURCES)

clean:
	rm -rf $(TEMP_DIR)