#define _GNU_SOURCE  // for 'dladdr'

#include "ctest.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define ENABLE_LKDBG and use lkdbg_report to report memory leaks.
#ifdef ENABLE_LKDBG
typedef struct lkdbg_alloc_record {
    const char* file;
    int line;
    void* p;
    size_t size;
} lkdbg_alloc_record;

typedef struct lkdbg_alloc_records {
    size_t len;
    size_t cap;
    lkdbg_alloc_record* records;
} lkdbg_alloc_records;

lkdbg_alloc_records* lkdbg_alloc_records_init(lkdbg_alloc_records* records) {
    records->len = 0;
    records->cap = 0;
    records->records = NULL;
    return records;
}

void lkdbg_alloc_records_destroy(lkdbg_alloc_records* records) {
    free(records->records);
    records->cap = records->len = 0;
    records->records = NULL;
}

void lkdbg_alloc_records_dump(lkdbg_alloc_records* records) {
    printf("[ALLOC RECORDS] %d ", (int)records->len);
    for (size_t i = 0; i < records->len; i++) {
        lkdbg_alloc_record* r = &records->records[i];
        printf("%d:{%s:%d %p %zu}, ", (int)i, r->file, r->line, r->p, r->size);
    }
    printf("\n");
}

// Binary search p in records.
// Returns the index of the first p in records, or the position where p should be inserted.
int lkdbg_alloc_records_binary_search_ceiling(lkdbg_alloc_records* records, void* p) {
    size_t i = 0;
    size_t j = records->len;
    while (i < j) {
        int h = (i + j) / 2;
        if (records->records[h].p < p) {
            i = h + 1;
        } else {
            j = h;
        }
    }

    return i;
}

void lkdbg_alloc_records_insert(lkdbg_alloc_records* records,
                                void* p, size_t size, const char* file, int line) {
    // binary search.
    int i = lkdbg_alloc_records_binary_search_ceiling(records, p);

    // grow
    size_t cap = records->len + 1;
    if (records->cap < cap) {
        records->cap = cap * 2;
        records->records = realloc(records->records, records->cap * sizeof(lkdbg_alloc_record));
        if (!records->records) {
            fprintf(stderr, "realloc failed!");
            abort();
        }
    }

    // insert
    memmove(records->records + i + 1, records->records + i, sizeof(lkdbg_alloc_record) * (records->len - i));
    lkdbg_alloc_record* r = records->records + i;
    r->file = file;
    r->line = line;
    r->p = p;
    r->size = size;
    records->len++;
}

/**
 * Return the index of the first p in records, or -(pos+1) where pos is
 * the position where p should be inserted.
 */
int lkdbg_alloc_records_find(lkdbg_alloc_records* records, void* p) {
    int i = lkdbg_alloc_records_binary_search_ceiling(records, p);
    return (i < records->len && records->records[i].p == p) ? i : -(i + 1);
}

void lkdbg_alloc_records_delete(lkdbg_alloc_records* records, int i) {
    memmove(records->records + i, records->records + i + 1, ((int)records->len - i - 1) * sizeof(lkdbg_alloc_record));
    records->len--;
}

static lkdbg_alloc_records lkdbg_records = {0};

void* lkdbg_malloc(size_t n, const char* file, int line) {
    void* p = malloc(n);
    if (p) {
        lkdbg_alloc_records_insert(&lkdbg_records, p, n, file, line);
    }
#ifdef DEBUG_LKDBG
    printf("lkdbg_malloc %zu %p\n", n, p);
    lkdbg_alloc_records_dump(&lkdbg_records);
#endif
    return p;
}

void* lkdbg_calloc(size_t count, size_t size, const char* file, int line) {
    void* p = calloc(count, size);
    if (p) {
        lkdbg_alloc_records_insert(&lkdbg_records, p, size, file, line);
    }
#ifdef DEBUG_LKDBG
    printf("lkdbg_calloc %" PRId64 " %zu %p\n", count, size, p);
    lkdbg_alloc_records_dump(&lkdbg_records);
#endif
    return p;
}

void* lkdbg_realloc(void* p, size_t size, const char* file, int line) {
    int i = -1;
    if (p) {
        i = lkdbg_alloc_records_find(&lkdbg_records, p);
        if (i < 0) {
            fprintf(stderr, "[LKDBG] %s:%d `realloc` an invalid pointer %p\n", file, line, p);
        }
    }
    void* p2 = realloc(p, size);
    if (!p2) {
        return p2;
    }
    if (p2 == p) {
        if (i >= 0) {
            lkdbg_alloc_record* record = &lkdbg_records.records[i];
            record->file = file;
            record->line = line;
        }
    } else {
        if (i >= 0) {
            lkdbg_alloc_records_delete(&lkdbg_records, i);
        }
        lkdbg_alloc_records_insert(&lkdbg_records, p2, size, file, line);
    }
#ifdef DEBUG_LKDBG
    printf("lkdbg_realloc %p %zu %p\n", p, (int)size, p2);
    lkdbg_alloc_records_dump(&lkdbg_records);
#endif
    return p2;
}

void lkdbg_free(void* p, const char* file, int line) {
    if (p) {
        int i = lkdbg_alloc_records_find(&lkdbg_records, p);
        if (i < 0) {
            fprintf(stderr, "[LKDBG] %s:%d `free` an invalid pointer %p\n", file, line, p);
        } else {
            lkdbg_alloc_records_delete(&lkdbg_records, i);
        }
#ifdef DEBUG_LKDBG
        printf("lkdbg_free %p\n", p);
        lkdbg_alloc_records_dump(&lkdbg_records);
#endif
    }
    free(p);
}

void lkdbg_report() {
    if (lkdbg_records.len == 0) {
        return;
    }
    for (size_t i = 0; i < lkdbg_records.len; i++) {
        lkdbg_alloc_record* record = &lkdbg_records.records[i];
        fprintf(stderr, "[LKDBG] memory leak: %p size %zu at %s:%d\n", record->p, record->size, record->file, record->line);
    }
    lkdbg_alloc_records_destroy(&lkdbg_records);
}

#define malloc(n) lkdbg_malloc((n), __FILE__, __LINE__)
#define calloc(n, size) lkdbg_calloc((n), (size), __FILE__, __LINE__)
#define realloc(p, n) lkdbg_realloc((p), (n), __FILE__, __LINE__)
#define free(p) lkdbg_free((p), __FILE__, __LINE__)

#endif

////////// get_section //////////
#ifdef _WIN32

#include <stdio.h>
#include <windows.h>

void* get_section(const char*, const char* sectname, size_t* out_sec_len) {
    HMODULE hModule = GetModuleHandle(NULL);
    if (hModule == NULL) {
        return NULL;
    }

    PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }

    PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hModule + pDosHeader->e_lfanew);
    if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return NULL;
    }

    PIMAGE_SECTION_HEADER pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
    for (WORD i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++) {
        if (strncmp((const char*)pSectionHeader[i].Name, sectname, 8) == 0) {
            if (out_sec_len) {
                *out_sec_len = pSectionHeader[i].Misc.VirtualSize;
            }
            return (void*)((BYTE*)hModule + pSectionHeader[i].VirtualAddress);
        }
    }

    return NULL;
}

#elif __APPLE__

#include <dlfcn.h>
#include <mach-o/dyld.h>

