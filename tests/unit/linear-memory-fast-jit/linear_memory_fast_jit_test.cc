/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include <memory>
#include <vector>

#include "wasm_runtime_common.h"

#define TEST_SUITE_NAME linear_memory_test_suite_fast_jit

class TEST_SUITE_NAME : public testing::Test
{
  protected:
    virtual void SetUp()
    {
        /* Two independent, pre-existing environment limitations make
         * fast-jit untestable on this host:
         *
         *  1. This test harness (tests/unit/unit_common.cmake, matching
         *     runtime_lib.cmake's WAMR_BUILD_TARGET default) hardcodes
         *     X86_64 as the JIT-codegen target regardless of actual host
         *     CPU -- executing that generated machine code natively on
         *     a non-x86 host is undefined behavior, not a graceful
         *     failure.
         *  2. Independently, WAMR's fast-jit code-cache initialization
         *     (jit_code_cache_init -> jit_compiler_init) crashes with
         *     SIGBUS/EXC_BAD_ACCESS *during runtime construction itself*
         *     on macOS (confirmed via a debugger backtrace: the crash is
         *     inside gc_init_internal's memset over the freshly-mmap'd
         *     code-cache pool, consistent with Apple Silicon's MAP_JIT /
         *     W^X hardened-runtime requirements not being satisfied by
         *     WAMR's Linux-first allocation path) -- this reproduces
         *     before any wasm module is ever loaded or executed, so it
         *     is unrelated to custom page sizes specifically and would
         *     affect ANY fast-jit test on this platform.
         *
         * Because limitation #2 crashes during WAMRRuntimeRAII's own
         * constructor, `runtime` below is deliberately NOT a plain
         * member (which would construct -- and crash -- before this
         * SetUp() body even runs) -- it is heap-allocated here, after
         * this guard, specifically so the skip can take effect first. */
#if (!defined(__x86_64__) && !defined(__i386__)) || defined(__APPLE__)
        GTEST_SKIP() << "fast-jit is not exercisable on this host: this "
                        "harness hardcodes X86_64 JIT codegen regardless "
                        "of host CPU, and/or WAMR's fast-jit code-cache "
                        "initialization is known to crash on Apple "
                        "platforms independent of that";
#endif
        runtime = std::make_unique<WAMRRuntimeRAII<512 * 1024>>();
    }
    virtual void TearDown() {}

    std::unique_ptr<WAMRRuntimeRAII<512 * 1024>> runtime;
};

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
 *    init=1, max=10, page_size_log2=page_size_log2
 *  - one exported function () -> i32 named "grow_and_size" whose body is:
 *    i32.const 2; memory.grow 0; drop; memory.size 0; end
 * (grows memory by 2 pages then returns the new page count).
 *
 * Ported from linear-memory-wasm/linear_memory_wasm_test.cc,
 * parameterized on page_size_log2 to support a two-instance,
 * two-distinct-page-size "live, not stale/baked" proof. wabt/wat2wasm has
 * no support for the custom-page-sizes proposal's memory-limits encoding,
 * so this module is hand-constructed byte-by-byte, matching the technique
 * used throughout this feature's loader- and runtime-level tests.
 */
std::vector<uint8_t>
build_custom_page_size_grow_and_size_module(uint32_t page_size_log2)
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

    /* memory section: flags=0x09, init=1, max=10, page_size_log2 */
    std::vector<uint8_t> mem_section;
    append_leb_u32(mem_section, 1); /* memory count */
    uint8_t mem_flags = kCustomPageSizeFlag | kMaxPageCountFlag;
    mem_section.push_back(mem_flags);
    append_leb_u32(mem_section, 1);  /* init */
    append_leb_u32(mem_section, 10); /* max */
    append_leb_u32(mem_section, page_size_log2);
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

