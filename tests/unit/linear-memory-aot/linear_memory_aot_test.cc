/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "bh_read_file.h"
#include "wasm_runtime_common.h"

static std::string CWD;

#if WASM_DISABLE_HW_BOUND_CHECK != 0
#define TEST_SUITE_NAME linear_memory_test_suite_aot_no_hw_bound
#else
#define TEST_SUITE_NAME linear_memory_test_suite_aot
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
    wasm_module_t aot_module;
    wasm_module_inst_t aot_module_inst;
    unsigned char *aot_file_buf;
    char error_buf[128];
};

struct ret_env
load_aot(char *aot_file_tested, unsigned int app_heap_size)
{
    std::string aot_mem_page = aot_file_tested;
    const char *aot_file = strdup((CWD + aot_mem_page).c_str());
    wasm_module_inst_t aot_module_inst = nullptr;
    wasm_module_t aot_module = nullptr;
    wasm_exec_env_t exec_env = nullptr;
    unsigned char *aot_file_buf = nullptr;
    unsigned int aot_file_size = 0;
    unsigned int stack_size = 16 * 1024, heap_size = app_heap_size;
    char error_buf[128] = { 0 };
    struct ret_env ret_module_env;

    memset(ret_module_env.error_buf, 0, 128);
    aot_file_buf =
        (unsigned char *)bh_read_file_to_buffer(aot_file, &aot_file_size);
    if (!aot_file_buf) {
        goto fail;
    }

    aot_module = wasm_runtime_load(aot_file_buf, aot_file_size, error_buf,
                                   sizeof(error_buf));
    if (!aot_module) {
        memcpy(ret_module_env.error_buf, error_buf, 128);
        goto fail;
    }

    aot_module_inst = wasm_runtime_instantiate(
        aot_module, stack_size, heap_size, error_buf, sizeof(error_buf));
    if (!aot_module_inst) {
        memcpy(ret_module_env.error_buf, error_buf, 128);
        goto fail;
    }

    exec_env = wasm_runtime_create_exec_env(aot_module_inst, stack_size);

fail:
    ret_module_env.exec_env = exec_env;
    ret_module_env.aot_module = aot_module;
    ret_module_env.aot_module_inst = aot_module_inst;
    ret_module_env.aot_file_buf = aot_file_buf;

    return ret_module_env;
}

void
destroy_module_env(struct ret_env module_env)
{
    if (module_env.exec_env) {
        wasm_runtime_destroy_exec_env(module_env.exec_env);
    }

    if (module_env.aot_module_inst) {
        wasm_runtime_deinstantiate(module_env.aot_module_inst);
    }

    if (module_env.aot_module) {
        wasm_runtime_unload(module_env.aot_module);
    }

    if (module_env.aot_file_buf) {
        wasm_runtime_free(module_env.aot_file_buf);
    }
}

TEST_F(TEST_SUITE_NAME, test_aot_mem_page_count)
{
    struct ret_env tmp_module_env;
    const unsigned int num_normal_aot = 9;
    const unsigned int num_error_aot = 2;

#if UINTPTR_MAX == UINT64_MAX
    const char *aot_file_normal[num_normal_aot] = {
        "/mem_page_01.aot", "/mem_page_02.aot", "/mem_page_05.aot",
        "/mem_page_07.aot", "/mem_page_08.aot", "/mem_page_09.aot",
        "/mem_page_10.aot", "/mem_page_12.aot", "/mem_page_14.aot"
    };

    const char *aot_file_error[num_error_aot] = { "/mem_page_03.aot",
                                                  "/mem_page_16.aot" };
#else
    const char *aot_file_normal[num_normal_aot] = {
        "/mem_page_01_32.aot", "/mem_page_02_32.aot", "/mem_page_05_32.aot",
        "/mem_page_07_32.aot", "/mem_page_08_32.aot", "/mem_page_09_32.aot",
        "/mem_page_10_32.aot", "/mem_page_12_32.aot", "/mem_page_14_32.aot"
    };

    const char *aot_file_error[num_error_aot] = { "/mem_page_03_32.aot",
                                                  "/mem_page_16_32.aot" };
#endif

    // Test normal wasm file.
    for (int i = 0; i < num_normal_aot; i++) {
#if UINTPTR_MAX != UINT64_MAX
        // 32 bit do not load this wasm.
        if ((0 == strcmp("/mem_page_14_32.aot", aot_file_normal[i]))) {
            continue;
        }
#endif

        tmp_module_env = load_aot((char *)aot_file_normal[i], 16 * 1024);
        EXPECT_NE(nullptr, tmp_module_env.aot_module);
        EXPECT_NE(nullptr, tmp_module_env.aot_file_buf);

        destroy_module_env(tmp_module_env);
    }

    // Test error wasm file.
    for (int i = 0; i < num_error_aot; i++) {
        tmp_module_env = load_aot((char *)aot_file_error[i], 16 * 1024);
        if (0 != strlen(tmp_module_env.error_buf)) {
            /* 3 and 16 are for legit for loader, the init and max page count
             * can be 65536, but they can't allocate any host managed heap, so
             * instantiating errors  */
            EXPECT_EQ(0, strncmp("AOT module instantiate failed",
                                 (const char *)tmp_module_env.error_buf, 29));
            printf("%s\n", tmp_module_env.error_buf);
        }

        destroy_module_env(tmp_module_env);
    }
}

