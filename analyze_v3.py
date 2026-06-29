#!/usr/bin/env python3
"""
正确分析哈基米 v2.3 - 
关键：Capstone 的 ADRP imm 已经是目标页地址，不需要额外计算！
通过 GOT/.data.rel.ro 中的指针找到字符串引用。
"""

from elftools.elf.elffile import ELFFile
from capstone import *
from capstone.arm64 import *
import struct

BINARY_PATH = "E:/ImGuiOverlay/hjm_v2.3.sh"

elf = ELFFile(open(BINARY_PATH, 'rb'))
with open(BINARY_PATH, 'rb') as f:
    data = f.read()

# 段信息
text_sec = elf.get_section_by_name('.text')
rodata_sec = elf.get_section_by_name('.rodata')
got_sec = elf.get_section_by_name('.got')
reldata_sec = elf.get_section_by_name('.data.rel.ro')
rela_sec = elf.get_section_by_name('.rela.dyn')

text_addr = text_sec['sh_addr']
text_offset = text_sec['sh_offset']
text_size = text_sec['sh_size']
rd_addr = rodata_sec['sh_addr']
rd_size = rodata_sec['sh_size']
rd_data = data[rodata_sec['sh_offset']:rodata_sec['sh_offset'] + rd_size]

# 找字符串在 .rodata 中的偏移
def find_str(name):
    needle = name.encode('utf-8') + b'\x00'
    for i in range(len(rd_data) - len(needle)):
        if rd_data[i:i+len(needle)] == needle:
            return rd_addr + i
    return None

str_current = find_str("当前动作: %d")
str_sv = find_str("监管动作: %d")
print(f"字符串地址:")
print(f"  '当前动作: %d' → 0x{str_current:x}")
print(f"  '监管动作: %d' → 0x{str_sv:x}")

# 在 .data.rel.ro 中搜索指向这些字符串的指针
reldata_data = data[reldata_sec['sh_offset']:reldata_sec['sh_offset'] + reldata_sec['sh_size']]
reldata_addr = reldata_sec['sh_addr']

got_data = data[got_sec['sh_offset']:got_sec['sh_offset'] + got_sec['sh_size']]
got_addr = got_sec['sh_addr']

print(f"\n=== 指针搜索 ===")
for label, sec_data, sec_addr, sec_name in [
    (".data.rel.ro", reldata_data, reldata_addr, "reldata"),
    (".got", got_data, got_addr, "got")
]:
    for str_addr, str_name in [(str_current, "当前动作"), (str_sv, "监管动作")]:
        if str_addr is None:
            continue
        needle = struct.pack('<Q', str_addr)
        pos = 0
        while True:
            pos = sec_data.find(needle, pos)
            if pos == -1:
                break
            slot_addr = sec_addr + pos
            print(f"  [{sec_name}] '{str_name}' 指针 @ 0x{slot_addr:x}")
            pos += 8

# === 重定位分析 ===
print(f"\n=== .rela.dyn 中指向目标字符串的重定位 ===")
for reloc in rela_sec.iter_relocations():
    addend = reloc['r_addend']
    offset = reloc['r_offset']
    
    if addend == str_current:
        print(f"  '当前动作': offset=0x{offset:x} → 应该位于 0x{offset:x}")
        # 检查 offset 在哪个段
        if reldata_addr <= offset < reldata_addr + reldata_sec['sh_size']:
            slot = offset
            slot_idx = (offset - reldata_addr) // 8
            print(f"    在 .data.rel.ro[slot {slot_idx}] @ 0x{offset:x}")
        elif got_addr <= offset < got_addr + got_sec['sh_size']:
            slot = offset
            slot_idx = (offset - got_addr) // 8
            print(f"    在 .got[slot {slot_idx}] @ 0x{offset:x}")

    if addend == str_sv:
        print(f"  '监管动作': offset=0x{offset:x}")
        if reldata_addr <= offset < reldata_addr + reldata_sec['sh_size']:
            slot_idx = (offset - reldata_addr) // 8
            print(f"    在 .data.rel.ro[slot {slot_idx}] @ 0x{offset:x}")
        elif got_addr <= offset < got_addr + got_sec['sh_size']:
            slot_idx = (offset - got_addr) // 8
            print(f"    在 .got[slot {slot_idx}] @ 0x{offset:x}")

# === 反汇编，找到引用 GOT/data.rel.ro 中目标 slot 的代码 ===
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True
code = data[text_offset:text_offset + text_size]

# 目标 GOT slot 地址列表（我们需要找代码中引用这些地址的地方）
# 先收集所有指向目标字符串的 GOT/data.rel.ro slot
target_slots = {}
for str_addr, str_name in [(str_current, "当前动作"), (str_sv, "监管动作")]:
    if str_addr is None:
        continue
    needle = struct.pack('<Q', str_addr)
    for label, sec_data, sec_addr in [
        ("reldata", reldata_data, reldata_addr),
        ("got", got_data, got_addr)
    ]:
        pos = 0
        while True:
            pos = sec_data.find(needle, pos)
            if pos == -1:
                break
            slot = sec_addr + pos
            target_slots[slot] = (str_name, label)
            pos += 8
    # 也检查 .rela.dyn
    for reloc in rela_sec.iter_relocations():
        if reloc['r_addend'] == str_addr:
            target_slots[reloc['r_offset']] = (str_name, "rela")

