# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project (test-linear-memory-fast-jit)

add_definitions (-DRUN_ON_LINUX)

set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_APP_FRAMEWORK 0)
set (WAMR_BUILD_MEMORY_PROFILING 1)
set (WAMR_BUILD_INTERP 0)
set (WAMR_BUILD_AOT 0)
set (WAMR_BUILD_JIT 0)
set (WAMR_BUILD_FAST_JIT 1)
# WAMR_BUILD_MEMORY64 is intentionally NOT enabled here.
# build-scripts/unsupported_combination.cmake hard-fails on
# MEMORY64 + FAST_JIT ("Unsupported build configuration: MEMORY64 +
# FAST_JIT") -- a project-wide architectural constraint, not a bug in
# this test target. The memory64 test/module-builder code added to
# linear_memory_fast_jit_test.cc is guarded by
# #if WASM_ENABLE_MEMORY64 != 0 and stays inert (excluded from
# compilation) until fast-jit gains memory64 support upstream.
if (APPLE)
  set (WAMR_BUILD_INVOKE_NATIVE_GENERAL 1)
endif ()

include (../unit_common.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
    ${UNIT_SOURCE}
    ${WAMR_RUNTIME_LIB_SOURCE}
    ${UNCOMMON_SHARED_SOURCE}
)

# Fast-jit-only test target (no LLVM-JIT) proving memory.grow/
# memory.size custom-page-size parity under fast-jit, mirroring
# linear-memory-wasm's structure for the classic interpreter.
add_executable (linear_memory_test_fast_jit ${unit_test_sources})
target_link_libraries (linear_memory_test_fast_jit gtest_main)
gtest_discover_tests(linear_memory_test_fast_jit)