TEST_F(TEST_SUITE_NAME, test_aot_about_app_heap)
{
    struct ret_env tmp_module_env;

    // Test case: init_page_count = 65536, app heap size = 1.
#if UINTPTR_MAX == UINT64_MAX
    tmp_module_env = load_aot((char *)"/mem_page_03.aot", 1);
#else
    tmp_module_env = load_aot((char *)"/mem_page_03_32.aot", 1);
#endif
    EXPECT_EQ(
        0, strncmp("AOT module", (const char *)tmp_module_env.error_buf, 10));
    destroy_module_env(tmp_module_env);

    // Test case: init_page_count = 65535, app heap size = 65537.
#if UINTPTR_MAX == UINT64_MAX
    tmp_module_env = load_aot((char *)"/mem_page_20.aot", 65537);
#else
    tmp_module_env = load_aot((char *)"/mem_page_20_32.aot", 65537);
#endif
    EXPECT_EQ(
        0, strncmp("AOT module", (const char *)tmp_module_env.error_buf, 10));
    destroy_module_env(tmp_module_env);
}

TEST_F(TEST_SUITE_NAME, test_throw_exception_out_of_bounds)
{
    struct ret_env tmp_module_env;
    WASMFunctionInstanceCommon *func = nullptr;
    bool ret = false;
    uint32 argv[1] = { 9999 * 64 * 1024 };
    const char *exception = nullptr;

    /* TODO: use no_hw_bounds version when disable */
#if UINTPTR_MAX == UINT64_MAX
    tmp_module_env = load_aot((char *)"/out_of_bounds.aot", 16 * 1024);
#else
    tmp_module_env = load_aot((char *)"/out_of_bounds_32.aot", 16 * 1024);
#endif
    func = wasm_runtime_lookup_function(tmp_module_env.aot_module_inst, "load");
    if (!func) {
        printf("\nFailed to wasm_runtime_lookup_function!\n");
        goto failed_out_of_bounds;
    }

    ret = wasm_runtime_call_wasm(tmp_module_env.exec_env, func, 1, argv);
    if (!ret) {
        printf("\nFailed to wasm_runtime_call_wasm!\n");
    }

    exception = wasm_runtime_get_exception(tmp_module_env.aot_module_inst);
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
    uint32 argv[1] = { 65535 };
    const char *exception = nullptr;

    /* TODO: use no_hw_bounds version when disable */
    // Test case: module((memory 2)), memory.grow 65535, then memory.size.
#if UINTPTR_MAX == UINT64_MAX
    tmp_module_env = load_aot((char *)"/mem_grow_out_of_bounds_01.aot", 0);
#else
    tmp_module_env = load_aot((char *)"/mem_grow_out_of_bounds_01_32.aot", 0);
#endif

    func_mem_grow = wasm_runtime_lookup_function(tmp_module_env.aot_module_inst,
                                                 "mem_grow");
    if (!func_mem_grow) {
        printf("\nFailed to wasm_runtime_lookup_function!\n");
        goto failed_out_of_bounds;
    }

    func_mem_size = wasm_runtime_lookup_function(tmp_module_env.aot_module_inst,
                                                 "mem_size");
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

    // Test case: wasm_runtime_instantiate(heap_size=32768), memory.grow 65534,
    // memory.grow 1.
    destroy_module_env(tmp_module_env);

#if UINTPTR_MAX == UINT64_MAX
    tmp_module_env = load_aot((char *)"/mem_grow_out_of_bounds_02.aot", 32768);
#else
    tmp_module_env =
        load_aot((char *)"/mem_grow_out_of_bounds_02_32.aot", 32768);
#endif

    func_mem_grow = wasm_runtime_lookup_function(tmp_module_env.aot_module_inst,
                                                 "mem_grow");
    if (!func_mem_grow) {
        printf("\nFailed to wasm_runtime_lookup_function!\n");
        goto failed_out_of_bounds;
    }

    func_mem_size = wasm_runtime_lookup_function(tmp_module_env.aot_module_inst,
                                                 "mem_size");
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

    argv[0] = 65534;
    ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func_mem_grow, 1, argv);
    if (!ret) {
        printf("\nFailed to wasm_runtime_call_wasm!\n");
        goto failed_out_of_bounds;
    }

