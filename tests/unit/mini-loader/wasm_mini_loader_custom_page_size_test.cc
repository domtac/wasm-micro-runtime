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
     * instead of an out-of-bounds read. Mirrors Story 1.2's equivalent
     * wasm_loader.c coverage (Verification Gap review parity check). */
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