print(f"\n目标 GOT slot: {[f'0x{k:x}' for k in target_slots]}")

# 现在在代码中搜索引用这些 slot 的 ADRP+ADD+LDR 序列
print(f"\n=== 代码引用分析 ===")
instructions = list(md.disasm(code, text_addr))
insn_dict = {insn.address: insn for insn in instructions}

# 建立 slot → ADRP 的映射
slot_to_adrp = {}

for insn in instructions:
    if insn.mnemonic != 'adrp':
        continue
    
    # Capstone 的 ADRP imm 就是目标页地址
    page = insn.operands[1].imm
    dest_reg = insn.operands[0].reg
    
    # 检查这个页是否包含目标 slot
    for slot in target_slots:
        slot_page = slot & ~0xFFF
        if page == slot_page:
            page_off = slot & 0xFFF
            
            # 找下一条 ADD
            next_insn = insn_dict.get(insn.address + 4)
            if next_insn and next_insn.mnemonic == 'add':
                if next_insn.operands[1].reg == dest_reg:
                    if next_insn.operands[2].imm == page_off:
                        # Bingo! 这条 ADRP+ADD 计算的就是我们的 slot 地址
                        ref_reg = next_insn.operands[0].reg
                        slot_to_adrp[slot] = (insn.address, dest_reg, ref_reg)
                        print(f"  slot 0x{slot:x} ({target_slots[slot][0]})")
                        print(f"    ADRP @ 0x{insn.address:x}: adrp r{dest_reg}, #0x{page:x}")
                        print(f"    ADD  @ 0x{next_insn.address:x}: add r{ref_reg}, r{dest_reg}, #0x{page_off:x}")
                        
                        # 从这里找到所在的函数并分析
                        func_start = None
                        func_end = None
                        for a in sorted(insn_dict.keys()):
                            if a <= insn.address and a > insn.address - 0x200:
                                i = insn_dict[a]
                                if i.mnemonic == 'stp' and 'x29' in i.op_str and 'x30' in i.op_str:
                                    # 函数入口
                                    func_start = a
                                    break
                        
                        # 往前搜索函数结束 (ret) 
                        if func_start:
                            for a in sorted(insn_dict.keys()):
                                if a >= insn.address:
                                    i = insn_dict[a]
                                    if i.mnemonic == 'ret' and a > insn.address:
                                        func_end = a + 4
                                        break
                                    if a > insn.address + 0x400:
                                        break
                            
                            print(f"    函数: 0x{func_start:x} - 0x{func_end:x if func_end else '?'}")
                            
                            # 在函数内找 LDR x0, [ref_reg] 后面的 BL 调用
                            # 跟踪 ref_reg 的使用
                            current_reg = ref_reg
                            for a in sorted(insn_dict.keys()):
                                if func_start <= a < (func_end or func_start + 0x400) and a >= insn.address:
                                    i = insn_dict[a]
                                    
                                    # LDR 从我们的 slot 指针加载
                                    if i.mnemonic == 'ldr' and len(i.operands) >= 2:
                                        op0 = i.operands[0]
                                        op1 = i.operands[1]
                                        if op1.type == ARM64_OP_MEM and op1.mem.base == current_reg:
                                            # 加载到目标寄存器
                                            loaded_reg = op0.reg
                                            print(f"    LDR r{loaded_reg}, [r{current_reg}] @ 0x{a:x}")
                                          
                                    # 跟踪 MOV 链
                                    if i.mnemonic == 'mov' and len(i.operands) >= 2:
                                        if i.operands[1].type == ARM64_OP_REG and i.operands[1].reg == current_reg:
                                            pass  # 寄存器传递
                        
                            # 在这个函数中搜索所有 BL 调用，检查调用前参数
                            # 重点看 x1, x2 (动作ID参数)
                            print(f"    函数内所有 LDR/STR (非栈):")
                            for a in range(func_start, min(func_end or func_start + 0x400, func_start + 0x400), 4):
                                if a in insn_dict:
                                    i = insn_dict[a]
                                    if i.mnemonic in ['ldr', 'ldur']:
                                        for op in i.operands:
                                            if op.type == ARM64_OP_MEM:
                                                if op.mem.base != ARM64_REG_SP and op.mem.base != ARM64_REG_X29:
                                                    if op.mem.disp != 0:
                                                        print(f"      0x{a:x}: {i.mnemonic} r{i.reg_name(i.operands[0].reg)}, [r{i.reg_name(op.mem.base)}, #0x{op.mem.disp:x}]")
                        
                        print()
                        
                        if len(slot_to_adrp) >= 4:
                            break
    if len(slot_to_adrp) >= 4:
        break
