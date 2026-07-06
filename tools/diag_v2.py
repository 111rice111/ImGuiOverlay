#!/usr/bin/env python3
"""
诊断 v2：分析 PIE 二进制中的 ADRP 寻址，正确计算目标地址。
关键：理解 ELF 的加载基址和 PIE 重定位。
"""

from elftools.elf.elffile import ELFFile
from capstone import *
from capstone.arm64 import *

BINARY_PATH = "E:/ImGuiOverlay/hjm_v2.3.sh"

elf = ELFFile(open(BINARY_PATH, 'rb'))
with open(BINARY_PATH, 'rb') as f:
    data = f.read()

# === 1. 程序头（加载段）===
print("=== PT_LOAD 段 ===")
for seg in elf.iter_segments():
    if seg['p_type'] == 'PT_LOAD':
        print(f"  vaddr=0x{seg['p_vaddr']:x} memsz=0x{seg['p_memsz']:x} "
              f"filesz=0x{seg['p_filesz']:x} offset=0x{seg['p_offset']:x} "
              f"align=0x{seg['p_align']:x} flags={seg['p_flags']}")

# === 2. 直接读取 ADRP 指令字节，手动解码 ===
text_sec = elf.get_section_by_name('.text')
text_addr = text_sec['sh_addr']
text_offset = text_sec['sh_offset']

# 手动解码 ADRP: 0x47e04
raw_addr = 0x47e04
file_off = text_offset + (raw_addr - text_addr)
raw_insn = int.from_bytes(data[file_off:file_off+4], 'little')
print(f"\n=== 手动解码 ADRP @ 0x{raw_addr:x} ===")
print(f"  原始字节: {raw_insn:08x}")

# ADRP 编码: 
# bits[30:29] = immlo
# bits[23:5] = immhi
immlo = (raw_insn >> 29) & 0x3
immhi = (raw_insn >> 5) & 0x7FFFF
imm21 = (immhi << 2) | immlo
# Sign extend 21-bit to 64-bit
if imm21 & 0x100000:
    imm21 = imm21 - 0x200000
print(f"  immlo={immlo}, immhi={immhi:#x}, imm21={imm21} (0x{imm21 & 0xFFFFFFFF:x})")

# ADRP 计算: page = (PC & ~0xFFF) + SignExtend(immhi:immlo:Zeros(12), 64)
pc_page = raw_addr & ~0xFFF
# imm21 << 12:
target_page = pc_page + (imm21 << 12)
print(f"  PC page: 0x{pc_page:x}")
print(f"  imm21<<12: 0x{imm21<<12:x}")
print(f"  target page: 0x{target_page:x}")

# Capstone 解码
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True
code = data[text_offset:text_offset + text_sec['sh_size']]
for insn in md.disasm(code, text_addr):
    if insn.address == raw_addr:
        print(f"\n  Capstone: {insn.mnemonic} {insn.op_str}")
        for i, op in enumerate(insn.operands):
            if op.type == ARM64_OP_IMM:
                print(f"  Capstone imm[{i}]: {op.imm} (0x{op.imm:x})")
                # Capstone 的 imm 是 imm21 << 12 ?
                print(f"  Capstone imm >> 12 = {op.imm >> 12} (应该 = {imm21})")
                # 还是 Capstone 的 imm 就是 imm21？
                print(f"  如果用 imm21 计算: page = 0x{pc_page:x} + 0x{op.imm:x if op.imm < 0x100000 else op.imm << 12:x}")
        break

# === 3. 找出 .rodata 中字符串的正确引用方式 ===
print("\n=== 搜索字符串引用 ===")

# .data.rel.ro 可能有重定位项指向这些字符串
# 搜索 .data.rel.ro 中是否有指向 "当前动作" 的指针
reldata_sec = elf.get_section_by_name('.data.rel.ro')
if reldata_sec:
    rd_offset = reldata_sec['sh_offset']
    rd_size = reldata_sec['sh_size']
    rd_addr = reldata_sec['sh_addr']
    rd_data = data[rd_offset:rd_offset + rd_size]
    
    # 搜索指针值 0x171d7（"当前动作"字符串地址）
    needle_bytes = (0x171d7).to_bytes(8, 'little')
    pos = 0
    while True:
        pos = rd_data.find(needle_bytes, pos)
        if pos == -1:
            break
        vaddr = rd_addr + pos
        print(f"  .data.rel.ro 中找到指针 0x171d7 @ 0x{vaddr:x} (文件偏移 0x{rd_offset+pos:x})")
        pos += 1
    
    # 也搜索 0x1d2b2 ("监管动作")
    needle_bytes2 = (0x1d2b2).to_bytes(8, 'little')
    pos = 0
    while True:
        pos = rd_data.find(needle_bytes2, pos)
        if pos == -1:
            break
        vaddr = rd_addr + pos
        print(f"  .data.rel.ro 中找到指针 0x1d2b2 @ 0x{vaddr:x}")
        pos += 1

# .got 也是如此
got_sec = elf.get_section_by_name('.got')
if got_sec:
    got_offset = got_sec['sh_offset']
    got_size = got_sec['sh_size']
    got_addr = got_sec['sh_addr']
    got_data = data[got_offset:got_offset + got_size]
    
    needle_bytes = (0x171d7).to_bytes(8, 'little')
    pos = 0
    while True:
        pos = got_data.find(needle_bytes, pos)
        if pos == -1:
            break
        vaddr = got_addr + pos
        print(f"  .got 中找到指针 0x171d7 @ 0x{vaddr:x}")
        pos += 1

# === 4. 检查 .rela.dyn 中是否有重定位指向这些字符串 ===
print("\n=== .rela.dyn 重定位 ===")
rela = elf.get_section_by_name('.rela.dyn')
if rela:
    rd = elf.get_section_by_name('.rodata')
    rd_start = rd['sh_addr']
    rd_end = rd_start + rd['sh_size']
    
    for reloc in rela.iter_relocations():
        addend = reloc['r_addend']
        if rd_start <= addend < rd_end:
            sym_name = f" (sym: {reloc['r_info_sym']})" if reloc['r_info_sym'] != 0 else ""
            reloc_type = reloc['r_info_type']
            offset = reloc['r_offset']
            # 检查 addend 是否接近我们的字符串
            target_str_addr = addend
            if target_str_addr == 0x171d7:
                print(f"  *** '当前动作' 重定位: offset=0x{offset:x} addend=0x{addend:x} type={reloc_type}{sym_name}")
            elif target_str_addr == 0x1d2b2:
                print(f"  *** '监管动作' 重定位: offset=0x{offset:x} addend=0x{addend:x} type={reloc_type}{sym_name}")
