#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Writes the exact hand-built custom-page-size module bytes used by the
# classic interpreter test (build_custom_page_size_grow_and_size_module()
# in tests/unit/linear-memory-wasm/linear_memory_wasm_test.cc) to a .wasm
# file, so it can be compiled with wamrc and run via the AOT runtime for
# parity comparison.
#
# Module shape:
#  - one memory: flags=(CUSTOM_PAGE_SIZE_FLAG|MAX_PAGE_COUNT_FLAG)=0x09,
#    init=1, max=10, page_size_log2=8 (256 bytes/page)
#  - one exported function () -> i32 named "grow_and_size" whose body is:
#    i32.const 2; memory.grow 0; drop; memory.size 0; end

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


def section(section_id, content):
    return bytes([section_id]) + leb_u32(len(content)) + content


def build_module():
    module = bytearray()
    # header: magic + version
    module += b"\x00\x61\x73\x6d\x01\x00\x00\x00"

    # type section: one func type () -> (i32)
    type_section = bytearray()
    type_section += leb_u32(1)  # type count
    type_section.append(0x60)  # func
    type_section.append(0x00)  # param count
    type_section.append(0x01)  # result count
    type_section.append(0x7F)  # i32
    module += section(1, bytes(type_section))

    # function section: one function, type index 0
    function_section = bytearray()
    function_section += leb_u32(1)
    function_section += leb_u32(0)
    module += section(3, bytes(function_section))

    # memory section: flags=0x09, init=1, max=10, page_size_log2=8
    mem_section = bytearray()
    mem_section += leb_u32(1)  # memory count
    mem_flags = 0x08 | 0x01  # CUSTOM_PAGE_SIZE_FLAG | MAX_PAGE_COUNT_FLAG
    mem_section.append(mem_flags)
    mem_section += leb_u32(1)   # init
    mem_section += leb_u32(10)  # max
    mem_section += leb_u32(8)   # page_size_log2
    module += section(5, bytes(mem_section))

    # export section: export function 0 as "grow_and_size"
    export_section = bytearray()
    export_section += leb_u32(1)  # export count
    export_name = b"grow_and_size"
    export_section += leb_u32(len(export_name))
    export_section += export_name
    export_section.append(0)  # EXPORT_KIND_FUNC
    export_section += leb_u32(0)  # function index 0
    module += section(7, bytes(export_section))

    # code section: body = i32.const 2; memory.grow 0; drop;
    # memory.size 0; end
    code_section = bytearray()
    code_section += leb_u32(1)  # function body count
    body = bytearray()
    body.append(0x00)  # local decl count
    body.append(0x41)  # i32.const
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
        print("usage: gen_custom_page_size_module.py <output.wasm>",
              file=sys.stderr)
        return 1
    with open(sys.argv[1], "wb") as f:
        f.write(build_module())
    return 0


if __name__ == "__main__":
    sys.exit(main())