void* get_section(const char* segname, const char* sectname, size_t* out_len) {
    Dl_info info;
    if (!dladdr((void*)get_section, &info)) {
        return NULL;
    }

    intptr_t slide = _dyld_get_image_vmaddr_slide(0);

    const struct mach_header* header = (const struct mach_header*)info.dli_fbase;
    struct load_command* lc = (struct load_command*)((uintptr_t)header + sizeof(struct mach_header_64));

    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64* seg = (struct segment_command_64*)lc;
            if (strcmp(seg->segname, segname) == 0) {
                struct section_64* sect = (struct section_64*)(seg + 1);
                for (uint32_t j = 0; j < seg->nsects; j++) {
                    if (strcmp(sect[j].sectname, sectname) == 0) {
                        if (out_len) *out_len = sect[j].size;
                        return (void*)((uintptr_t)sect[j].addr + slide);
                    }
                }
            }
        }
        lc = (struct load_command*)((uintptr_t)lc + lc->cmdsize);
    }
    return NULL;
}

#else

#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void* get_section(const char* unused, const char* sectname, size_t* out_len) {
    Dl_info info;
    if (!dladdr(get_section, &info)) {
        return NULL;
    }

    int fd = open(info.dli_fname, O_RDONLY);
    if (fd == -1) {
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return NULL;
    }

    void* map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    // ELF Header
    // Do not use (ElfW(Ehdr) *)info.dli_fbase,
    // whose string table may be empty.
    ElfW(Ehdr)* ehdr = (ElfW(Ehdr)*)map;

    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        munmap(map, st.st_size);
        close(fd);
        return NULL;
    }

    // Section header table
    ElfW(Shdr)* shdr = (ElfW(Shdr)*)((char*)map + ehdr->e_shoff);

    // String table
    ElfW(Shdr)* sh_strtab = &shdr[ehdr->e_shstrndx];
    const char* sh_strtab_p = (char*)map + sh_strtab->sh_offset;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        const char* name = sh_strtab_p + shdr[i].sh_name;
        if (strcmp(name, sectname) == 0) {
            // Found
            void* addr = (void*)((uintptr_t)info.dli_fbase + shdr[i].sh_addr);
            *out_len = shdr[i].sh_size;

            munmap(map, st.st_size);
            close(fd);
            return addr;
        }
    }

    // Not found
    munmap(map, st.st_size);
    close(fd);
    return NULL;
}

#endif  // _WIN32

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

#define ERROR_ABORT_MSG(msg)                                                                        \
    {                                                                                               \
        fprintf(stderr, "[ERROR] %s:%d in function '%s': %s\n", __FILE__, __LINE__, __func__, msg); \
        abort();                                                                                    \
    }

#define ERROR_ABORT()                                                                    \
    {                                                                                    \
        fprintf(stderr, "[ERROR] %s:%d in function '%s'", __FILE__, __LINE__, __func__); \
        abort();                                                                         \
    }

#endif

////////// OS & CPU //////////
#ifdef _WIN32
#include <intrin.h>

static char* get_os_name(char* buf, size_t buf_len) {
    return strncpy(buf, "Windows", buf_len);
}

static char* get_cpu_brand_string(char* buf, size_t buf_len) {
    if (buf_len < 49)
        ERROR_ABORT_MSG("buf_len < 49");
    memset(buf, 0, 49);

    int regs[4];
    __cpuid(regs, 0x80000002);
    memcpy(buf, regs, sizeof(regs));
    __cpuid(regs, 0x80000003);
    memcpy(buf + 16, regs, sizeof(regs));
    __cpuid(regs, 0x80000004);
    memcpy(buf + 32, regs, sizeof(regs));

    return buf;
}

#else  // _WIN32

#ifdef __linux__

static char* get_cpu_brand_string(char* buf, size_t buf_len) {
    FILE* cpuinfo = NULL;
    char line[256];  // 用于读取 /proc/cpuinfo 的每一行
    const char* target_field = "model name";
    char* model_name_ptr = NULL;
    size_t model_name_len = 0;

    cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo == NULL) {
        return NULL;
    }

    while (fgets(line, sizeof(line), cpuinfo) != NULL) {
        if (strncmp(line, target_field, strlen(target_field)) == 0) {
            char* colon_ptr = strchr(line, ':');
            if (colon_ptr != NULL) {
                model_name_ptr = colon_ptr + 1;
                while (*model_name_ptr && (*model_name_ptr == ' ' || *model_name_ptr == '\t')) {
                    model_name_ptr++;
                }
                model_name_len = strlen(model_name_ptr);
                if (model_name_len > 0 && model_name_ptr[model_name_len - 1] == '\n') {
                    model_name_ptr[model_name_len - 1] = '\0';
                    model_name_len--;  // 更新长度
                }

                strncpy(buf, model_name_ptr, buf_len);
                fclose(cpuinfo);
                return buf;
            }
        }
    }

    fclose(cpuinfo);
    return NULL;
}

#else  // __linux__

#include <sys/sysctl.h>

static char* get_cpu_brand_string(char* buf, size_t buf_len) {
    if (sysctlbyname("machdep.cpu.brand_string", buf, &buf_len, NULL, 0) != 0)
        ERROR_ABORT_MSG("sysctlbyname");
    return buf;
}

#endif  // __linux__

#include <sys/utsname.h>

static char* get_os_name(char* buf, size_t buf_len) {
    struct utsname sys_info;
    if (uname(&sys_info) != 0)
        ERROR_ABORT_MSG("uname");
    strncpy(buf, sys_info.sysname, buf_len);
    return buf;
}

#endif  // _WIN32

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
mem_block* mem_block_reset(mem_block* mem) {
    mem->len = 0;
    return mem;
}

/**
 * Trims extra trailing capacity.
 */
mem_block* mem_block_trim(mem_block* mem) {
    if (mem->data) {
        mem->cap = mem->len;
        mem->data = realloc(mem->data, mem->len);
    }
    return mem;
}

/**
 * Detaches the memory block from mem and returns it.
 * After detaching, mem is reinitialized.
 */
void* mem_block_detach(mem_block* mem) {
    void* p = mem->data;
    mem->len = mem->cap = 0;
    mem->data = NULL;
    return p;
}

/**
 * Returns a pointer to a memory block, which is a duplicate of the content of m.
 * The returned pointer must be passed to free to avoid a memory leak.
 * If the length of memory is 0, NULL is returned.
 */
void* mem_block_dup(mem_block* mem) {
    if (mem->len == 0) {
        return NULL;
    }
    void* p = malloc(mem->len);
    if (!p)
        ERROR_ABORT_MSG("malloc");
    return memcpy(p, mem->data, mem->len);
}

/**
 * Grows the block's capacity, if necessary, to guarantee space for another n bytes.
 * After mem_block_grow(mem, n), at least n bytes can be written to mem without another allocation.
 */
mem_block* mem_block_grow(mem_block* mem, size_t n) {
    size_t cap = mem->len + n;
    if (mem->cap < cap) {
        mem->cap = cap < 1024 ? cap * 2 : cap * 3 / 2;
        mem->data = realloc(mem->data, mem->cap);
        if (!mem->data) ERROR_ABORT_MSG("realloc");
    }
    return mem;
}

/**
 * Appends src_size bytes from src to the end of mem.
 */
