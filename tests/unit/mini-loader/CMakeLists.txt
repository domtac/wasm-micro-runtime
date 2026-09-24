# Copyright (C) 2024 Amazon Inc.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project (test-mini-loader)

add_definitions (-DRUN_ON_LINUX)

set (WAMR_BUILD_AOT 0)
set (WAMR_BUILD_FAST_INTERP 1)
set (WAMR_BUILD_MINI_LOADER 1)
set (WAMR_BUILD_INTERP 1)
set (WAMR_BUILD_JIT 0)
set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_APP_FRAMEWORK 0)
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
)

add_executable (mini_loader_test ${unit_test_sources})

target_link_libraries (mini_loader_test gtest_main)

gtest_discover_tests(mini_loader_test)
