/*
 * Copyright (C) 2024 Amazon Inc.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include <cstring>
#include <vector>

extern "C" {
#include "wasm.h"
#include "wasm_loader.h"
#include "wasm_export.h"
#include "wasm_runtime_common.h"
}

namespace {

/* Wire-format flag bits, mirroring wasm.h */
constexpr uint8_t kMaxPageCountFlag = 0x01;
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

/* Builds a minimal module with a single declared memory (memory section). */
std::vector<uint8_t>
build_declared_memory_module(uint8_t mem_flags, uint32_t init_page_count,
                             uint32_t max_page_count,
                             uint32_t page_size_log2)
{
    std::vector<uint8_t> module;
    append_header(module);

    std::vector<uint8_t> mem_section;
    append_leb_u32(mem_section, 1); /* memory count */
    mem_section.push_back(mem_flags);
    append_leb_u32(mem_section, init_page_count);
    if (mem_flags & kMaxPageCountFlag) {
        append_leb_u32(mem_section, max_page_count);
    }
    if (mem_flags & kCustomPageSizeFlag) {
        append_leb_u32(mem_section, page_size_log2);
    }

    append_section(module, SECTION_TYPE_MEMORY, mem_section);
    return module;
}

/* Builds a minimal module with a single imported memory (import section). */
std::vector<uint8_t>
build_imported_memory_module(uint8_t mem_flags, uint32_t init_page_count,
                             uint32_t max_page_count,
                             uint32_t page_size_log2)
{
    std::vector<uint8_t> module;
    append_header(module);

    std::vector<uint8_t> import_section;
    append_leb_u32(import_section, 1); /* import count */

    const char *module_name = "env";
    const char *field_name = "mem";
    append_leb_u32(import_section, (uint32_t)strlen(module_name));
    import_section.insert(import_section.end(), module_name,
                          module_name + strlen(module_name));
    append_leb_u32(import_section, (uint32_t)strlen(field_name));
    import_section.insert(import_section.end(), field_name,
                          field_name + strlen(field_name));
    import_section.push_back(IMPORT_KIND_MEMORY);

    import_section.push_back(mem_flags);
    append_leb_u32(import_section, init_page_count);
    if (mem_flags & kMaxPageCountFlag) {
        append_leb_u32(import_section, max_page_count);
    }
    if (mem_flags & kCustomPageSizeFlag) {
        append_leb_u32(import_section, page_size_log2);
    }

    append_section(module, SECTION_TYPE_IMPORT, import_section);
    return module;
}

/* Same as build_declared_memory_module, but with a type/function/code
 * section containing a `memory.size` opcode -- required so that
 * module->possible_memory_grow is set, which prevents an unrelated
 * pre-existing "shrink to single big page" loader optimization from
 * collapsing max_page_count down to 1 regardless of the declared value,
 * masking assertions about the actual parsed max_page_count (mirrors the
 * same workaround already used in the full-loader's parity test file). */
std::vector<uint8_t>
build_declared_memory_module_with_memory_size_op(
    uint8_t mem_flags, uint32_t init_page_count, uint32_t max_page_count,
    uint32_t page_size_log2)
{
    std::vector<uint8_t> module;
    append_header(module);

    /* type section: one func type () -> () */
    std::vector<uint8_t> type_section;
    append_leb_u32(type_section, 1);
    type_section.push_back(0x60);
    type_section.push_back(0x00);
    type_section.push_back(0x00);
    append_section(module, /* SECTION_TYPE_TYPE */ 1, type_section);

    /* function section: one function, type index 0 */
    std::vector<uint8_t> function_section;
    append_leb_u32(function_section, 1);
    append_leb_u32(function_section, 0);
    append_section(module, /* SECTION_TYPE_FUNC */ 3, function_section);

    std::vector<uint8_t> mem_section;
    append_leb_u32(mem_section, 1); /* memory count */
    mem_section.push_back(mem_flags);
    append_leb_u32(mem_section, init_page_count);
    if (mem_flags & kMaxPageCountFlag) {
        append_leb_u32(mem_section, max_page_count);
    }
    if (mem_flags & kCustomPageSizeFlag) {
        append_leb_u32(mem_section, page_size_log2);
    }
    append_section(module, SECTION_TYPE_MEMORY, mem_section);

    /* code section: one body = memory.size(0x3f) memidx(0x00) drop(0x1a)
     * end(0x0b), with 0 local decls */
    std::vector<uint8_t> code_section;
    append_leb_u32(code_section, 1); /* function body count */
    std::vector<uint8_t> body;
    body.push_back(0x00); /* local decl count */
    body.push_back(0x3f); /* memory.size */
    body.push_back(0x00); /* memidx */
    body.push_back(0x1a); /* drop */
    body.push_back(0x0b); /* end */
    append_leb_u32(code_section, (uint32_t)body.size());
    code_section.insert(code_section.end(), body.begin(), body.end());
    append_section(module, /* SECTION_TYPE_CODE */ 10, code_section);

    return module;
}