mem_block* mem_block_append(mem_block* mem, void* src, size_t src_size) {
    if (src == NULL || src_size == 0) {
        return mem;
    }
    mem_block_grow(mem, src_size);
    memcpy((uint8_t*)mem->data + mem->len, src, src_size);
    mem->len += src_size;
    return mem;
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
mem_block* mem_block_delete(mem_block* mem, size_t start, int len) {
    if (start >= mem->len)
        ERROR_ABORT_MSG("index out of range");
    size_t del_len = len < 0 ? mem->len - start : len;
    size_t move_start = start + del_len;
    if (move_start > mem->len)
        ERROR_ABORT_MSG("index out of range");
    memmove((uint8_t*)mem->data + start, (uint8_t*)mem->data + move_start, mem->len - move_start);
    mem->len -= del_len;
    return mem;
}

/**
 * Append a value of specified type to the end of mem.
 */
#define mem_block_expand_t(mem, type) (*(type*)mem_block_expand((mem), sizeof(type)))
#define mem_block_append_t(mem, type, value) ((*(type*)(mem_block_expand((mem), sizeof(type))) = (value)), (mem))

/**
 * See mem_block_append_sprintf for details.
 */
mem_block* mem_block_append_vsprintf(mem_block* mem, bool include0, const char* format, va_list args) {
    va_list args2;
    va_copy(args2, args);

    int n = vsnprintf(NULL, 0, format, args);
    if (n < 0)
        ERROR_ABORT_MSG("vsnprintf");
    if (!include0 && n == 0) {
        va_end(args2);
        return mem;
    }

    mem_block_grow(mem, n + 1);
    n = vsnprintf((char*)mem->data + mem->len, n + 1, format, args2);
    if (n < 0)
        ERROR_ABORT_MSG("vsnprintf");
    mem->len += (n + (include0 ? 1 : 0));
    va_end(args2);
    return mem;
}

/**
 * Appends the result of formatted string to mem.
 * If include0 is true, the terminating zero of formatted string is included.
 */
mem_block* mem_block_append_sprintf(mem_block* mem, bool include0, const char* format, ...) {
    va_list args;
    va_start(args, format);
    mem_block_append_vsprintf(mem, include0, format, args);
    va_end(args);
    return mem;
}

#define mem_block_array_size_t(mem, type) ((mem)->len / sizeof(type))

void* mem_block_array_index(mem_block* mem, size_t elem_size, size_t i) {
    if (i >= mem->len / elem_size)
        ERROR_ABORT_MSG("index out of range");
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

static void get_time(time_spec* t) {
    if (performance_freq.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&performance_freq)) {
            char msg_buf[64] = {0};
            snprintf(msg_buf, sizeof(msg_buf) / sizeof(msg_buf[0]), "Last Error=0x%lx", GetLastError());
            ERROR_ABORT_MSG("QueryPerformanceFrequency");
        }
    }

    if (!QueryPerformanceCounter(t)) {
        char msg_buf[64] = {0};
        snprintf(msg_buf, sizeof(msg_buf) / sizeof(msg_buf[0]), "Last Error=0x%lx", GetLastError());
        ERROR_ABORT_MSG("QueryPerformanceFrequency");
    }
}

/**
 * Calculates the duration of t1 - t2, in nanoseconds.
 */
static int64_t time_sub_nsec(time_spec* t1, time_spec* t2) {
    return (t1->QuadPart - t2->QuadPart) * 1000000000 / performance_freq.QuadPart;
}

#else

#include <time.h>
typedef struct timespec time_spec;

static void get_time(time_spec* t) {
    if (clock_gettime(CLOCK_MONOTONIC, t) != 0) {
        ERROR_ABORT_MSG(strerror(errno));
    }
}

/**
 * Calculates the duration of t1 - t2, in nanoseconds.
 */
static int64_t time_sub_nsec(time_spec* t1, time_spec* t2) {
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

typedef struct time_str {
    char number[8];
    const char* unit;
} time_str;
/**
 * Format nanoseconds duration d.
 */
static time_str time_format_nsec(int64_t d) {
    time_str str;
    if (d < 1000) {  // 1000ns
        snprintf(str.number, sizeof(str.number) / sizeof(str.number[0]), "%u", (uint16_t)d);
        str.unit = "ns";
    } else if (d < 100000000) {  // 0.1s
        snprintf(str.number, sizeof(str.number) / sizeof(str.number[0]), "%0.3f", (double)d / 1000000);
        str.unit = "ms";
    } else {
        snprintf(str.number, sizeof(str.number) / sizeof(str.number[0]), "%0.3f", (double)d / 1000000000);
        str.unit = "s";
    }
    return str;
}

typedef struct ctest_benchmark_data {
    int64_t ns;
    int64_t op;
} ctest_benchmark_data;

////////// ctest encoders //////////

// Return whether all the members of encoder is NULL.
// Please snyc with ctest_output_encoder.
static bool zero_encoder(ctest_output_encoder* encoder) {
    return !encoder->on_setup_test_suit &&
           !encoder->on_teardown_test_suit &&
           !encoder->on_setup_tests &&
           !encoder->on_teardown_tests &&
           !encoder->on_test_begin &&
           !encoder->on_test_end &&
           !encoder->on_test_log_message &&
           !encoder->on_setup_benchmarks &&
           !encoder->on_teardown_benchmarks &&
           !encoder->on_benchmark_begin &&
           !encoder->on_benchmark_end &&
           !encoder->on_benchmark_log_message;
}

static void* text_encoder_on_setup_test_suit(ctest_printer print, void* printer_cookie, const char* name) {
    print(printer_cookie, "*** %s ***\n", name);
    return NULL;
}

static void text_encoder_on_teardown_test_suit(ctest_printer print, void* printer_cookie,
                                               const char* name, void* suit_cookie,
                                               size_t failed_count, int64_t duration) {
    time_str duration_str = time_format_nsec(duration);
    print(printer_cookie, "%s\t%s %s%s\n", failed_count > 0 ? "FAIL" : "PASS", name, duration_str.number, duration_str.unit);
}

typedef struct text_encoder_cookie {
    bool has_log_message;
} text_encoder_cookie;

text_encoder_cookie* text_encoder_cookie_create() {
    text_encoder_cookie* cookie = malloc(sizeof(text_encoder_cookie));
    memset(cookie, 0, sizeof(text_encoder_cookie));
    return cookie;
}

void text_encoder_cookie_free(text_encoder_cookie* cookie) {
    free(cookie);
}

static void* text_encoder_on_test_begin(ctest_printer print, void* printer_cookie,
                                        const char* name, size_t count, size_t index) {
    print(printer_cookie, "=== RUN   %s\n", name);
    return text_encoder_cookie_create();
}

static void text_encoder_on_test_end(ctest_printer print, void* printer_cookie,
                                     const char* name, void* cookie,
                                     size_t count, size_t index,
                                     bool failed, int64_t duration) {
    time_str d_str = time_format_nsec(duration);
    if (failed) {
        print(printer_cookie, "--- FAIL: %s (%s%s)\n", name, d_str.number, d_str.unit);
    } else {
        print(printer_cookie, "--- PASS: %s (%s%s)\n", name, d_str.number, d_str.unit);
    }
    text_encoder_cookie_free(cookie);
}

static void text_encoder_on_test_log_message(ctest_printer print, void* printer_cookie,
                                             const char* test_name, void* cookie,
                                             const char* file, int line, const char* message) {
    size_t msg_len = strlen(message);
    print(printer_cookie, "    %s:%d: %s%s",
          file, line,
          message, msg_len > 0 && message[msg_len - 1] == '\n' ? "" : "\n");
    ((text_encoder_cookie*)cookie)->has_log_message = true;
}

static void* text_encoder_on_setup_benchmarks(ctest_printer print, void* printer_cookie, size_t benchmark_count) {
    if (benchmark_count == 0) {
        return NULL;
    }
    char os[512] = {0};
    char cpu[512] = {0};
    print(printer_cookie, "OS: %s\nCPU: %s\n",
          get_os_name(os, sizeof(os) / sizeof(os[0])),
          get_cpu_brand_string(cpu, sizeof(cpu) / sizeof(cpu[0])));
    return NULL;
}

static void* text_encoder_on_benchmark_begin(ctest_printer print, void* printer_cookie, const char* name, size_t benchmark_count, size_t index) {
    return text_encoder_cookie_create();
}

static void text_encoder_on_benchmark_end(ctest_printer print, void* printer_cookie,
                                          const char* name, void* cookie,
                                          ctest_benchmark_data* data,
                                          size_t count, size_t index,
                                          bool failed, int64_t duration) {
    if (data->op == 0) {
        fprintf(stderr, "CTEST_BENCHMARK_LOOP is not used in benchmark function '%s'\n", name);
        text_encoder_cookie_free(cookie);
        abort();
        return;
    }
    if (failed) {
        time_str d_str = time_format_nsec(duration);
        print(printer_cookie, "--- FAIL: %s (%s%s)\n", name, d_str.number, d_str.unit);
        text_encoder_cookie_free(cookie);
        return;
    }
    time_str duration_str = time_format_nsec(data->ns / data->op);
    print(printer_cookie, "%s\t%" PRId64 "\t%s %s/op\n", name, data->op, duration_str.number, duration_str.unit);
    text_encoder_cookie_free(cookie);
}

static void text_encoder_on_benchmark_log_message(ctest_printer print, void* printer_cookie,
                                                  const char* name, void* cookie,
                                                  const char* file, int line, const char* message) {
    if (!((text_encoder_cookie*)cookie)->has_log_message) {
        print(printer_cookie, "    %s\n", name);
    }
    text_encoder_on_test_log_message(print, printer_cookie, name, cookie, file, line, message);
}

ctest_options* ctest_options_set_text_encoder(ctest_options* options) {
    options->encoder.on_setup_test_suit = text_encoder_on_setup_test_suit;
    options->encoder.on_teardown_test_suit = text_encoder_on_teardown_test_suit;
    options->encoder.on_setup_tests = NULL;
    options->encoder.on_teardown_tests = NULL;
    options->encoder.on_test_begin = text_encoder_on_test_begin;
    options->encoder.on_test_end = text_encoder_on_test_end;
    options->encoder.on_test_log_message = text_encoder_on_test_log_message;
    options->encoder.on_setup_benchmarks = text_encoder_on_setup_benchmarks;
    options->encoder.on_teardown_benchmarks = NULL;
    options->encoder.on_benchmark_begin = text_encoder_on_benchmark_begin;
    options->encoder.on_benchmark_end = text_encoder_on_benchmark_end;
    options->encoder.on_benchmark_log_message = text_encoder_on_benchmark_log_message;
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
            case '\b':
                mem_block_append(mem, "\\b", 2);
                break;
            case '\f':
                mem_block_append(mem, "\\f", 2);
                break;
            case '\t':
                mem_block_append(mem, "\\t", 2);
                break;
            case '\n':
                mem_block_append(mem, "\\n", 2);
                break;
            case '\r':
                mem_block_append(mem, "\\r", 2);
                break;
            default:
                mem_block_expand_t(mem, char) = *p;
        }
    }
    return mem_block_append_t(mem, char, 0)->data;  // add terminating zero.
}

