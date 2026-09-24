/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include <vector>

#include "bh_read_file.h"
#include "wasm_runtime_common.h"

static std::string CWD;

#if WASM_DISABLE_HW_BOUND_CHECK != 0
#define TEST_SUITE_NAME linear_memory_test_suite_wasm_no_hw_bound
#else
#define TEST_SUITE_NAME linear_memory_test_suite_wasm
#endif

class TEST_SUITE_NAME : public testing::Test
{
  protected:
    // You should make the members protected s.t. they can be
    // accessed from sub-classes.

    // virtual void SetUp() will be called before each test is run.  You
    // should define it if you need to initialize the variables.
    // Otherwise, this can be skipped.
    virtual void SetUp() {}

    static void SetUpTestCase() { CWD = get_test_binary_dir(); }

    // virtual void TearDown() will be called after each test is run.
    // You should define it if there is cleanup work to do.  Otherwise,
    // you don't have to provide it.
    //
    virtual void TearDown() {}

    WAMRRuntimeRAII<512 * 1024> runtime;
};

struct ret_env {
    wasm_exec_env_t exec_env;
    wasm_module_t wasm_module;
    wasm_module_inst_t wasm_module_inst;
    unsigned char *wasm_file_buf;
    char error_buf[128];
};

struct ret_env
load_wasm(char *wasm_file_tested, unsigned int app_heap_size)
{
    std::string wasm_mem_page = wasm_file_tested;
    const char *wasm_file = strdup((CWD + wasm_mem_page).c_str());
    wasm_module_inst_t wasm_module_inst = nullptr;
    wasm_module_t wasm_module = nullptr;
    wasm_exec_env_t exec_env = nullptr;
    unsigned char *wasm_file_buf = nullptr;
    unsigned int wasm_file_size = 0;
    unsigned int stack_size = 16 * 1024, heap_size = app_heap_size;
    char error_buf[128] = { 0 };
    struct ret_env ret_module_env;

    memset(ret_module_env.error_buf, 0, 128);
    wasm_file_buf =
        (unsigned char *)bh_read_file_to_buffer(wasm_file, &wasm_file_size);
    if (!wasm_file_buf) {
        goto fail;
    }

    wasm_module = wasm_runtime_load(wasm_file_buf, wasm_file_size, error_buf,
                                    sizeof(error_buf));
    if (!wasm_module) {
        memcpy(ret_module_env.error_buf, error_buf, 128);
        goto fail;
    }

    wasm_module_inst = wasm_runtime_instantiate(
        wasm_module, stack_size, heap_size, error_buf, sizeof(error_buf));
    if (!wasm_module_inst) {
        memcpy(ret_module_env.error_buf, error_buf, 128);
        goto fail;
    }

    exec_env = wasm_runtime_create_exec_env(wasm_module_inst, stack_size);

fail:
    ret_module_env.exec_env = exec_env;
    ret_module_env.wasm_module = wasm_module;
    ret_module_env.wasm_module_inst = wasm_module_inst;
    ret_module_env.wasm_file_buf = wasm_file_buf;

    return ret_module_env;
}

void
destroy_module_env(struct ret_env module_env)
{
    if (module_env.exec_env) {
        wasm_runtime_destroy_exec_env(module_env.exec_env);
    }

    if (module_env.wasm_module_inst) {
        wasm_runtime_deinstantiate(module_env.wasm_module_inst);
    }

    if (module_env.wasm_module) {
        wasm_runtime_unload(module_env.wasm_module);
    }

    if (module_env.wasm_file_buf) {
        wasm_runtime_free(module_env.wasm_file_buf);
    }
}

namespace {

/* Wire-format flag bits for the custom-page-sizes proposal. */
constexpr uint8_t kMaxPageCountFlag = 0x01;
constexpr uint8_t kMemory64Flag = 0x04;
constexpr uint8_t kCustomPageSizeFlag = 0x08;

void
append_leb_u32(std::vector<uint8_t> &buf, uint32_t value)
{
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (value != 0)
            byte |= 0x80;
        buf.push_back(byte);
    } while (value != 0);
}

void
append_leb_u64(std::vector<uint8_t> &buf, uint64_t value)
{
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (value != 0)
            byte |= 0x80;
        buf.push_back(byte);
    } while (value != 0);
}