#if WASM_ENABLE_MEMORY64 != 0
/*
 * memory64 + custom-page-size variant, parameterized on
 * page_size_log2 like the memory32 builder above. Memory limits
 * (init/max) are u64-LEB-encoded (memory64 index width); the exported
 * function returns i64.
 */
std::vector<uint8_t>
build_memory64_custom_page_size_grow_and_size_module(uint32_t page_size_log2)
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

    /* memory section: flags=0x0D, init=1, max=10 (u64), page_size_log2 */
    std::vector<uint8_t> mem_section;
    append_leb_u32(mem_section, 1); /* memory count */
    uint8_t mem_flags =
        kCustomPageSizeFlag | kMaxPageCountFlag | kMemory64Flag;
    mem_section.push_back(mem_flags);
    append_leb_u64(mem_section, 1);  /* init (u64) */
    append_leb_u64(mem_section, 10); /* max (u64) */
    append_leb_u32(mem_section, page_size_log2);
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
#endif /* WASM_ENABLE_MEMORY64 != 0 */

struct loaded_module {
    wasm_module_t module = nullptr;
    wasm_module_inst_t module_inst = nullptr;
    wasm_exec_env_t exec_env = nullptr;
    std::vector<uint8_t> bytes;
    char error_buf[128] = { 0 };
};

/*
 * Loads and instantiates a custom-page-size module. heap_size=0: WAMR's
 * pre-existing app-heap-insertion logic (unrelated to custom page sizes)
 * sizes extra pages to fit a requested heap using the memory's OWN page
 * size -- a nonzero heap request against a small custom page size would
 * consume many extra pages just for the heap, before this test's own
 * memory.grow(2) even runs, defeating the discriminating proofs below.
 * Zero heap keeps these tests isolated to grow/size only.
 */
loaded_module
load_custom_page_size_module(uint32_t page_size_log2)
{
    loaded_module env;
    env.bytes = build_custom_page_size_grow_and_size_module(page_size_log2);

    env.module = wasm_runtime_load(env.bytes.data(),
                                   (uint32_t)env.bytes.size(), env.error_buf,
                                   sizeof(env.error_buf));
    if (!env.module)
        return env;

    env.module_inst = wasm_runtime_instantiate(
        env.module, 16 * 1024, /* heap_size */ 0, env.error_buf,
        sizeof(env.error_buf));
    if (!env.module_inst)
        return env;

    env.exec_env = wasm_runtime_create_exec_env(env.module_inst, 16 * 1024);
    return env;
}

void
destroy_loaded_module(loaded_module &env)
{
    if (env.exec_env)
        wasm_runtime_destroy_exec_env(env.exec_env);
    if (env.module_inst)
        wasm_runtime_deinstantiate(env.module_inst);
    if (env.module)
        wasm_runtime_unload(env.module);
}