static void* json_encoder_on_setup_test_suit(ctest_printer print, void* printer_cookie, const char* name) {
    mem_block escape_buf;
    mem_block_init(&escape_buf);
    print(printer_cookie, "{\"name\":\"%s\"",
          name ? escape_json_string(&escape_buf, name) : "");
    mem_block_destroy(&escape_buf);
    return NULL;
}

static void json_encoder_on_teardown_test_suit(ctest_printer print, void* printer_cookie,
                                               const char* name, void* cookie,
                                               size_t failed_count, int64_t duration) {
    print(printer_cookie,
          ",\"failed_count\":%d, \"duration\":%" PRId64 "}\n",
          failed_count, duration);
}

static void* json_encoder_on_setup_tests(ctest_printer print, void* printer_cookie, size_t test_count) {
    print(printer_cookie, ",\"tests\":[");
    return NULL;
}

static void json_encoder_on_teardown_tests(ctest_printer print, void* printer_cookie, void* cookie, size_t test_count) {
    print(printer_cookie, "]");
}

// Test cookie used by JSON encoder.
typedef struct json_encoder_cookie {
    bool has_log_message;
    mem_block escape_buf;
} json_encoder_cookie;

static json_encoder_cookie* json_encoder_cookie_create() {
    json_encoder_cookie* cookie = calloc(1, sizeof(json_encoder_cookie));
    mem_block_init(&cookie->escape_buf);
    return cookie;
}

static void json_encoder_cookie_free(json_encoder_cookie* cookie) {
    mem_block_destroy(&cookie->escape_buf);
    free(cookie);
}

static void* json_encoder_on_test_begin(ctest_printer print, void* printer_cookie, const char* name, size_t test_count, size_t index) {
    json_encoder_cookie* cookie = json_encoder_cookie_create();
    print(printer_cookie, "%s{\"name\":\"%s\"", index ? "," : "", escape_json_string(&cookie->escape_buf, name));
    return cookie;
}

static void json_encoder_on_test_end(ctest_printer print, void* printer_cookie,
                                     const char* name, void* cookie,
                                     size_t test_count, size_t index,
                                     bool failed, int64_t duration) {
    print(printer_cookie, "%s,\"pass\":%s,\"duration\":%" PRId64 "}",
          ((json_encoder_cookie*)cookie)->has_log_message ? "]" : "",
          failed ? "false" : "true", duration);
    json_encoder_cookie_free(cookie);
}

static void json_encoder_on_log_message(ctest_printer print, void* printer_cookie, const char* test_name, void* test, const char* file, int line, const char* message) {
    json_encoder_cookie* cookie = test;
    bool* has_log_message = &cookie->has_log_message;
    mem_block_reset(&cookie->escape_buf);
    escape_json_string(&cookie->escape_buf, file);
    char* file_str = mem_block_dup(&cookie->escape_buf);
    mem_block_reset(&cookie->escape_buf);
    print(printer_cookie, ",%s{\"file\":\"%s\",\"line\":%d,\"message\":\"%s\"}",
          *has_log_message ? "" : "\"log\":[",
          file_str, line,
          escape_json_string(&cookie->escape_buf, message));
    free(file_str);
    *has_log_message = true;
}

static void* json_encoder_on_setup_benchmarks(ctest_printer print, void* printer_cookie, size_t benchmark_count) {
    char buf[512] = {0};
    const size_t buf_size = sizeof(buf) / sizeof(buf[0]);
    mem_block escape_buf;
    mem_block_init(&escape_buf);
    escape_json_string(&escape_buf, get_os_name(buf, buf_size));
    char* os = mem_block_dup(&escape_buf);
    mem_block_reset(&escape_buf);
    print(printer_cookie, ",\"benchmarks\":{\"OS\":\"%s\",\"CPU\":\"%s\", \"benchmarks\":[",
          os, escape_json_string(&escape_buf, get_cpu_brand_string(buf, buf_size)));
    free(os);
    mem_block_destroy(&escape_buf);
    return NULL;
}

static void json_encoder_on_teardown_benchmarks(ctest_printer print, void* printer_cookie, void* cookie, size_t benchmark_count) {
    print(printer_cookie, "]}");
}

static void* json_encoder_on_benchmark_begin(ctest_printer print, void* printer_cookie,
                                             const char* name,
                                             size_t benchmark_count, size_t index) {
    json_encoder_cookie* cookie = json_encoder_cookie_create();
    print(printer_cookie, "%s{\"name\":\"%s\"",
          index ? "," : "",
          escape_json_string(&cookie->escape_buf, name));
    return cookie;
}