void
append_header(std::vector<uint8_t> &buf)
{
    /* magic: \0asm */
    buf.push_back(0x00);
    buf.push_back(0x61);
    buf.push_back(0x73);
    buf.push_back(0x6d);
    /* version: 1 */
    buf.push_back(0x01);
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x00);
}

void
append_section(std::vector<uint8_t> &buf, uint8_t section_id,
               const std::vector<uint8_t> &content)
{
    buf.push_back(section_id);
    append_leb_u32(buf, (uint32_t)content.size());
    buf.insert(buf.end(), content.begin(), content.end());
}

/*
 * Hand-builds a module with:
 *  - one memory: flags=(CUSTOM_PAGE_SIZE_FLAG|MAX_PAGE_COUNT_FLAG)=0x09,
 *    init=1, max=10, page_size_log2=8 (256 bytes/page)
 *  - one exported function () -> i32 named "grow_and_size" whose body is:
 *    i32.const 2; memory.grow 0; drop; memory.size 0; end
 * (grows memory by 2 pages then returns the new page count).
 *
 * wabt/wat2wasm has no support for the custom-page-sizes proposal's
 * memory-limits encoding, so this module is hand-constructed byte-by-byte,
 * matching the technique used by the loader-level tests.
 */
std::vector<uint8_t>
build_custom_page_size_grow_and_size_module()
{
    std::vector<uint8_t> module;
    append_header(module);

    /* type section: one func type () -> (i32) */
    std::vector<uint8_t> type_section;
    append_leb_u32(type_section, 1); /* type count */
    type_section.push_back(0x60);   /* func */
    type_section.push_back(0x00);   /* param count */
    type_section.push_back(0x01);   /* result count */
    type_section.push_back(0x7f);   /* i32 */
    append_section(module, /* SECTION_TYPE_TYPE */ 1, type_section);

    /* function section: one function, type index 0 */
    std::vector<uint8_t> function_section;
    append_leb_u32(function_section, 1);
    append_leb_u32(function_section, 0);
    append_section(module, /* SECTION_TYPE_FUNC */ 3, function_section);

    /* memory section: flags=0x09, init=1, max=10, page_size_log2=8 */
    std::vector<uint8_t> mem_section;
    append_leb_u32(mem_section, 1); /* memory count */
    uint8_t mem_flags = kCustomPageSizeFlag | kMaxPageCountFlag;
    mem_section.push_back(mem_flags);
    append_leb_u32(mem_section, 1);  /* init */
    append_leb_u32(mem_section, 10); /* max */
    append_leb_u32(mem_section, 8);  /* page_size_log2 */
    append_section(module, /* SECTION_TYPE_MEMORY */ 5, mem_section);

    /* export section: export function 0 as "grow_and_size" */
    std::vector<uint8_t> export_section;
    append_leb_u32(export_section, 1); /* export count */
    const char *export_name = "grow_and_size";
    append_leb_u32(export_section, (uint32_t)strlen(export_name));
    export_section.insert(export_section.end(), export_name,
                          export_name + strlen(export_name));
    export_section.push_back(/* EXPORT_KIND_FUNC */ 0);
    append_leb_u32(export_section, 0); /* function index 0 */
    append_section(module, /* SECTION_TYPE_EXPORT */ 7, export_section);

    /* code section: body = i32.const 2; memory.grow 0; drop;
     * memory.size 0; end */
    std::vector<uint8_t> code_section;
    append_leb_u32(code_section, 1); /* function body count */
    std::vector<uint8_t> body;
    body.push_back(0x00); /* local decl count */
    body.push_back(0x41); /* i32.const */
    body.push_back(0x02); /* 2 */
    body.push_back(0x40); /* memory.grow */
    body.push_back(0x00); /* memidx */
    body.push_back(0x1a); /* drop */
    body.push_back(0x3f); /* memory.size */
    body.push_back(0x00); /* memidx */
    body.push_back(0x0b); /* end */
    append_leb_u32(code_section, (uint32_t)body.size());
    code_section.insert(code_section.end(), body.begin(), body.end());
    append_section(module, /* SECTION_TYPE_CODE */ 10, code_section);

    return module;
}