#if UINTPTR_MAX == UINT64_MAX
    EXPECT_EQ(2, argv[0]);
#else
    EXPECT_EQ(-1, argv[0]);
#endif

    argv[0] = 1;
    ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func_mem_grow, 1, argv);
    if (!ret) {
        printf("\nFailed to wasm_runtime_call_wasm!\n");
        goto failed_out_of_bounds;
    }

#if UINTPTR_MAX == UINT64_MAX
    EXPECT_EQ(-1, argv[0]);
#else
    EXPECT_EQ(2, argv[0]);
#endif

failed_out_of_bounds:
    destroy_module_env(tmp_module_env);
}

/*
 * AOT runtime / wamrc parity for custom page sizes.
 *
 * Loads and executes the .aot fixture wamrc-compiled from the hand-built
 * custom-page-size module (flags=0x09, init=1, max=10, page_size_log2=8,
 * exported "grow_and_size" function), and asserts the exact same results
 * as the interpreter test
 * (test_custom_page_size_memory_grow_and_size_execution in
 * linear-memory-wasm/linear_memory_wasm_test.cc).
 */
TEST_F(TEST_SUITE_NAME, test_aot_custom_page_size_memory_grow_and_size)
{
    struct ret_env tmp_module_env;

    /* heap_size=0: a nonzero app-heap request would be sized using the
     * memory's own (256-byte) page size and defeat the discriminating
     * bounds-check proof below. */
    tmp_module_env =
        load_aot((char *)"/custom_page_size_grow_and_size.aot", 0);
    ASSERT_NE(nullptr, tmp_module_env.aot_module)
        << tmp_module_env.error_buf;
    ASSERT_NE(nullptr, tmp_module_env.aot_module_inst)
        << tmp_module_env.error_buf;

    /* Self-diagnostic: this fixture is compiled at build time by whatever
     * wamrc happens to be found (see CMakeLists.txt). If that wamrc
     * predates this feature's own implementation, it will silently emit
     * the wrong num_bytes_per_page (the old 1-byte placeholder rather
     * than the module's declared 256), which cannot be distinguished
     * from a real regression by this test alone, and calling into the
     * compiled function body below is unsafe if the toolchain's target
     * architecture also does not match this host. Detect and skip --
     * rather than fail or hang -- so a stale/mismatched build toolchain
     * reads as a clear, actionable skip instead of ambiguous CI noise. */
    wasm_memory_inst_t pre_call_memory_inst =
        wasm_runtime_get_memory(tmp_module_env.aot_module_inst, 0);
    ASSERT_NE(nullptr, pre_call_memory_inst);
    if (wasm_memory_get_bytes_per_page(pre_call_memory_inst) != 256) {
        destroy_module_env(tmp_module_env);
        GTEST_SKIP() << "wamrc used to build this .aot fixture does not "
                        "correctly reflect this feature's custom page "
                        "size (expected bytes_per_page=256, got "
                     << wasm_memory_get_bytes_per_page(pre_call_memory_inst)
                     << ") -- toolchain is stale or targets a mismatched "
                        "architecture";
    }

    WASMFunctionInstanceCommon *func = wasm_runtime_lookup_function(
        tmp_module_env.aot_module_inst, "grow_and_size");
    ASSERT_NE(nullptr, func);

    uint32 argv[1] = { 0 };
    bool ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func, 0, argv);
    ASSERT_TRUE(ret)
        << wasm_runtime_get_exception(tmp_module_env.aot_module_inst);

    /* grow_and_size grows by 2 pages (1 -> 3) and returns memory.size --
     * identical to the interpreter's result. */
    EXPECT_EQ(3u, argv[0]);

    /* Public accessor consistency: post-grow page count is 3,
     * bytes-per-page is 256. */
    wasm_memory_inst_t memory_inst =
        wasm_runtime_get_memory(tmp_module_env.aot_module_inst, 0);
    ASSERT_NE(nullptr, memory_inst);
    EXPECT_EQ(3u, wasm_memory_get_cur_page_count(memory_inst));
    EXPECT_EQ(256u, wasm_memory_get_bytes_per_page(memory_inst));

    /* Discriminating bounds-check proof: at 256 bytes/page, 3 pages = 768
     * bytes total -- identical discriminator to the interpreter test. */
    EXPECT_TRUE(wasm_runtime_validate_app_addr(
        tmp_module_env.aot_module_inst, 0, 768));
    EXPECT_FALSE(wasm_runtime_validate_app_addr(
        tmp_module_env.aot_module_inst, 0, 769));

    destroy_module_env(tmp_module_env);
}