static void json_encoder_on_benchmark_end(ctest_printer print, void* printer_cookie,
                                          const char* name, void* benchmark_cookie,
                                          ctest_benchmark_data* data,
                                          size_t count, size_t index,
                                          bool failed, int64_t duration) {
    if (data->op == 0) {
        fprintf(stderr, "CTEST_BENCHMARK_LOOP is not used in benchmark function '%s'\n", name);
        abort();
        return;
    }
    json_encoder_cookie* cookie = benchmark_cookie;
    if (failed) {
        print(printer_cookie, "%s,\"pass\":false,\"duration\":%" PRId64 "}",
              cookie->has_log_message ? "]" : ":",
              duration);
    } else {
        print(printer_cookie, "%s,\"ops\":%" PRIu64 ",\"ns_per_op\":%" PRIu64 ",\"pass\":%s}",
              cookie->has_log_message ? "]" : ":",
              data->op, data->ns / data->op, failed ? "false" : "true");
    }
    json_encoder_cookie_free(cookie);
}

ctest_options* ctest_options_set_json_encoder(ctest_options* options) {
    options->encoder.on_setup_test_suit = json_encoder_on_setup_test_suit;
    options->encoder.on_teardown_test_suit = json_encoder_on_teardown_test_suit;
    options->encoder.on_setup_tests = json_encoder_on_setup_tests;
    options->encoder.on_teardown_tests = json_encoder_on_teardown_tests;
    options->encoder.on_test_begin = json_encoder_on_test_begin;
    options->encoder.on_test_end = json_encoder_on_test_end;
    options->encoder.on_test_log_message = json_encoder_on_log_message;
    options->encoder.on_setup_benchmarks = json_encoder_on_setup_benchmarks;
    options->encoder.on_teardown_benchmarks = json_encoder_on_teardown_benchmarks;
    options->encoder.on_benchmark_begin = json_encoder_on_benchmark_begin;
    options->encoder.on_benchmark_end = json_encoder_on_benchmark_end;
    options->encoder.on_benchmark_log_message = json_encoder_on_log_message;
    return options;
}

////////// printers //////////

static void console_printer(void* cookie, const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (vprintf(format, args) < 0)
        ERROR_ABORT_MSG("vprintf");
    va_end(args);
}

void ctest_set_console_printer(ctest_options* options) {
    options->printer = console_printer;
    options->printer_cookie = NULL;
}

typedef struct ctest_string_printer {
    mem_block mem;
    // DO NOT ADD MORE MEMBERS!
    // SEE ctest_string_printer_free.
} ctest_string_printer;

static ctest_string_printer* ctest_string_printer_create() {
    ctest_string_printer* printer = calloc(1, sizeof(ctest_string_printer));
    mem_block_init(&printer->mem);
    return printer;
}

void ctest_string_printer_free(ctest_string_printer* printer) {
    mem_block_free((mem_block*)printer);
}

char* ctest_string_printer_str(ctest_string_printer* printer) {
    mem_block_append_t((mem_block*)printer, char, 0);
    return (char*)mem_block_data(&printer->mem);
}

static void string_printer_(void* cookie, const char* format, ...) {
    ctest_string_printer* mem = cookie;
    va_list args;
    va_start(args, format);
    mem_block_append_vsprintf(&mem->mem, false, format, args);
    va_end(args);
}

ctest_string_printer* ctest_options_create_string_printer(ctest_options* options) {
    ctest_string_printer* printer = ctest_string_printer_create();
    options->printer_cookie = printer;
    options->printer = string_printer_;
    return printer;
}

////////// log_message //////////
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

////////// ctest_test_base //////////
typedef struct ctest_test_base {
    const char* name;
    void* cookie;
    mem_block log_messages;
    bool failed;
    bool benchmark;  // is benchmark.
} ctest_test_base;

static void ctest_test_base_init(ctest_test_base* base, const char* name) {
    base->name = name;
    mem_block_init(&base->log_messages);
}

static void ctest_test_base_destroy(ctest_test_base* base) {
    const size_t message_count = mem_block_array_size_t(&base->log_messages, log_message);
    for (size_t i = 0; i < message_count; i++) {
        log_message_destroy(&mem_block_array_index_t(&base->log_messages, log_message, i));
    }
    mem_block_destroy(&base->log_messages);
}

void ctest_test_base_log(ctest_test_base* base, ctest_options* options, const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    mem_block mem;
    mem_block_init(&mem);
    mem_block_append_vsprintf(&mem, true, format, args);
    char* message = mem_block_detach(&mem);
    if (base->benchmark || options->verbose || base->failed) {
        (base->benchmark ? options->encoder.on_benchmark_log_message : options->encoder.on_test_log_message)(options->printer, options->printer_cookie,
                                                                                                             base->name, base->cookie,
                                                                                                             file, line, message);
        free(message);
    } else {
        // save it.
        log_message_init(mem_block_expand(&base->log_messages, sizeof(log_message)), file, line, message);
    }
    va_end(args);
}

void ctest_test_base_fail(ctest_test_base* base, ctest_options* options) {
    base->failed = true;
    // Print accumulated log messages.
    for (size_t i = 0; i < mem_block_array_size_t(&base->log_messages, log_message); i++) {
        log_message* msg = &mem_block_array_index_t(&base->log_messages, log_message, i);
        options->encoder.on_test_log_message(options->printer, options->printer_cookie,
                                             base->name, base->cookie,
                                             msg->file, msg->line, msg->message);
        log_message_destroy(msg);
    }
    mem_block_reset(&base->log_messages);
}

////////// ctest_test //////////
typedef struct ctest_test {
    struct ctest_test_base base;
    ctest_test_func f;
} ctest_test;

static ctest_test* ctest_test_create(const char* name, ctest_test_func f) {
    ctest_test* test = calloc(1, sizeof(ctest_test));
    ctest_test_base_init(&test->base, name);
    test->f = f;
    return test;
}

static void ctest_test_free(ctest_test* test) {
    ctest_test_base_destroy(&test->base);
    free(test);
}

////////// ctest_benchmark //////////
typedef struct ctest_benchmark {
    ctest_test_base base;
    ctest_benchmark_func f;
} ctest_benchmark;

static ctest_benchmark* ctest_benchmark_create(const char* name, ctest_benchmark_func f) {
    ctest_benchmark* bench = calloc(1, sizeof(ctest_benchmark));
    ctest_test_base_init(&bench->base, name);
    bench->base.benchmark = true;  // benchmarks always show log.
    bench->f = f;
    return bench;
}

static void ctest_benchmark_free(ctest_benchmark* bench) {
    ctest_test_base_destroy(&bench->base);
    free(bench);
}

////////// ctest_test_suit //////////

typedef struct ctest_test_suit {
    const char* name;
    mem_block tests;
    mem_block benchmarks;
    void* cookie;
} ctest_test_suit;

ctest_test_suit* ctest_test_suit_create(const char* name) {
    ctest_test_suit* suit = (ctest_test_suit*)calloc(1, sizeof(ctest_test_suit));
    suit->name = name;
    mem_block_init(&suit->tests);
    mem_block_init(&suit->benchmarks);
    return suit;
}

void ctest_test_suit_free(ctest_test_suit* suit) {
    assert(suit);
    for (size_t i = 0; i < mem_block_array_size_t(&suit->tests, ctest_test*); i++) {
        ctest_test_free(mem_block_array_index_t(&suit->tests, ctest_test*, i));
    }
    mem_block_destroy(&suit->tests);
    for (size_t i = 0; i < mem_block_array_size_t(&suit->benchmarks, ctest_benchmark*); i++) {
        ctest_benchmark_free(mem_block_array_index_t(&suit->benchmarks, ctest_benchmark*, i));
    }
    mem_block_destroy(&suit->benchmarks);
    free(suit);
}