/*
 * Hand-builds a memory64 + custom-page-size module:
 *  - one memory: flags=(CUSTOM_PAGE_SIZE_FLAG|MAX_PAGE_COUNT_FLAG|
 *    MEMORY64_FLAG)=0x0D, init=1, max=10 (u64-LEB, memory64 index width),
 *    page_size_log2=8 (256 bytes/page)
 *  - one exported function () -> i64 named "grow_and_size_mem64" whose
 *    body is: i64.const 2; memory.grow 0; drop; memory.size 0; end
 * (grows memory by 2 pages then returns the new page count as i64).
 */
std::vector<uint8_t>
build_memory64_custom_page_size_grow_and_size_module()
{
    std::vector<uint8_t> module;
    append_header(module);

    /* type section: one func type () -> (i64) */
    std::vector<uint8_t> type_section;
    append_leb_u32(type_section, 1); /* type count */
    type_section.push_back(0x60);   /* func */
    type_section.push_back(0x00);   /* param count */
    type_section.push_back(0x01);   /* result count */
    type_section.push_back(0x7e);   /* i64 */
    append_section(module, /* SECTION_TYPE_TYPE */ 1, type_section);

    /* function section: one function, type index 0 */
    std::vector<uint8_t> function_section;
    append_leb_u32(function_section, 1);
    append_leb_u32(function_section, 0);
    append_section(module, /* SECTION_TYPE_FUNC */ 3, function_section);

    /* memory section: flags=0x0D, init=1, max=10 (u64), page_size_log2=8 */
    std::vector<uint8_t> mem_section;
    append_leb_u32(mem_section, 1); /* memory count */
    uint8_t mem_flags =
        kCustomPageSizeFlag | kMaxPageCountFlag | kMemory64Flag;
    mem_section.push_back(mem_flags);
    append_leb_u64(mem_section, 1);  /* init (u64) */
    append_leb_u64(mem_section, 10); /* max (u64) */
    append_leb_u32(mem_section, 8);  /* page_size_log2 */
    append_section(module, /* SECTION_TYPE_MEMORY */ 5, mem_section);

    /* export section: export function 0 as "grow_and_size_mem64" */
    std::vector<uint8_t> export_section;
    append_leb_u32(export_section, 1); /* export count */
    const char *export_name = "grow_and_size_mem64";
    append_leb_u32(export_section, (uint32_t)strlen(export_name));
    export_section.insert(export_section.end(), export_name,
                          export_name + strlen(export_name));
    export_section.push_back(/* EXPORT_KIND_FUNC */ 0);
    append_leb_u32(export_section, 0); /* function index 0 */
    append_section(module, /* SECTION_TYPE_EXPORT */ 7, export_section);

    /* code section: body = i64.const 2; memory.grow 0; drop;
     * memory.size 0; end */
    std::vector<uint8_t> code_section;
    append_leb_u32(code_section, 1); /* function body count */
    std::vector<uint8_t> body;
    body.push_back(0x00); /* local decl count */
    body.push_back(0x42); /* i64.const */
    body.push_back(0x02); /* 2 */
    body.push_back(0x40); /* memory.grow */
    body.push_back(0x00); /* memidx */
    body.push_back(0x1a); /* drop */
    body.push_back(0x3f); /* memory.size */
    body.push_back(0x00); /* memidx */
    body.push_back(0x0b); /* end */
    append_leb_u32(code_section, (uint32_t)body.size());
    code_section.insert(code_section.end(), body.begin(), body.end());
    append_section(module, /* SECTION_TYPE_CODE */ 10, code_section);

    return module;
}

} // namespace

