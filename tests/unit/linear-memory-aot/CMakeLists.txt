# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project (test-linear-memory-aot)

add_definitions (-DRUN_ON_LINUX)

set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_APP_FRAMEWORK 0)
set (WAMR_BUILD_MEMORY_PROFILING 1)
set (WAMR_BUILD_INTERP 0)
set (WAMR_BUILD_AOT 1)
set (WAMR_BUILD_FAST_INTERP 0)
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
  ${IWASM_COMPL_SOURCE}
)

# Test case: .aot file with hardware bound check.
add_executable (linear_memory_test_aot ${unit_test_sources})
target_link_libraries (linear_memory_test_aot gtest_main)
gtest_discover_tests(linear_memory_test_aot)
target_compile_definitions(linear_memory_test_aot PRIVATE WAMR_DISABLE_HW_BOUND_CHECK=0)

# Ensure that aot compiled is completed before linear_memory_test_aot is built
set(dummy_output "${CMAKE_CURRENT_BINARY_DIR}/dummy_output")

add_custom_command(OUTPUT ${dummy_output}
  COMMAND ./build_aot.sh
  COMMAND ${CMAKE_COMMAND} -E touch ${dummy_output}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build_aot.sh
  COMMENT "Executing script to compile aot files"
  VERBATIM
)

add_custom_target(
  RunBuildAot ALL
  DEPENDS ${dummy_output}
)

add_dependencies(linear_memory_test_aot RunBuildAot)

add_custom_command(TARGET linear_memory_test_aot POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
  ${CMAKE_CURRENT_SOURCE_DIR}/build/*.aot
  ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Copy aot files to the directory: build/linear-memory-aot."
)

# Test case: .aot file with no hardware bound check.
add_executable (linear_memory_test_aot_no_hw_bound ${unit_test_sources})
target_link_libraries (linear_memory_test_aot_no_hw_bound gtest_main)
gtest_discover_tests(linear_memory_test_aot_no_hw_bound)
target_compile_definitions(linear_memory_test_aot_no_hw_bound PRIVATE WAMR_DISABLE_HW_BOUND_CHECK=1)

# Story 2.3: compile Story 2.2's hand-built custom-page-size module
# (build_custom_page_size_grow_and_size_module() in
# linear-memory-wasm/linear_memory_wasm_test.cc) into a .aot fixture via
# the prebuilt wamrc binary, for AOT-runtime/wamrc parity testing.
# This is a narrow, standalone step -- it does NOT reuse build_aot.sh
# (which depends on a hardcoded /opt/wabt/bin/wat2wasm path unavailable
# here), and custom page sizes can't be authored via .wat anyway.
#
# --target=x86_64 is intentional, not a portability bug: this whole test
# harness (tests/unit/unit_common.cmake) unconditionally builds the AOT
# runtime/loader itself with WAMR_BUILD_TARGET=X86_64 on any 64-bit host,
# regardless of actual host CPU -- so its loader only ever accepts x86_64
# .aot files, matching every other fixture in this directory (build_aot.sh).
#
# On a host whose checked-in wamrc predates this feature's implementation,
# the generated .aot's custom-page-size memory metadata may not reflect
# current source (see deferred-work.md, Story 2.3 entry) -- this is an
# environment/toolchain-freshness limitation of the fixture, not something
# this CMake rule can detect or guard against.
find_program(WAMRC wamrc
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../../../wamr-compiler/build
  NO_DEFAULT_PATH
)

if (WAMRC)
  set(custom_page_size_wasm
    ${CMAKE_CURRENT_BINARY_DIR}/custom_page_size_grow_and_size.wasm)
  set(custom_page_size_aot
    ${CMAKE_CURRENT_BINARY_DIR}/custom_page_size_grow_and_size.aot)

  add_custom_command(OUTPUT ${custom_page_size_aot}
    COMMAND python3
      ${CMAKE_CURRENT_SOURCE_DIR}/gen_custom_page_size_module.py
      ${custom_page_size_wasm}
    COMMAND ${WAMRC} --target=x86_64 -o ${custom_page_size_aot} ${custom_page_size_wasm}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_custom_page_size_module.py ${WAMRC}
    COMMENT "Generating and wamrc-compiling custom-page-size .aot fixture"
    VERBATIM
  )

  add_custom_target(
    GenCustomPageSizeAot ALL
    DEPENDS ${custom_page_size_aot}
  )

  add_dependencies(linear_memory_test_aot GenCustomPageSizeAot)
  add_dependencies(linear_memory_test_aot_no_hw_bound GenCustomPageSizeAot)
else()
  message(WARNING
    "wamrc not found under wamr-compiler/build -- skipping custom-page-size "
    "AOT fixture generation; the corresponding test will fail to find its "
    ".aot file")
endif()