void ctest_test_suit_add_test(ctest_test_suit* suit, const char* name, ctest_test_func f) {
    if (!f)
        ERROR_ABORT_MSG("NULL test function");

    ctest_test* test = ctest_test_create(name, f);
    if (!test)
        ERROR_ABORT_MSG("ctest_test_create");

    mem_block_expand_t(&suit->tests, ctest_test*) = test;
}

void ctest_test_suit_add_benchmark(ctest_test_suit* suit, const char* name, ctest_benchmark_func f) {
    if (!f)
        ERROR_ABORT_MSG("NULL benchmark function");

    ctest_benchmark* bench = ctest_benchmark_create(name, f);
    if (!bench)
        ERROR_ABORT_MSG("ctest_benchmark_create");

    mem_block_expand_t(&suit->benchmarks, ctest_benchmark*) = bench;
}

static void fill_default_options(ctest_options* options) {
    if (!options->printer) {
        options->printer = console_printer;
        options->printer_cookie = NULL;
    }
    if (zero_encoder(&options->encoder)) {
        ctest_options_set_text_encoder(options);
    }
}

#ifdef TEST_CTEST
// Duration used generate stable output for test.
static const int64_t test_duration = 12345;
#endif

static size_t ctest_test_suit_run_tests(ctest_test_suit* suit, ctest_options* options) {
    const size_t test_count = mem_block_array_size_t(&suit->tests, ctest_test*);

    void* tests_cookie = NULL;
    if (options->encoder.on_setup_tests) {
        tests_cookie = options->encoder.on_setup_tests(options->printer, options->printer_cookie, test_count);
    }

    size_t failure_count = 0;

    for (size_t i = 0; i < test_count; i++) {
        ctest_test* test = mem_block_array_index_t(&suit->tests, ctest_test*, i);
        if (options->encoder.on_test_begin) {
            test->base.cookie = options->encoder.on_test_begin(options->printer, options->printer_cookie,
                                                               test->base.name,
                                                               test_count, i);
        }
        time_spec start, end;
        get_time(&start);
        test->f(&test->base, options);
        get_time(&end);
        int64_t duration = time_sub_nsec(&end, &start);
#ifdef TEST_CTEST
        duration = test_duration;
#endif

        if (test->base.failed) {
            failure_count++;
        }

        if (options->encoder.on_test_end) {
            options->encoder.on_test_end(options->printer, options->printer_cookie,
                                         test->base.name, test->base.cookie,
                                         test_count, i,
                                         test->base.failed, duration);
        }
    }

    if (options->encoder.on_teardown_tests) {
        options->encoder.on_teardown_tests(options->printer, options->printer_cookie,
                                           tests_cookie, test_count);
    }

    return failure_count;
}

typedef struct ctest_benchmark_loop_args {
    time_spec* start_time;
    time_spec start_time_buf;
    size_t loop_count;
    size_t loop_index;
    ctest_benchmark_data* data;
} ctest_benchmark_loop_args;

static bool benchmark_loop(ctest_benchmark_loop_args* args) {
    if (!args->start_time) {
        get_time(&args->start_time_buf);
        args->start_time = &args->start_time_buf;
        args->loop_count = 1;
        args->loop_index = 0;
        args->data->ns = 0;
        args->data->op = 0;
    } else if (args->loop_index >= args->loop_count) {
        time_spec now;
        get_time(&now);
        int64_t d = time_sub_nsec(&now, args->start_time);
        args->data->ns += d;
        if (d < 10000000) {  // 10ms
            args->loop_count *= 10;
            args->loop_index = 0;
        } else if (args->data->ns < 600000000) {  // 600ms
            args->data->op += args->loop_count;
            args->loop_count *= (600000000 / d + 1);
            args->loop_index = 0;
            get_time(&args->start_time_buf);
            args->start_time = &args->start_time_buf;
        } else {
            args->data->op += args->loop_count;
            return false;
        }
    }
    args->loop_index++;  // a bit early, but OK.
    return true;
}

static void run_benchmark(ctest_benchmark* bench, ctest_options* options, ctest_benchmark_data* data) {
    ctest_benchmark_loop_args args = {0};
    args.data = data;
    bench->f(&bench->base, options, benchmark_loop, &args);
}

static size_t ctest_test_suit_run_benchmarks(ctest_test_suit* suit, ctest_options* options) {
    const size_t benchmark_count = mem_block_array_size_t(&suit->benchmarks, ctest_benchmark*);
    void* top_cookie = NULL;
    if (options->encoder.on_setup_benchmarks) {
        top_cookie = options->encoder.on_setup_benchmarks(options->printer, options->printer_cookie,
                                                          benchmark_count);
    }

    size_t failure_count = 0;
    for (size_t i = 0; i < benchmark_count; i++) {
        ctest_benchmark* bench = mem_block_array_index_t(&suit->benchmarks, ctest_benchmark*, i);
        if (options->encoder.on_benchmark_begin) {
            bench->base.cookie = options->encoder.on_benchmark_begin(options->printer, options->printer_cookie,
                                                                     bench->base.name,
                                                                     benchmark_count, i);
        }
        time_spec start, end;
        get_time(&start);
        ctest_benchmark_data data = {0};
        run_benchmark(bench, options, &data);
        get_time(&end);
        int64_t duration = time_sub_nsec(&end, &start);
#ifdef TEST_CTEST
        duration = test_duration;
#endif
        if (bench->base.failed) {
            failure_count++;
        }

        if (options->encoder.on_benchmark_end) {
            options->encoder.on_benchmark_end(options->printer, options->printer_cookie,
                                              bench->base.name, bench->base.cookie,
                                              &data,
                                              benchmark_count, i,
                                              bench->base.failed, duration);
        }
    }

    if (options->encoder.on_teardown_benchmarks) {
        options->encoder.on_teardown_benchmarks(options->printer, options->printer_cookie,
                                                top_cookie,
                                                benchmark_count);
    }
    return failure_count;
}

bool ctest_test_suit_run(ctest_test_suit* suit, ctest_options* options) {
    fill_default_options(options);

    if (options->encoder.on_setup_test_suit) {
        suit->cookie = options->encoder.on_setup_test_suit(options->printer, options->printer_cookie,
                                                           suit->name);
    }

    time_spec start, end;
    get_time(&start);
    size_t failure_count = ctest_test_suit_run_tests(suit, options);
    get_time(&end);
    if (failure_count == 0) {
        failure_count = ctest_test_suit_run_benchmarks(suit, options);
    }

    int64_t duration = time_sub_nsec(&end, &start);
#ifdef TEST_CTEST
    duration = test_duration;
#endif

    if (options->encoder.on_teardown_test_suit) {
        options->encoder.on_teardown_test_suit(options->printer, options->printer_cookie,
                                               suit->name, suit->cookie,
                                               failure_count, duration);
    }
    return failure_count == 0;
}

#ifdef __APPLE__
#define GET_SECTION(sec, len) get_section("__DATA", sec, len)
#else
#define GET_SECTION(sec, len) get_section(NULL, sec, len)
#endif

static char* get_base_filename(const char* path, char* buf, size_t buf_len) {
    if (buf == NULL || buf_len == 0) {
        return NULL;
    }
    const char* file = path;
    const char* sep = strrchr(file, '/');
    if (sep == NULL) {
        sep = strrchr(file, '\\');
    }
    if (sep) {
        file = sep + 1;
    }
    size_t len = strlen(file);
    sep = strrchr(file, '.');
    if (sep) {
        len = sep - file;
    }
    if (len > buf_len - 1) {
        len = buf_len - 1;
    }
    if (len > 0) {
        strncpy(buf, file, len);
    }
    buf[len] = 0;
    return buf;
}