TEST_F(TEST_SUITE_NAME, test_custom_page_size_memory_grow_and_size_execution)
{
    auto module_bytes = build_custom_page_size_grow_and_size_module();

    char error_buf[128] = { 0 };
    wasm_module_t wasm_module = wasm_runtime_load(
        module_bytes.data(), (uint32_t)module_bytes.size(), error_buf,
        sizeof(error_buf));
    ASSERT_NE(nullptr, wasm_module) << error_buf;

    /* heap_size=0: WAMR's pre-existing app-heap-insertion logic (unrelated
     * to custom page sizes) sizes extra pages to fit a requested heap
     * using the memory's OWN page size -- a 16KB heap request against a
     * 256-byte page would consume ~64 pages just for the heap, before
     * this test's own memory.grow(2) even runs, defeating the discriminating
     * proof below. Zero heap keeps this test isolated to grow/size only. */
    wasm_module_inst_t wasm_module_inst = wasm_runtime_instantiate(
        wasm_module, 16 * 1024, /* heap_size */ 0, error_buf,
        sizeof(error_buf));
    ASSERT_NE(nullptr, wasm_module_inst) << error_buf;

    wasm_exec_env_t exec_env =
        wasm_runtime_create_exec_env(wasm_module_inst, 16 * 1024);
    ASSERT_NE(nullptr, exec_env);

    WASMFunctionInstanceCommon *func =
        wasm_runtime_lookup_function(wasm_module_inst, "grow_and_size");
    ASSERT_NE(nullptr, func);

    uint32 argv[1] = { 0 };
    bool ret = wasm_runtime_call_wasm(exec_env, func, 0, argv);
    ASSERT_TRUE(ret) << wasm_runtime_get_exception(wasm_module_inst);

    /* grow_and_size grows by 2 pages (1 -> 3) and returns memory.size */
    EXPECT_EQ(3u, argv[0]);

    /* Public accessor consistency: post-grow page count is 3,
     * bytes-per-page is 256. */
    wasm_memory_inst_t memory_inst =
        wasm_runtime_get_memory(wasm_module_inst, 0);
    ASSERT_NE(nullptr, memory_inst);
    EXPECT_EQ(3u, wasm_memory_get_cur_page_count(memory_inst));
    EXPECT_EQ(256u, wasm_memory_get_bytes_per_page(memory_inst));

    /* Discriminating bounds-check proof: at 256 bytes/page, 3 pages = 768
     * bytes total. A byte offset just past the true (small) allocated
     * size must be rejected -- which would NOT be the case if the
     * runtime silently still assumed 64 KiB pages internally. */
    EXPECT_TRUE(
        wasm_runtime_validate_app_addr(wasm_module_inst, 0, 768));
    EXPECT_FALSE(
        wasm_runtime_validate_app_addr(wasm_module_inst, 0, 769));

    wasm_runtime_destroy_exec_env(exec_env);
    wasm_runtime_deinstantiate(wasm_module_inst);
    wasm_runtime_unload(wasm_module);
}

#if WASM_ENABLE_MEMORY64 != 0
TEST_F(TEST_SUITE_NAME,
       test_memory64_custom_page_size_memory_grow_and_size_execution)
{
    auto module_bytes = build_memory64_custom_page_size_grow_and_size_module();

    char error_buf[128] = { 0 };
    wasm_module_t wasm_module = wasm_runtime_load(
        module_bytes.data(), (uint32_t)module_bytes.size(), error_buf,
        sizeof(error_buf));
    ASSERT_NE(nullptr, wasm_module) << error_buf;

    /* heap_size=0, same rationale as the memory32 custom-page-size test
     * above: avoid the app-heap-insertion logic skewing page counts at a
     * tiny 256-byte page size. */
    wasm_module_inst_t wasm_module_inst = wasm_runtime_instantiate(
        wasm_module, 16 * 1024, /* heap_size */ 0, error_buf,
        sizeof(error_buf));
    ASSERT_NE(nullptr, wasm_module_inst) << error_buf;

    wasm_exec_env_t exec_env =
        wasm_runtime_create_exec_env(wasm_module_inst, 16 * 1024);
    ASSERT_NE(nullptr, exec_env);

    WASMFunctionInstanceCommon *func = wasm_runtime_lookup_function(
        wasm_module_inst, "grow_and_size_mem64");
    ASSERT_NE(nullptr, func);

    /* i64 result occupies two argv slots (WAMR ABI). */
    uint32 argv[2] = { 0, 0 };
    bool ret = wasm_runtime_call_wasm(exec_env, func, 0, argv);
    ASSERT_TRUE(ret) << wasm_runtime_get_exception(wasm_module_inst);

    uint64 result;
    memcpy(&result, argv, sizeof(uint64));
    /* grow_and_size_mem64 grows by 2 pages (1 -> 3) and returns
     * memory.size as i64. */
    EXPECT_EQ(3u, result);

    wasm_memory_inst_t memory_inst =
        wasm_runtime_get_memory(wasm_module_inst, 0);
    ASSERT_NE(nullptr, memory_inst);
    EXPECT_EQ(3u, wasm_memory_get_cur_page_count(memory_inst));
    EXPECT_EQ(256u, wasm_memory_get_bytes_per_page(memory_inst));

    /* Same discriminating bounds-check proof as the memory32 case,
     * confirming memory64 indexing doesn't change custom-page-size
     * byte-size computation: 3 pages * 256 bytes/page = 768 bytes. */
    EXPECT_TRUE(
        wasm_runtime_validate_app_addr(wasm_module_inst, 0, 768));
    EXPECT_FALSE(
        wasm_runtime_validate_app_addr(wasm_module_inst, 0, 769));

    wasm_runtime_destroy_exec_env(exec_env);
    wasm_runtime_deinstantiate(wasm_module_inst);
    wasm_runtime_unload(wasm_module);
}
#endif /* WASM_ENABLE_MEMORY64 != 0 */