#if WASM_ENABLE_MEMORY64 != 0
loaded_module
load_memory64_custom_page_size_module(uint32_t page_size_log2)
{
    loaded_module env;
    env.bytes =
        build_memory64_custom_page_size_grow_and_size_module(page_size_log2);

    env.module = wasm_runtime_load(env.bytes.data(),
                                   (uint32_t)env.bytes.size(), env.error_buf,
                                   sizeof(env.error_buf));
    if (!env.module)
        return env;

    env.module_inst = wasm_runtime_instantiate(
        env.module, 16 * 1024, /* heap_size */ 0, env.error_buf,
        sizeof(env.error_buf));
    if (!env.module_inst)
        return env;

    env.exec_env = wasm_runtime_create_exec_env(env.module_inst, 16 * 1024);
    return env;
}
#endif /* WASM_ENABLE_MEMORY64 != 0 */

} // namespace
TEST_F(TEST_SUITE_NAME, test_custom_page_size_memory_grow_and_size_execution)
{
    loaded_module env = load_custom_page_size_module(/* page_size_log2 */ 8);
    ASSERT_NE(nullptr, env.module) << env.error_buf;
    ASSERT_NE(nullptr, env.module_inst) << env.error_buf;
    ASSERT_NE(nullptr, env.exec_env);

    /* Confirm this test is actually exercising fast-jit, not silently
     * falling back to some other running mode. With WAMR_BUILD_INTERP=0
     * and WAMR_BUILD_JIT=0, Mode_Default resolves to Mode_Fast_JIT (see
     * set_running_mode() in wasm_runtime.c) -- assert this explicitly so
     * a future build-config change can't silently defeat this story's
     * purpose without failing a test. */
    ASSERT_EQ(Mode_Fast_JIT, wasm_runtime_get_running_mode(env.module_inst));

    WASMFunctionInstanceCommon *func =
        wasm_runtime_lookup_function(env.module_inst, "grow_and_size");
    ASSERT_NE(nullptr, func);

    uint32 argv[1] = { 0 };
    bool ret = wasm_runtime_call_wasm(env.exec_env, func, 0, argv);
    ASSERT_TRUE(ret) << wasm_runtime_get_exception(env.module_inst);

    /* grow_and_size grows by 2 pages (1 -> 3) and returns memory.size --
     * identical to the classic interpreter's result. */
    EXPECT_EQ(3u, argv[0]);

    /* Public accessor consistency: post-grow page count is 3,
     * bytes-per-page is 256. */
    wasm_memory_inst_t memory_inst =
        wasm_runtime_get_memory(env.module_inst, 0);
    ASSERT_NE(nullptr, memory_inst);
    EXPECT_EQ(3u, wasm_memory_get_cur_page_count(memory_inst));
    EXPECT_EQ(256u, wasm_memory_get_bytes_per_page(memory_inst));

    /* Discriminating bounds-check proof: at 256 bytes/page, 3 pages = 768
     * bytes total -- identical discriminator to the interpreter test. */
    EXPECT_TRUE(wasm_runtime_validate_app_addr(env.module_inst, 0, 768));
    EXPECT_FALSE(wasm_runtime_validate_app_addr(env.module_inst, 0, 769));

    destroy_loaded_module(env);
}