class WasmRuntimeEnvironment : public ::testing::Environment {
public:
    void
    SetUp() override
    {
        ASSERT_TRUE(wasm_runtime_init());
    }

    void
    TearDown() override
    {
        wasm_runtime_destroy();
    }
};

::testing::Environment *const wasm_runtime_env =
    ::testing::AddGlobalTestEnvironment(new WasmRuntimeEnvironment());

} // namespace

/* NOTE: `wasm_loader_load` is the same public entry point whether the
 * underlying implementation compiled in is wasm_loader.c or
 * wasm_mini_loader.c (selected at build time via WAMR_BUILD_FAST_INTERP);
 * this test binary is built with WAMR_BUILD_FAST_INTERP=1 so it exercises
 * wasm_mini_loader.c's load_memory/load_memory_import. */

TEST(wasm_mini_loader_custom_page_size, declared_memory_valid_page_size_log2)
{
    auto module_bytes =
        build_declared_memory_module(kCustomPageSizeFlag, 1, 0, 8);

    char error_buf[128] = { 0 };
    LoadArgs args = {};
    WASMModule *module =
        wasm_loader_load(module_bytes.data(), (uint32_t)module_bytes.size(),
#if WASM_ENABLE_MULTI_MODULE != 0
                         true,
#endif
                         &args, error_buf, sizeof(error_buf));
    ASSERT_NE(module, nullptr) << error_buf;
    ASSERT_EQ(module->memory_count, 1u);
    EXPECT_EQ(module->memories[0].num_bytes_per_page, 256u);

    wasm_loader_unload(module);
}

TEST(wasm_mini_loader_custom_page_size, import_memory_valid_page_size_log2)
{
    auto module_bytes =
        build_imported_memory_module(kCustomPageSizeFlag, 1, 0, 8);

    char error_buf[128] = { 0 };
    LoadArgs args = {};
    WASMModule *module =
        wasm_loader_load(module_bytes.data(), (uint32_t)module_bytes.size(),
#if WASM_ENABLE_MULTI_MODULE != 0
                         true,
#endif
                         &args, error_buf, sizeof(error_buf));
    ASSERT_NE(module, nullptr) << error_buf;
    ASSERT_EQ(module->import_memory_count, 1u);
    EXPECT_EQ(module->import_memories[0].u.memory.mem_type.num_bytes_per_page,
             256u);

    wasm_loader_unload(module);
}

TEST(wasm_mini_loader_custom_page_size, invalid_page_size_log2_fails)
{
    auto declared_bytes =
        build_declared_memory_module(kCustomPageSizeFlag, 1, 0, 17);
    char error_buf1[128] = { 0 };
    LoadArgs args1 = {};
    WASMModule *declared_module = wasm_loader_load(
        declared_bytes.data(), (uint32_t)declared_bytes.size(),
#if WASM_ENABLE_MULTI_MODULE != 0
        true,
#endif
        &args1, error_buf1, sizeof(error_buf1));
    EXPECT_EQ(declared_module, nullptr);
    EXPECT_GT(strlen(error_buf1), 0u);
    if (declared_module)
        wasm_loader_unload(declared_module);

    auto imported_bytes =
        build_imported_memory_module(kCustomPageSizeFlag, 1, 0, 17);
    char error_buf2[128] = { 0 };
    LoadArgs args2 = {};
    WASMModule *imported_module = wasm_loader_load(
        imported_bytes.data(), (uint32_t)imported_bytes.size(),
#if WASM_ENABLE_MULTI_MODULE != 0
        true,
#endif
        &args2, error_buf2, sizeof(error_buf2));
    EXPECT_EQ(imported_module, nullptr);
    EXPECT_GT(strlen(error_buf2), 0u);
    if (imported_module)
        wasm_loader_unload(imported_module);
}

