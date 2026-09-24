#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Writes the exact hand-built memory64 + custom-page-size module bytes
# used by the classic interpreter test
# (build_memory64_custom_page_size_grow_and_size_module() in
# tests/unit/linear-memory-wasm/linear_memory_wasm_test.cc) to a .wasm
# file, so it can be compiled with wamrc and run via the AOT runtime for
# parity comparison.
#
# Module shape:
#  - one memory: flags=(CUSTOM_PAGE_SIZE_FLAG|MAX_PAGE_COUNT_FLAG|
#    MEMORY64_FLAG)=0x0D, init=1, max=10 (u64-LEB, memory64 index width),
#    page_size_log2=8 (256 bytes/page)
#  - one exported function () -> i64 named "grow_and_size_mem64" whose
#    body is: i64.const 2; memory.grow 0; drop; memory.size 0; end

import sys


def leb_u32(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value != 0:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            break
    return bytes(out)


def leb_u64(value):
    return leb_u32(value)


def section(section_id, content):
    return bytes([section_id]) + leb_u32(len(content)) + content


def build_module():
    module = bytearray()
    # header: magic + version
    module += b"\x00\x61\x73\x6d\x01\x00\x00\x00"

    # type section: one func type () -> (i64)
    type_section = bytearray()
    type_section += leb_u32(1)  # type count
    type_section.append(0x60)  # func
    type_section.append(0x00)  # param count
    type_section.append(0x01)  # result count
    type_section.append(0x7E)  # i64
    module += section(1, bytes(type_section))

    # function section: one function, type index 0
    function_section = bytearray()
    function_section += leb_u32(1)
    function_section += leb_u32(0)
    module += section(3, bytes(function_section))

    # memory section: flags=0x0D, init=1, max=10 (u64), page_size_log2=8
    mem_section = bytearray()
    mem_section += leb_u32(1)  # memory count
    mem_flags = 0x08 | 0x01 | 0x04  # CUSTOM_PAGE_SIZE | MAX_PAGE_COUNT | MEMORY64
    mem_section.append(mem_flags)
    mem_section += leb_u64(1)   # init (u64)
    mem_section += leb_u64(10)  # max (u64)
    mem_section += leb_u32(8)   # page_size_log2
    module += section(5, bytes(mem_section))

    # export section: export function 0 as "grow_and_size_mem64"
    export_section = bytearray()
    export_section += leb_u32(1)  # export count
    export_name = b"grow_and_size_mem64"
    export_section += leb_u32(len(export_name))
    export_section += export_name
    export_section.append(0)  # EXPORT_KIND_FUNC
    export_section += leb_u32(0)  # function index 0
    module += section(7, bytes(export_section))

    # code section: body = i64.const 2; memory.grow 0; drop;
    # memory.size 0; end
    code_section = bytearray()
    code_section += leb_u32(1)  # function body count
    body = bytearray()
    body.append(0x00)  # local decl count
    body.append(0x42)  # i64.const
    body.append(0x02)  # 2
    body.append(0x40)  # memory.grow
    body.append(0x00)  # memidx
    body.append(0x1A)  # drop
    body.append(0x3F)  # memory.size
    body.append(0x00)  # memidx
    body.append(0x0B)  # end
    code_section += leb_u32(len(body))
    code_section += bytes(body)
    module += section(10, bytes(code_section))

    return bytes(module)


def main():
    if len(sys.argv) != 2:
        print("usage: gen_memory64_custom_page_size_module.py <output.wasm>",
              file=sys.stderr)
        return 1
    with open(sys.argv[1], "wb") as f:
        f.write(build_module())
    return 0


if __name__ == "__main__":
    sys.exit(main())