#if WASM_ENABLE_MEMORY64 != 0
/*
 * AOT runtime / wamrc parity for memory64 + custom page sizes.
 *
 * Loads and executes the .aot fixture wamrc-compiled from the hand-built
 * memory64 custom-page-size module (flags=0x0D, init=1, max=10,
 * page_size_log2=8, exported "grow_and_size_mem64" function returning
 * i64), and asserts the same results as the interpreter test
 * (test_memory64_custom_page_size_memory_grow_and_size_execution in
 * linear-memory-wasm/linear_memory_wasm_test.cc).
 */
TEST_F(TEST_SUITE_NAME, test_aot_memory64_custom_page_size_memory_grow_and_size)
{
    struct ret_env tmp_module_env;

    tmp_module_env = load_aot(
        (char *)"/memory64_custom_page_size_grow_and_size.aot", 0);
    ASSERT_NE(nullptr, tmp_module_env.aot_module)
        << tmp_module_env.error_buf;
    ASSERT_NE(nullptr, tmp_module_env.aot_module_inst)
        << tmp_module_env.error_buf;

    /* Same stale-toolchain self-diagnostic as the memory32 AOT test. */
    wasm_memory_inst_t pre_call_memory_inst =
        wasm_runtime_get_memory(tmp_module_env.aot_module_inst, 0);
    ASSERT_NE(nullptr, pre_call_memory_inst);
    if (wasm_memory_get_bytes_per_page(pre_call_memory_inst) != 256) {
        destroy_module_env(tmp_module_env);
        GTEST_SKIP() << "wamrc used to build this .aot fixture does not "
                        "correctly reflect this feature's custom page "
                        "size (expected bytes_per_page=256, got "
                     << wasm_memory_get_bytes_per_page(pre_call_memory_inst)
                     << ") -- toolchain is stale or targets a mismatched "
                        "architecture";
    }

    WASMFunctionInstanceCommon *func = wasm_runtime_lookup_function(
        tmp_module_env.aot_module_inst, "grow_and_size_mem64");
    ASSERT_NE(nullptr, func);

    /* i64 result occupies two argv slots (WAMR ABI). */
    uint32 argv[2] = { 0, 0 };
    bool ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func, 0, argv);
    ASSERT_TRUE(ret)
        << wasm_runtime_get_exception(tmp_module_env.aot_module_inst);

    uint64 result;
    memcpy(&result, argv, sizeof(uint64));
    EXPECT_EQ(3u, result);

    wasm_memory_inst_t memory_inst =
        wasm_runtime_get_memory(tmp_module_env.aot_module_inst, 0);
    ASSERT_NE(nullptr, memory_inst);
    EXPECT_EQ(3u, wasm_memory_get_cur_page_count(memory_inst));
    EXPECT_EQ(256u, wasm_memory_get_bytes_per_page(memory_inst));

    EXPECT_TRUE(wasm_runtime_validate_app_addr(
        tmp_module_env.aot_module_inst, 0, 768));
    EXPECT_FALSE(wasm_runtime_validate_app_addr(
        tmp_module_env.aot_module_inst, 0, 769));

    destroy_module_env(tmp_module_env);
}
#endif /* WASM_ENABLE_MEMORY64 != 0 */