int ctest_main(int argc, char* argv[], ctest_options* options) {
    size_t sec_len = 0;
    ctest_sec_** secs = GET_SECTION(CTEST_SECTION_, &sec_len);
    if (!secs) {
        ERROR_ABORT_MSG("get_section");
    }
    if (!sec_len) {
        return 0;
    }

    char suit_name[16] = {0};

    ctest_test_suit* suit = ctest_test_suit_create(get_base_filename(__FILE__, suit_name, sizeof(suit_name) / sizeof(suit_name[0])));
    const size_t count = sec_len / sizeof(ctest_sec_*);
    for (size_t i = 0; i < count; i++) {
        ctest_sec_* sec = secs[i];
        if (sec->t) {
            ctest_test_suit_add_test(suit, sec->name, sec->t);
        } else if (sec->b) {
            ctest_test_suit_add_benchmark(suit, sec->name, sec->b);
        } else {
            ERROR_ABORT_MSG("no test or benchmark");
        }
    }

    bool ok = ctest_test_suit_run(suit, options);
    ctest_test_suit_free(suit);

    return ok ? 0 : -1;
}

////////// Test ctest itself //////////
#ifdef TEST_CTEST

CTEST_TEST(test_mem_block_create) {
    mem_block* mem = mem_block_create();
    uint8_t zero_block[sizeof(mem_block)] = {0};
    if (memcmp(mem, zero_block, sizeof(mem_block)) != 0) {
        CTEST_FAIL();
    }
    mem_block_free(mem);
}

CTEST_TEST(test_mem_block_init) {
    mem_block mem;
    mem_block_init(&mem);
    uint8_t zero_block[sizeof(mem_block)] = {0};
    if (memcmp(&mem, zero_block, sizeof(mem_block)) != 0) {
        CTEST_FAIL();
    }
    mem_block_destroy(&mem);
}

CTEST_TEST(test_mem_block_append) {
    mem_block* mem = mem_block_create();
    mem_block_append(mem, "abc", 3);

    size_t len = mem_block_len(mem);
    if (len != 3) {
        CTEST_FAILF("want %d, got %d", 3, len);
    }

    size_t cap = mem_block_cap(mem);
    if (cap != 6) {
        CTEST_FAILF("want %d, got %d", 6, cap);
    }

    char c0 = mem_block_array_index_t(mem, char, 0);
    if (c0 != 'a') {
        CTEST_FAILF("want %c, got %c", 'a', c0);
    }
    char c1 = mem_block_array_index_t(mem, char, 1);
    if (c1 != 'b') {
        CTEST_FAILF("want %c, got %c", 'b', c1);
    }
    char c2 = mem_block_array_index_t(mem, char, 2);
    if (c2 != 'c') {
        CTEST_FAILF("want %c, got %c", 'c', c2);
    }
    mem_block_free(mem);
}

CTEST_TEST(test_mem_block_sprintf) {
    mem_block* mem = mem_block_create();
    mem_block_expand_t(mem, char) = '-';
    mem_block_append_sprintf(mem, false, "%s%d", "abc", 123);

    size_t len = mem_block_len(mem);
    if (len != 7) {
        CTEST_FAILF("want %d, got %d", 7, len);
    }

    size_t cap = mem_block_cap(mem);
    if (cap != 16) {
        CTEST_FAILF("want %d, got %d", 16, cap);
    }
    const char* str = mem_block_data(mem_block_append_t(mem, char, '\0'));
    if (strcmp(str, "-abc123") != 0) {
        CTEST_FAILF("want %s, got %s", "-abc123", str);
    }

    mem_block_delete(mem, mem_block_len(mem) - 1, 1);
    mem_block_append_sprintf(mem, true, "|");
    len = mem_block_len(mem);
    if (len != 9) {
        CTEST_FAILF("want %d, got %d", 9, len);
    }
    str = mem_block_data(mem);
    if (strcmp(str, "-abc123|") != 0) {
        CTEST_FAILF("want %s, got %s", "-abc123", str);
    }

    mem_block_free(mem);
}

CTEST_TEST(test_mem_block_delete) {
    mem_block* mem = mem_block_create();
    mem_block_append(mem, "0123456789", 10);
    mem_block_delete(mem, 8, 2);
    size_t len = mem_block_len(mem);
    if (len != 8) {
        CTEST_FAILF("want %d, got %d", 8, len);
    }
    if (memcmp(mem_block_data(mem), "01234567", len) != 0) {
        CTEST_FAIL();
    }

    mem_block_delete(mem, 0, 1);
    len = mem_block_len(mem);
    if (len != 7) {
        CTEST_FAILF("want %d, got %d", 7, len);
    }
    if (memcmp(mem_block_data(mem), "1234567", len) != 0) {
        CTEST_FAIL();
    }

    mem_block_delete(mem, 1, 2);
    len = mem_block_len(mem);
    if (len != 5) {
        CTEST_FAILF("want %d, got %d", 5, len);
    }
    if (memcmp(mem_block_data(mem), "14567", len) != 0) {
        CTEST_FAIL();
    }

    mem_block_delete(mem, 0, -1);
    len = mem_block_len(mem);
    if (len != 0) {
        CTEST_FAILF("want %d, got %d", 0, len);
    }

    mem_block_free(mem);
}

CTEST_TEST(test_mem_block_reset) {
    mem_block* mem = mem_block_create();
    mem_block_append(mem, "abc", 3);
    size_t old_cap = mem_block_cap(mem);
    mem_block_reset(mem);
    size_t cap = mem_block_cap(mem);
    if (cap != old_cap) {
        CTEST_FAILF("want %d, got %d", old_cap, cap);
    }
    size_t len = mem_block_len(mem);
    if (len != 0) {
        CTEST_FAILF("want %d, got %d", 0, len);
    }

    mem_block_free(mem);
}

CTEST_TEST(test_mem_block_trim) {
    mem_block* mem = mem_block_create();
    mem_block_append(mem, "abc", 3);
    mem_block_trim(mem);
    size_t len = mem_block_len(mem);
    if (len != 3) {
        CTEST_FAILF("want %d, got %d", 3, len);
    }
    size_t cap = mem_block_cap(mem);
    if (cap != len) {
        CTEST_FAILF("want %d, got %d", len, cap);
    }
    if (memcmp(mem_block_data(mem), "abc", 3) != 0) {
        CTEST_FAIL();
    }

    mem_block_free(mem);
}

CTEST_TEST(test_mem_block_detach) {
    mem_block* mem = mem_block_create();
    mem_block_append(mem, "abc", 3);
    void* p = mem_block_detach(mem);
    size_t len = mem_block_len(mem);
    if (len != 0) {
        CTEST_FAILF("want %d, got %d", 0, len);
    }
    size_t cap = mem_block_cap(mem);
    if (cap != 0) {
        CTEST_FAILF("want %d, got %d", 0, len);
    }
    if (memcmp(p, "abc", 3) != 0) {
        CTEST_FAIL();
    }

    free(p);
    mem_block_free(mem);
}

CTEST_TEST(test_get_base_filename) {
    char buf[32];
    const size_t buf_size = sizeof(buf) / sizeof(buf[0]);
    char* p = get_base_filename("", buf, buf_size);
    if (strcmp(p, "") != 0) {
        CTEST_FATALF("want \"%s\", got \"%s\"", "", p);
    }

    buf[0] = 0;
    p = get_base_filename("/file/path", buf, buf_size);
    if (strcmp(p, "path") != 0) {
        CTEST_FATALF("want \"%s\", got \"%s\"", "path", p);
    }

    buf[0] = 0;
    p = get_base_filename("\\file\\path", buf, buf_size);
    if (strcmp(p, "path") != 0) {
        CTEST_FATALF("want \"%s\", got \"%s\"", "path", p);
    }

    buf[0] = 0;
    p = get_base_filename("/file/path.ext", buf, buf_size);
    if (strcmp(p, "path") != 0) {
        CTEST_FATALF("want \"%s\", got \"%s\"", "path", p);
    }
}

