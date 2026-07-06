#!/usr/bin/env python3
"""
拓展搜索：在 rodata 中找所有包含"动作"、"action"、"anim"等关键词的字符串，
并分析周围的其他字符串，找到实际的动作ID读取代码。
"""

from elftools.elf.elffile import ELFFile
from capstone import *
from capstone.arm64 import *
import struct

BINARY_PATH = "E:/ImGuiOverlay/hjm_v2.3.sh"

elf = ELFFile(open(BINARY_PATH, 'rb'))
with open(BINARY_PATH, 'rb') as f:
    data = f.read()

rodata_sec = elf.get_section_by_name('.rodata')
rd_data = data[rodata_sec['sh_offset']:rodata_sec['sh_offset'] + rodata_sec['sh_size']]
rd_addr = rodata_sec['sh_addr']

# 搜索关键词
keywords = [
    "当前动作", "监管动作", "动作", "action", "Action", "ACTION",
    "anim", "Anim", "ANI", "角色动作", "技能动作", "状态",
    "奔跑", "行走", "静止", "交互", "翻窗", "翻板",
    "倒地", "气球", "挣扎", "治疗", "破译", "校准",
]

print("=== .rodata 关键词搜索 ===")
found_strings = {}
for i in range(len(rd_data) - 4):
    # 搜索 UTF-8 字符串
    for kw in keywords:
        kwb = kw.encode('utf-8')
        if rd_data[i:i+len(kwb)] == kwb:
            # 找字符串的结束位置
            end = i
            while end < len(rd_data) and rd_data[end] != 0:
                end += 1
            full_str = rd_data[i:end]
            try:
                decoded = full_str.decode('utf-8')
                if len(decoded) >= len(kw):
                    addr = rd_addr + i
                    if addr not in found_strings:
                        found_strings[addr] = decoded
            except:
                pass

for addr in sorted(found_strings.keys()):
    print(f"  0x{addr:x}: '{found_strings[addr]}'")

# === 新策略：找 .rela.dyn 中引用这些字符串的条目 ===
print(f"\n=== .rela.dyn 引用分析 ===")
rela_sec = elf.get_section_by_name('.rela.dyn')

# 建立 offset → string 的映射
string_at_offset = {}
for str_addr, s in found_strings.items():
    string_at_offset[str_addr] = s

# 也添加我们已知的目标
string_at_offset[rd_addr + 0x3107] = "当前动作: %d"
string_at_offset[rd_addr + 0x91e2] = "监管动作: %d"

# 搜索 rela.dyn
for reloc in rela_sec.iter_relocations():
    addend = reloc['r_addend']
    offset = reloc['r_offset']
    rtype = reloc['r_info_type']
    
    if addend in string_at_offset:
        print(f"  '{string_at_offset[addend]}' → offset=0x{offset:x}")

# === 关键：找到了字符串引用，分析对应的代码 ===
# 手动搜索 .data.rel.ro 中所有可能指向"当前动作"的指针
# （即使不是完全匹配的地址，也可能是通过基址+偏移计算）
print(f"\n=== 在 .data.rel.ro 中搜索包含 %d 格式的字符串指针 ===")
reldata_sec = elf.get_section_by_name('.data.rel.ro')
reldata_data = data[reldata_sec['sh_offset']:reldata_sec['sh_offset'] + reldata_sec['sh_size']]
reldata_addr = reldata_sec['sh_addr']

# 搜索所有可能指向 "当前动作" 附近区域的指针
target_base = rd_addr + 0x3107  # "当前动作: %d"
for offset in range(-0x100, 0x100, 8):
    search_addr = target_base + offset
    needle = struct.pack('<Q', search_addr)
    pos = 0
    while True:
        pos = reldata_data.find(needle, pos)
        if pos == -1:
            break
        slot = reldata_addr + pos
        rd_off = search_addr - rd_addr
        if rd_off >= 0 and rd_off < len(rd_data):
            try:
                nearby = rd_data[rd_off:rd_off+30].decode('utf-8', errors='replace')
            except:
                nearby = repr(rd_data[rd_off:rd_off+30])
        else:
            nearby = "?"
        print(f"  .data.rel.ro[0x{slot:x}] → 0x{search_addr:x} (offset {offset:+d}): '{nearby[:30]}'")
        pos += 8

# === 最后的尝试：搜索所有 %d 格式字符串，看哪些被代码引用 ===
print(f"\n=== 所有包含 %%d 的格式字符串 (前30) ===")
fmt_strings = []
for i in range(len(rd_data) - 4):
    if rd_data[i:i+2] == b'%d':
        start = i
        while start > 0 and rd_data[start-1] != 0:
            start -= 1
        end = i + 2
        while end < len(rd_data) and rd_data[end] != 0:
            end += 1
        s = rd_data[start:end]
        try:
            decoded = s.decode('utf-8')
            addr = rd_addr + start
            fmt_strings.append((addr, decoded))
        except:
            pass

# 去重
seen = set()
unique_fmts = []
for addr, s in fmt_strings:
    if s not in seen:
        seen.add(s)
        unique_fmts.append((addr, s))

for addr, s in unique_fmts[:30]:
    # 检查是否有 rela.dyn 指向这个地址
    has_rela = False
    for reloc in rela_sec.iter_relocations():
        if reloc['r_addend'] == addr:
            has_rela = True
            break
    marker = " ✓rela" if has_rela else ""
    print(f"  0x{addr:x}: '{s}'{marker}")