TEST_F(TEST_SUITE_NAME, test_wasm_mem_page_count)
{
    struct ret_env tmp_module_env;
    const char *wasm_file_normal[9] = {
        "/wasm_mem_page_01.wasm", "/wasm_mem_page_02.wasm",
        "/wasm_mem_page_05.wasm", "/wasm_mem_page_07.wasm",
        "/wasm_mem_page_08.wasm", "/wasm_mem_page_09.wasm",
        "/wasm_mem_page_10.wasm", "/wasm_mem_page_12.wasm",
        "/wasm_mem_page_14.wasm"
    };
    unsigned int num_normal_wasm =
        sizeof(wasm_file_normal) / sizeof(wasm_file_normal[0]);

    const char *wasm_file_error[10] = {
        "/wasm_mem_page_03.wasm", "/wasm_mem_page_04.wasm",
        "/wasm_mem_page_06.wasm", "/wasm_mem_page_11.wasm",
        "/wasm_mem_page_13.wasm", "/wasm_mem_page_15.wasm",
        "/wasm_mem_page_16.wasm", "/wasm_mem_page_17.wasm",
        "/wasm_mem_page_18.wasm", "/wasm_mem_page_19.wasm"
    };
    unsigned int num_error_wasm =
        sizeof(wasm_file_error) / sizeof(wasm_file_error[0]);

    // Test normal wasm file.
    for (int i = 0; i < num_normal_wasm; i++) {
#if UINTPTR_MAX != UINT64_MAX
        // 32 bit do not load this wasm.
        if ((0 == strcmp("/wasm_mem_page_12.wasm", wasm_file_normal[i]))
            || (0 == strcmp("/wasm_mem_page_14.wasm", wasm_file_normal[i]))) {
            continue;
        }
#endif
        tmp_module_env = load_wasm((char *)wasm_file_normal[i], 16 * 1024);
        EXPECT_NE(nullptr, tmp_module_env.wasm_module);
        EXPECT_NE(nullptr, tmp_module_env.wasm_file_buf);

#if WASM_DISABLE_HW_BOUND_CHECK == 0
        EXPECT_NE(nullptr, tmp_module_env.exec_env);
        EXPECT_NE(nullptr, tmp_module_env.wasm_module_inst);
#endif
        destroy_module_env(tmp_module_env);
    }

    // Test error wasm file.
    for (int i = 0; i < num_error_wasm; i++) {
        tmp_module_env = load_wasm((char *)wasm_file_error[i], 16 * 1024);

        if (0 != strlen(tmp_module_env.error_buf)) {
            EXPECT_EQ(0, strncmp("WASM module",
                                 (const char *)tmp_module_env.error_buf, 11));
        }

        destroy_module_env(tmp_module_env);
    }
}

TEST_F(TEST_SUITE_NAME, test_wasm_about_app_heap)
{
    struct ret_env tmp_module_env;

    // Test case: init_page_count = 65536, app heap size = 1.
    tmp_module_env = load_wasm((char *)"/wasm_mem_page_03.wasm", 1);
    EXPECT_EQ(0, strncmp("WASM module instantiate failed",
                         (const char *)tmp_module_env.error_buf, 30));
    destroy_module_env(tmp_module_env);

    // Test case: init_page_count = 65535, app heap size = 65537.
    tmp_module_env = load_wasm((char *)"/wasm_mem_page_20.wasm", 65537);
    EXPECT_EQ(0, strncmp("WASM module instantiate failed",
                         (const char *)tmp_module_env.error_buf, 30));
    destroy_module_env(tmp_module_env);
}