CTEST_TEST_FUNC(empty_test) {}

const char* log_it_log_file = NULL;
int log_it_log_line = 0;

CTEST_TEST_FUNC(log_it) {
    CTEST_LOGF("1+1=%d\na:{\"b\":3}", 2), log_it_log_file = __FILE__, log_it_log_line = __LINE__;
}

CTEST_TEST(test_escape_json_string) {
    mem_block buf = {0};
    const char* src = "a\"\\b/c\b\f\t\n\r";
    const char* want = "a\\\"\\\\b\\/c\\b\\f\\t\\n\\r";
    const char* escaped = escape_json_string(&buf, src);
    if (strcmp(escaped, want) != 0) {
        CTEST_FATALF("want \"%s\" got \"%s\"", want, escaped);
    }
    mem_block_destroy(&buf);
}

CTEST_TEST(test_text_output) {
    ctest_test_suit* suit = ctest_test_suit_create("test_text_output");
    CTEST_ADD_TEST(suit, empty_test);
    CTEST_ADD_TEST(suit, log_it);
    ctest_options options = {0};
    options.verbose = true;
    ctest_string_printer* printer = ctest_options_create_string_printer(&options);
    bool pass = ctest_test_suit_run(suit, &options);
    if (!pass) {
        CTEST_FAILF("want %s, got %s", "true", pass ? "true" : "false");
    }
    char* output = ctest_string_printer_str(printer);
    time_str d_str = time_format_nsec(test_duration);
    char want_output[1024] = {0};
    snprintf(want_output, sizeof(want_output) / sizeof(want_output[0]),
             "*** test_text_output ***\n"
             "=== RUN   empty_test\n"
             "--- PASS: empty_test (%s%s)\n"
             "=== RUN   log_it\n"
             "    %s:%d: 1+1=2\na:{\"b\":3}\n"
             "--- PASS: log_it (%s%s)\n"
             "PASS\ttest_text_output %s%s\n",
             d_str.number, d_str.unit,
             log_it_log_file, log_it_log_line,
             d_str.number, d_str.unit,
             d_str.number, d_str.unit);
    if (strcmp(output, want_output) != 0) {
        CTEST_FAILF("*-*-*-*-*\nwant\n%s\n, got\n%s*-*-*-*-*", want_output, output);
    }

    ctest_string_printer_free(printer);
    ctest_test_suit_free(suit);
}

CTEST_TEST(test_json_output) {
    ctest_test_suit* suit = ctest_test_suit_create("test_text_output");
    CTEST_ADD_TEST(suit, empty_test);
    CTEST_ADD_TEST(suit, log_it);
    ctest_options options = {0};
    options.verbose = true;
    ctest_options_set_json_encoder(&options);
    ctest_string_printer* printer = ctest_options_create_string_printer(&options);
    bool pass = ctest_test_suit_run(suit, &options);
    if (!pass) {
        CTEST_FAILF("want %s, got %s", "true", pass ? "true" : "false");
    }
    char* output = ctest_string_printer_str(printer);
    char want_output[1024] = {0};
    mem_block json_log_file_buffer = {0};
    mem_block_init(&json_log_file_buffer);
    mem_block json_os_name_buffer = {0};
    mem_block_init(&json_os_name_buffer);
    mem_block json_cpu_brand_buffer = {0};
    mem_block_init(&json_cpu_brand_buffer);
    char os_name_buffer[64] = {0};
    char cpu_brand_buffer[49] = {0};
    snprintf(want_output, sizeof(want_output) / sizeof(want_output[0]),
             "{\"name\":\"test_text_output\","
             "\"tests\":["
             "{\"name\":\"empty_test\",\"pass\":true,\"duration\":%" PRId64
             "},"
             "{\"name\":\"log_it\",\"log\":[{\"file\":\"%s\",\"line\":%d,\"message\":\"1+1=2\\na:{\\\"b\\\":3}\"}"
             "],"
             "\"pass\":true,\"duration\":%" PRId64
             "}],"
             "\"benchmarks\":{\"OS\":\"%s\",\"CPU\":\"%s\", \"benchmarks\":[]},\"failed_count\":0, \"duration\":%" PRId64 "}\n",
             test_duration,
             escape_json_string(&json_log_file_buffer, log_it_log_file), log_it_log_line,
             test_duration,
             escape_json_string(&json_os_name_buffer, get_os_name(os_name_buffer, sizeof(os_name_buffer) / sizeof(os_name_buffer[0]))),
             escape_json_string(&json_cpu_brand_buffer, get_cpu_brand_string(cpu_brand_buffer, sizeof(cpu_brand_buffer) / sizeof(cpu_brand_buffer[0]))),
             test_duration);

    mem_block_destroy(&json_log_file_buffer);
    mem_block_destroy(&json_os_name_buffer);
    mem_block_destroy(&json_cpu_brand_buffer);
    if (strcmp(output, want_output) != 0) {
        CTEST_FAILF("*-*-*-*-*\nwant\n%s\n, got\n%s*-*-*-*-*", want_output, output);
    }

    ctest_string_printer_free(printer);
    ctest_test_suit_free(suit);
}

#if defined(_WIN32) || defined(_WIN64)

#include <Windows.h>
void sleep_ms(uint32_t ms) {
    Sleep(ms);
}

#else

#include <time.h>
#include <unistd.h>
void sleep_ms(uint32_t ms) {
    usleep(ms * 1000);
}
#endif

CTEST_BENCHMARK_FUNC(benchmark_sleep_10ms) {
    CTEST_BENCHMARK_LOOP {
        sleep_ms(10);
    }
}

CTEST_TEST(benchmark_sleep_10ms) {
    ctest_test_suit* suit = ctest_test_suit_create("test_benchmark_sleep");
    CTEST_ADD_BENCHMARK(suit, benchmark_sleep_10ms);
    ctest_options options = {0};
    options.verbose = true;
    ctest_options_set_json_encoder(&options);
    ctest_string_printer* printer = ctest_options_create_string_printer(&options);
    bool pass = ctest_test_suit_run(suit, &options);
    if (!pass) {
        CTEST_FAILF("want %s, got %s", "true", pass ? "true" : "false");
    }
    char* output = ctest_string_printer_str(printer);
    const char* key = "\"ns_per_op\":";
    const char* key_start = strstr(output, key);
    if (!key_start) {
        CTEST_FATALF("can't find key \"%s\"", key);
    }
    const char* value_begin = key_start + strlen(key);
    const char* value_end = strchr(value_begin + 1, ',');
    if (!value_end) {
        CTEST_FATALF("can't find delimiter \",\"");
    }
    char buf[64] = {0};
    errno = 0;
    int64_t ns_per_op = strtoll(strncpy(buf, value_begin, value_end - value_begin), NULL, 10);
    if (errno) {
        CTEST_FATALF("strtoll failed: %d %s", errno, strerror(errno));
    }
    if (ns_per_op > 15000000 || ns_per_op < 10000000) {
        CTEST_FATALF("the error is too large: %" PRId64 " vs %" PRId64, ns_per_op, 10000000);
    }

    ctest_string_printer_free(printer);
    ctest_test_suit_free(suit);
}

int main(int argc, char* argv[]) {
    ctest_options options = {0};
    options.verbose = true;

    int ret = ctest_main(argc, argv, &options);

#ifdef ENABLE_LKDBG
    lkdbg_report();
#endif
    return ret;
}

#endif