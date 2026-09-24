/*
 * Copyright (C) 2024 Amazon Inc.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include <cstring>

extern "C" {
#include "wasm_loader_common.h"
}

#if WASM_ENABLE_CUSTOM_PAGE_SIZE != 0
TEST(wasm_loader_common, page_size_log2_zero)
{
    uint32 num_bytes_per_page = 0;
    bool ret = wasm_check_page_size_log2(0, &num_bytes_per_page, NULL, 0,
                                         false);
    EXPECT_TRUE(ret);
    EXPECT_EQ(num_bytes_per_page, 1u);
}

TEST(wasm_loader_common, page_size_log2_sixteen)
{
    uint32 num_bytes_per_page = 0;
    bool ret = wasm_check_page_size_log2(16, &num_bytes_per_page, NULL, 0,
                                         false);
    EXPECT_TRUE(ret);
    EXPECT_EQ(num_bytes_per_page, 65536u);
}

TEST(wasm_loader_common, page_size_log2_seventeen_invalid)
{
    uint32 num_bytes_per_page = 0;
    char error_buf[128] = { 0 };
    bool ret = wasm_check_page_size_log2(17, &num_bytes_per_page, error_buf,
                                         sizeof(error_buf), false);
    EXPECT_FALSE(ret);
    EXPECT_GT(strlen(error_buf), 0u);
}

TEST(wasm_loader_common, page_size_log2_null_output_param_invalid)
{
    char error_buf[128] = { 0 };
    bool ret =
        wasm_check_page_size_log2(0, NULL, error_buf, sizeof(error_buf), false);
    EXPECT_FALSE(ret);
    EXPECT_GT(strlen(error_buf), 0u);
}

TEST(wasm_loader_common, page_size_log2_eight)
{
    uint32 num_bytes_per_page = 0;
    bool ret = wasm_check_page_size_log2(8, &num_bytes_per_page, NULL, 0,
                                         false);
    EXPECT_TRUE(ret);
    EXPECT_EQ(num_bytes_per_page, 256u);
}

TEST(wasm_loader_common, page_size_log2_uint32_max_invalid)
{
    uint32 num_bytes_per_page = 0;
    bool ret = wasm_check_page_size_log2(UINT32_MAX, &num_bytes_per_page,
                                         NULL, 0, false);
    EXPECT_FALSE(ret);
}
#endif /* WASM_ENABLE_CUSTOM_PAGE_SIZE != 0 */

TEST(wasm_loader_common, max_page_count_default_page_size)
{
    /* num_bytes_per_page = 65536 (default), non-memory64 */
    EXPECT_EQ(wasm_calculate_max_page_count(false, 65536u), 65536u);
}

TEST(wasm_loader_common, max_page_count_smallest_custom_page_size_overflow)
{
    /* multiplier = 65536, DEFAULT_MAX_PAGES(65536) * 65536 = 2^32,
     * must clamp to UINT32_MAX (today's ==0 check happens to also catch
     * this exact-wrap case, the new logic must too) */
    EXPECT_EQ(wasm_calculate_max_page_count(false, 1u), UINT32_MAX);
}

#if WASM_ENABLE_MEMORY64 != 0
TEST(wasm_loader_common, max_page_count_memory64_smallest_custom_page_size)
{
    /* is_memory64=true, num_bytes_per_page=1 (multiplier=65536):
     * DEFAULT_MEM64_MAX_PAGES(UINT32_MAX) * 65536 overflows to a
     * large-but-not-zero garbage value under the old ==0-only logic;
     * this is the discriminating proof the new uint64-intermediate
     * clamp must catch it. */
    EXPECT_EQ(wasm_calculate_max_page_count(true, 1u), UINT32_MAX);
}

TEST(wasm_loader_common, max_page_count_memory64_moderate_custom_page_size)
{
    /* is_memory64=true, num_bytes_per_page=256 (multiplier=256):
     * UINT32_MAX * 256 overflows to a different non-zero garbage value
     * under the old logic; second, differently-valued overflow case,
     * ruling out a coincidental single-value fix. */
    EXPECT_EQ(wasm_calculate_max_page_count(true, 256u), UINT32_MAX);
}
#endif /* WASM_ENABLE_MEMORY64 != 0 */

TEST(wasm_loader_common, max_page_count_zero_bytes_per_page_defensive)
{
    /* structurally unreachable post wasm_check_page_size_log2, but the
     * helper must defend against divide-by-zero regardless. */
    EXPECT_EQ(wasm_calculate_max_page_count(false, 0u), UINT32_MAX);
    EXPECT_EQ(wasm_calculate_max_page_count(true, 0u), UINT32_MAX);
}