TEST(wasm_mini_loader_custom_page_size, no_custom_page_size_flag_regression)
{
    /* No CUSTOM_PAGE_SIZE_FLAG: behavior must match pre-existing code,
     * i.e. default 64KiB pages. */
    auto module_bytes = build_declared_memory_module(0, 1, 0, 0);

    char error_buf[128] = { 0 };
    LoadArgs args = {};
    WASMModule *module =
        wasm_loader_load(module_bytes.data(), (uint32_t)module_bytes.size(),
#if WASM_ENABLE_MULTI_MODULE != 0
                         true,
#endif
                         &args, error_buf, sizeof(error_buf));
    ASSERT_NE(module, nullptr) << error_buf;
    ASSERT_EQ(module->memory_count, 1u);
    EXPECT_EQ(module->memories[0].num_bytes_per_page, 65536u);

    wasm_loader_unload(module);
}

TEST(wasm_mini_loader_custom_page_size, truncated_buffer_during_page_size_decode)
{
    /* CUSTOM_PAGE_SIZE_FLAG is set, but the page_size_log2 LEB byte is
     * never appended, so the section body ends exactly where
     * load_memory()'s read_leb_uint32(p, p_end, page_size_log2) would
     * read -- p == p_end, triggering the macro's internal `goto fail`
     * instead of an out-of-bounds read. Mirrors the full loader's
     * equivalent wasm_loader.c coverage. */
    std::vector<uint8_t> module;
    append_header(module);

    std::vector<uint8_t> mem_section;
    append_leb_u32(mem_section, 1); /* memory count */
    mem_section.push_back(kCustomPageSizeFlag); /* flag set... */
    append_leb_u32(mem_section, 1); /* init_page_count */
    /* ...but no page_size_log2 byte appended: truncated. */
    append_section(module, SECTION_TYPE_MEMORY, mem_section);

    char error_buf[128] = { 0 };
    LoadArgs args = {};
    WASMModule *loaded_module =
        wasm_loader_load(module.data(), (uint32_t)module.size(),
#if WASM_ENABLE_MULTI_MODULE != 0
                         true,
#endif
                         &args, error_buf, sizeof(error_buf));

    EXPECT_EQ(loaded_module, nullptr);
    EXPECT_GT(strlen(error_buf), 0u);

    if (loaded_module) {
        wasm_loader_unload(loaded_module);
    }
}

TEST(wasm_mini_loader_custom_page_size,
    max_page_count_scaled_for_custom_page_size)
{
    /* page_size_log2 = 8 -> 256 bytes/page.
     * Correct scaled cap: DEFAULT_MAX_PAGES(65536) * (65536/256) = 16,777,216.
     * Stale (pre-fix, mini loader had NO scaling at all) cap: DEFAULT_MAX_PAGES
     * (65536), i.e. entirely unscaled. Choosing a declared value strictly
     * between these two caps means the test FAILS if the mini loader's
     * scaling regresses back to none/wrong, and PASSES only when it
     * correctly matches the full loader's scaling (mini-loader parity
     * requirement). */
    uint32_t page_size_log2 = 8;
    uint32_t declared_max_page_count = 100000;

    auto module_bytes = build_declared_memory_module_with_memory_size_op(
        kCustomPageSizeFlag | kMaxPageCountFlag, 1, declared_max_page_count,
        page_size_log2);

    char error_buf[128] = { 0 };
    LoadArgs args = {};
    WASMModule *module =
        wasm_loader_load(module_bytes.data(), (uint32_t)module_bytes.size(),
#if WASM_ENABLE_MULTI_MODULE != 0
                         true,
#endif
                         &args, error_buf, sizeof(error_buf));
    ASSERT_NE(module, nullptr) << error_buf;
    ASSERT_EQ(module->memory_count, 1u);
    EXPECT_EQ(module->memories[0].num_bytes_per_page, 256u);
    EXPECT_EQ(module->memories[0].max_page_count, declared_max_page_count);

    wasm_loader_unload(module);
}