TEST_F(TEST_SUITE_NAME, test_throw_exception_out_of_bounds)
{
    struct ret_env tmp_module_env;
    WASMFunctionInstanceCommon *func = nullptr;
    bool ret = false;
    uint32 argv[1] = { 9999 * 64 * 1024 };
    const char *exception = nullptr;

    tmp_module_env = load_wasm((char *)"/out_of_bounds.wasm", 16 * 1024);
    func =
        wasm_runtime_lookup_function(tmp_module_env.wasm_module_inst, "load");
    if (!func) {
        printf("\nFailed to wasm_runtime_lookup_function!\n");
        goto failed_out_of_bounds;
    }

    ret = wasm_runtime_call_wasm(tmp_module_env.exec_env, func, 1, argv);
    if (!ret) {
        printf("\nFailed to wasm_runtime_call_wasm!\n");
    }

    exception = wasm_runtime_get_exception(tmp_module_env.wasm_module_inst);
    EXPECT_EQ(0,
              strncmp("Exception: out of bounds memory access", exception, 38));

failed_out_of_bounds:
    destroy_module_env(tmp_module_env);
}

TEST_F(TEST_SUITE_NAME, test_mem_grow_out_of_bounds)
{
    struct ret_env tmp_module_env;
    WASMFunctionInstanceCommon *func_mem_grow = nullptr;
    WASMFunctionInstanceCommon *func_mem_size = nullptr;
    bool ret = false;
    // after refactor, the 65536 pages to one 4G page optimization is removed
    // the size can be 65536 now, so use 2 + 65535 to test OOB
    uint32 argv[1] = { 65535 };
    const char *exception = nullptr;

    // Test case: module((memory 2)), memory.grow 65535, then memory.size.
    tmp_module_env = load_wasm((char *)"/mem_grow_out_of_bounds_01.wasm", 0);
    func_mem_grow = wasm_runtime_lookup_function(
        tmp_module_env.wasm_module_inst, "mem_grow");
    if (!func_mem_grow) {
        printf("\nFailed to wasm_runtime_lookup_function!\n");
        goto failed_out_of_bounds;
    }

    func_mem_size = wasm_runtime_lookup_function(
        tmp_module_env.wasm_module_inst, "mem_size");
    if (!func_mem_size) {
        printf("\nFailed to wasm_runtime_lookup_function!\n");
        goto failed_out_of_bounds;
    }

    ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func_mem_grow, 1, argv);
    if (!ret) {
        printf("\nFailed to wasm_runtime_call_wasm!\n");
        goto failed_out_of_bounds;
    }

    EXPECT_EQ(-1, argv[0]);

    ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func_mem_size, 0, argv);
    if (!ret) {
        printf("\nFailed to wasm_runtime_call_wasm!\n");
        goto failed_out_of_bounds;
    }

    EXPECT_EQ(2, argv[0]);

    // Test case: wasm_runtime_instantiate(heap_size=32768), memory.grow 65535,
    // memory.grow 1.
    destroy_module_env(tmp_module_env);
    tmp_module_env =
        load_wasm((char *)"/mem_grow_out_of_bounds_02.wasm", 32768);
    func_mem_grow = wasm_runtime_lookup_function(
        tmp_module_env.wasm_module_inst, "mem_grow");
    if (!func_mem_grow) {
        printf("\nFailed to wasm_runtime_lookup_function!\n");
        goto failed_out_of_bounds;
    }

    func_mem_size = wasm_runtime_lookup_function(
        tmp_module_env.wasm_module_inst, "mem_size");
    if (!func_mem_size) {
        printf("\nFailed to wasm_runtime_lookup_function!\n");
        goto failed_out_of_bounds;
    }

    ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func_mem_size, 0, argv);
    if (!ret) {
        printf("\nFailed to wasm_runtime_call_wasm!\n");
        goto failed_out_of_bounds;
    }
    EXPECT_EQ(2, argv[0]);

    argv[0] = 65535;
    ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func_mem_grow, 1, argv);
    if (!ret) {
        printf("\nFailed to wasm_runtime_call_wasm!\n");
        goto failed_out_of_bounds;
    }

    EXPECT_NE(2, argv[0]);

    argv[0] = 1;
    ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func_mem_grow, 1, argv);
    if (!ret) {
        printf("\nFailed to wasm_runtime_call_wasm!\n");
        goto failed_out_of_bounds;
    }

    EXPECT_EQ(2, argv[0]);

failed_out_of_bounds:
    destroy_module_env(tmp_module_env);
}
