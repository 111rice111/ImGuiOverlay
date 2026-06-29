#!/usr/bin/env python3
"""
诊断脚本：检查 ADRP 指令的 imm 值范围，找出为什么找不到 .rodata 引用。
"""

from elftools.elf.elffile import ELFFile
from capstone import *
from capstone.arm64 import *

BINARY_PATH = "E:/ImGuiOverlay/hjm_v2.3.sh"

elf = ELFFile(open(BINARY_PATH, 'rb'))
with open(BINARY_PATH, 'rb') as f:
    data = f.read()

# 获取段信息
for sec in elf.iter_sections():
    if sec.name in ['.text', '.rodata', '.data.rel.ro', '.got', '.got.plt']:
        print(f"{sec.name:20s} addr=0x{sec['sh_addr']:x} size=0x{sec['sh_size']:x} off=0x{sec['sh_offset']:x}")

# 反汇编
text_sec = elf.get_section_by_name('.text')
text_addr = text_sec['sh_addr']
text_offset = text_sec['sh_offset']
text_size = text_sec['sh_size']

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True
code = data[text_offset:text_offset + text_size]

rodata_sec = elf.get_section_by_name('.rodata')
rd_addr = rodata_sec['sh_addr']
rd_size = rodata_sec['sh_size']
rd_end = rd_addr + rd_size

print(f"\n.rodata 范围: 0x{rd_addr:x} - 0x{rd_end:x}")

# 找前20条 ADRP，看它们的 imm 范围
print("\n=== 前20条 ADRP 指令 ===")
count = 0
adrp_pages = set()
for insn in md.disasm(code, text_addr):
    if insn.mnemonic == 'adrp':
        count += 1
        if count <= 20:
            # imm 是页索引
            imm = insn.operands[1].imm
            # 计算引用的页地址
            ref_page = (insn.address & ~0xFFF) + imm * 0x1000
            print(f"  0x{insn.address:x}: adrp {insn.reg_name(insn.operands[0].reg)}, #{imm} -> page 0x{ref_page:x}")
        
        imm = insn.operands[1].imm
        ref_page = (insn.address & ~0xFFF) + imm * 0x1000
        adrp_pages.add(ref_page)

# 检查 ADRP 页是否能命中 .rodata
print(f"\n=== ADRP 页命中检查 ===")
min_page = min(adrp_pages)
max_page = max(adrp_pages)
print(f"ADRP 引用页范围: 0x{min_page:x} - 0x{max_page:x}")
print(f".rodata 范围:      0x{rd_addr:x} - 0x{rd_end:x}")

hit = [p for p in adrp_pages if rd_addr <= p < rd_end]
print(f"命中 .rodata 的页数: {len(hit)}")

# 搜索 "当前动作: %d" 的字节
needle = "当前动作: %d".encode('utf-8') + b'\x00'
needle_short = "当前动作".encode('utf-8')

rd_data = data[rodata_sec['sh_offset']:rodata_sec['sh_offset'] + rd_size]
for i in range(len(rd_data) - len(needle)):
    if rd_data[i:i+len(needle)] == needle:
        vaddr = rd_addr + i
        page = vaddr & ~0xFFF
        print(f"\n'当前动作: %d' @ 0x{vaddr:x} (page 0x{page:x})")
        in_range = page in adrp_pages
        print(f"  对应页是否被 ADRP 引用: {in_range}")

# 检查 ADR (非 ADRP) 
print("\n=== 搜索 ADR 引用 ===")
adr_refs_to_rodata = []
for insn in md.disasm(code, text_addr):
    if insn.mnemonic == 'adr':
        imm = insn.operands[1].imm
        target = insn.address + imm
        if rd_addr <= target < rd_end:
            adr_refs_to_rodata.append((insn.address, target))

print(f"ADR 引用 .rodata 的数量: {len(adr_refs_to_rodata)}")
for addr, target in adr_refs_to_rodata[:10]:
    # 检查目标是否是我们关心的字符串
    offset_in_rd = target - rd_addr
    if offset_in_rd >= 0 and offset_in_rd < len(rd_data):
        nearby = rd_data[max(0,offset_in_rd-5):offset_in_rd+30]
        try:
            nearby_str = nearby.decode('utf-8', errors='replace')
        except:
            nearby_str = repr(nearby)
        print(f"  ADR @ 0x{addr:x} -> 0x{target:x}: {nearby_str[:40]}")

# 也用 LDR literal 搜索 (PC相对加载)
print("\n=== 搜索 LDR literal (PC相对) 引用 .rodata ===")
ldr_refs = []
for insn in md.disasm(code, text_addr):
    if insn.mnemonic == 'ldr' and len(insn.operands) >= 2:
        op1 = insn.operands[1]
        if op1.type == ARM64_OP_MEM and op1.mem.base == 0:  # literal
            target = op1.mem.disp  # PC相对地址
            if rd_addr <= target < rd_end:
                ldr_refs.append((insn.address, target))

print(f"LDR literal 引用 .rodata 的数量: {len(ldr_refs)}")
for addr, target in ldr_refs[:10]:
    offset_in_rd = target - rd_addr
    if 0 <= offset_in_rd < len(rd_data):
        nearby = rd_data[max(0,offset_in_rd-5):offset_in_rd+30]
        try:
            nearby_str = nearby.decode('utf-8', errors='replace')
        except:
            nearby_str = repr(nearby)
        print(f"  LDR @ 0x{addr:x} -> 0x{target:x}: {nearby_str[:40]}")
