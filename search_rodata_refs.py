#!/usr/bin/env python3
"""
综合搜索：在 .rela.dyn、.data.rel.ro、.got 中查找所有 .rodata 引用，
特别关注字符串和附近的地址。
"""

from elftools.elf.elffile import ELFFile
import struct

BINARY_PATH = "E:/ImGuiOverlay/hjm_v2.3.sh"

elf = ELFFile(open(BINARY_PATH, 'rb'))
with open(BINARY_PATH, 'rb') as f:
    data = f.read()

# 段
rodata_sec = elf.get_section_by_name('.rodata')
reldata_sec = elf.get_section_by_name('.data.rel.ro')
got_sec = elf.get_section_by_name('.got')
rela_sec = elf.get_section_by_name('.rela.dyn')

rd_addr = rodata_sec['sh_addr']
rd_size = rodata_sec['sh_size']
rd_end = rd_addr + rd_size
rd_data = data[rodata_sec['sh_offset']:rodata_sec['sh_offset'] + rd_size]

# 找字符串
str_current_off = None
str_sv_off = None
for i in range(len(rd_data)):
    chunk = rd_data[i:i+20]
    if chunk.startswith("当前动作: %d".encode('utf-8')):
        str_current_off = i
    if chunk.startswith("监管动作: %d".encode('utf-8')):
        str_sv_off = i

print(f"字符串在 .rodata 中偏移:")
print(f"  '当前动作: %d' @ 0x{rd_addr + str_current_off:x} (offset 0x{str_current_off:x})")
print(f"  '监管动作: %d' @ 0x{rd_addr + str_sv_off:x} (offset 0x{str_sv_off:x})")

# === 1. 列出 ALL .rela.dyn 指向 .rodata 的条目 ===
print(f"\n=== .rela.dyn → .rodata ===")
rd_refs = []
for reloc in rela_sec.iter_relocations():
    addend = reloc['r_addend']
    offset = reloc['r_offset']
    rtype = reloc['r_info_type']
    
    if rd_addr <= addend < rd_end:
        rd_off = addend - rd_addr
        nearby = rd_data[rd_off:rd_off+40]
        try:
            nearby_str = nearby.decode('utf-8', errors='replace')
        except:
            nearby_str = repr(nearby[:30])
        
        is_target = addend in [rd_addr + str_current_off, rd_addr + str_sv_off]
        marker = " ★★★★★" if is_target else ""
        
        rd_refs.append((offset, addend, rtype, nearby_str, is_target))
        if is_target:
            print(f"  *** TARGET *** offset=0x{offset:x} addend=0x{addend:x} type={rtype} str='{nearby_str[:30]}'")

print(f"\n共 {len(rd_refs)} 个 .rela.dyn 引用 .rodata")
if rd_refs and not any(r[4] for r in rd_refs):
    print("前20个:")
    for offset, addend, rtype, nearby_str, _ in rd_refs[:20]:
        print(f"  offset=0x{offset:x} addend=0x{addend:x} type={rtype} str='{nearby_str[:30]}'")

# === 2. 直接搜索 .data.rel.ro 和 .got 中的字节 ===
print(f"\n=== 在 .data.rel.ro 和 .got 中搜索目标地址 ===")
for label, sec in [(".data.rel.ro", reldata_sec), (".got", got_sec)]:
    sec_data = data[sec['sh_offset']:sec['sh_offset'] + sec['sh_size']]
    sec_addr = sec['sh_addr']
    
    for str_addr, str_name in [
        (rd_addr + str_current_off, "当前动作: %d"),
        (rd_addr + str_sv_off, "监管动作: %d")
    ]:
        needle_le = struct.pack('<Q', str_addr)
        pos = 0
        found = False
        while True:
            pos = sec_data.find(needle_le, pos)
            if pos == -1:
                break
            slot = sec_addr + pos
            print(f"  [{label}] '{str_name}' @ slot 0x{slot:x}")
            found = True
            pos += 8
        if not found:
            # 尝试大数据搜索（可能指针不完全匹配）
            pass

# === 3. 搜索 .text 中所有 LDR 指令偏移量统计（扩展版）===
from capstone import *
from capstone.arm64 import *

text_sec = elf.get_section_by_name('.text')
text_data = data[text_sec['sh_offset']:text_sec['sh_offset'] + text_sec['sh_size']]
text_addr = text_sec['sh_addr']

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True

print(f"\n=== .text 中 LDR 偏移量统计 (非栈) ===")
offset_count = {}
for insn in md.disasm(text_data, text_addr):
    if insn.mnemonic in ['ldr', 'ldur']:
        for op in insn.operands:
            if op.type == ARM64_OP_MEM:
                if op.mem.base != ARM64_REG_SP and op.mem.base != ARM64_REG_X29:
                    disp = op.mem.disp
                    if disp > 0:  # 只统计正偏移
                        offset_count[disp] = offset_count.get(disp, 0) + 1

# 排序，显示前30个最常见的偏移量
sorted_offsets = sorted(offset_count.items(), key=lambda x: -x[1])[:30]
print(f"{'偏移':>10} | 次数")

# 只显示可能与游戏相关的偏移（0x100以上）
for off, cnt in sorted_offsets:
    marker = ""
    if off == 0x2D0: marker = " ★ actionID?"
    if off == 0x150: marker = " ★ comp?"
    if off == 0x138: marker = " ★ comp2?"
    if off == 0x10: marker = " ★ 对象首字段?"
    if off == 0x8: marker = " ★"
    if off >= 0x100 or cnt > 100:
        print(f"0x{off:08x} | {cnt}{marker}")
