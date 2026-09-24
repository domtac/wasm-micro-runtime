/*
 * Copyright (C) 2024 Amazon Inc.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include <cstring>

extern "C" {
#include "wasm_loader_common.h"
}

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