TEST_F(TEST_SUITE_NAME, test_custom_page_size_fast_jit_reads_live_per_instance)
{
    /* Two instances, two distinct page sizes (256 bytes and 1024 bytes),
     * each grown by the same page delta -- proving fast-jit's
     * memory.grow/memory.size emission reads num_bytes_per_page live per
     * module instance, rather than baking a single value in once (e.g.
     * at first JIT-compilation/emission time) and reusing it stale for
     * every subsequent instance. */
    loaded_module env_small =
        load_custom_page_size_module(/* page_size_log2 */ 8); /* 256 B */
    ASSERT_NE(nullptr, env_small.module) << env_small.error_buf;
    ASSERT_NE(nullptr, env_small.module_inst) << env_small.error_buf;

    loaded_module env_large =
        load_custom_page_size_module(/* page_size_log2 */ 10); /* 1024 B */
    ASSERT_NE(nullptr, env_large.module) << env_large.error_buf;
    ASSERT_NE(nullptr, env_large.module_inst) << env_large.error_buf;

    WASMFunctionInstanceCommon *func_small = wasm_runtime_lookup_function(
        env_small.module_inst, "grow_and_size");
    ASSERT_NE(nullptr, func_small);
    WASMFunctionInstanceCommon *func_large = wasm_runtime_lookup_function(
        env_large.module_inst, "grow_and_size");
    ASSERT_NE(nullptr, func_large);

    uint32 argv_small[1] = { 0 };
    ASSERT_TRUE(wasm_runtime_call_wasm(env_small.exec_env, func_small, 0,
                                       argv_small))
        << wasm_runtime_get_exception(env_small.module_inst);
    uint32 argv_large[1] = { 0 };
    ASSERT_TRUE(wasm_runtime_call_wasm(env_large.exec_env, func_large, 0,
                                       argv_large))
        << wasm_runtime_get_exception(env_large.module_inst);

    /* Both grow by 2 pages (1 -> 3): memory.size (page count) is
     * identical for both regardless of page size -- as expected, since
     * memory.size is defined in page units. The real proof is in the
     * *byte-level* bounds checks below, which diverge per instance. */
    EXPECT_EQ(3u, argv_small[0]);
    EXPECT_EQ(3u, argv_large[0]);

    wasm_memory_inst_t mem_small =
        wasm_runtime_get_memory(env_small.module_inst, 0);
    wasm_memory_inst_t mem_large =
        wasm_runtime_get_memory(env_large.module_inst, 0);
    ASSERT_NE(nullptr, mem_small);
    ASSERT_NE(nullptr, mem_large);
    EXPECT_EQ(256u, wasm_memory_get_bytes_per_page(mem_small));
    EXPECT_EQ(1024u, wasm_memory_get_bytes_per_page(mem_large));

    /* Small instance: 3 pages * 256 B = 768 B total. */
    EXPECT_TRUE(wasm_runtime_validate_app_addr(env_small.module_inst, 0, 768));
    EXPECT_FALSE(
        wasm_runtime_validate_app_addr(env_small.module_inst, 0, 769));

    /* Large instance: 3 pages * 1024 B = 3072 B total -- if fast-jit had
     * baked a single (e.g. the first-seen 256 B) page size, this larger
     * instance would incorrectly reject an in-bounds access at 769 (as
     * proven safe for the SMALL instance above) or, worse, incorrectly
     * accept access far beyond 3072. Both instances' bounds must be
     * independently correct for their own declared page size. */
    EXPECT_TRUE(wasm_runtime_validate_app_addr(env_large.module_inst, 0, 769));
    EXPECT_TRUE(
        wasm_runtime_validate_app_addr(env_large.module_inst, 0, 3072));
    EXPECT_FALSE(
        wasm_runtime_validate_app_addr(env_large.module_inst, 0, 3073));

    destroy_loaded_module(env_large);
    destroy_loaded_module(env_small);
}

#if WASM_ENABLE_MEMORY64 != 0
TEST_F(TEST_SUITE_NAME,
       test_memory64_custom_page_size_memory_grow_and_size_execution)
{
    loaded_module env =
        load_memory64_custom_page_size_module(/* page_size_log2 */ 8);
    ASSERT_NE(nullptr, env.module) << env.error_buf;
    ASSERT_NE(nullptr, env.module_inst) << env.error_buf;
    ASSERT_NE(nullptr, env.exec_env);

    ASSERT_EQ(Mode_Fast_JIT, wasm_runtime_get_running_mode(env.module_inst));

    WASMFunctionInstanceCommon *func = wasm_runtime_lookup_function(
        env.module_inst, "grow_and_size_mem64");
    ASSERT_NE(nullptr, func);

    /* i64 result occupies two argv slots (WAMR ABI). */
    uint32 argv[2] = { 0, 0 };
    bool ret = wasm_runtime_call_wasm(env.exec_env, func, 0, argv);
    ASSERT_TRUE(ret) << wasm_runtime_get_exception(env.module_inst);

    uint64 result;
    memcpy(&result, argv, sizeof(uint64));
    EXPECT_EQ(3u, result);

    wasm_memory_inst_t memory_inst =
        wasm_runtime_get_memory(env.module_inst, 0);
    ASSERT_NE(nullptr, memory_inst);
    EXPECT_EQ(3u, wasm_memory_get_cur_page_count(memory_inst));
    EXPECT_EQ(256u, wasm_memory_get_bytes_per_page(memory_inst));

    EXPECT_TRUE(wasm_runtime_validate_app_addr(env.module_inst, 0, 768));
    EXPECT_FALSE(wasm_runtime_validate_app_addr(env.module_inst, 0, 769));

    destroy_loaded_module(env);
}
#endif /* WASM_ENABLE_MEMORY64 != 0 */
