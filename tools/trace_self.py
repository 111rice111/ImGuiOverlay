#!/usr/bin/env python3
"""分析"自身"显示代码 — 找到哈基米如何获取玩家对象地址"""
from capstone import *
from capstone.arm64 import *
import struct

BIN = "E:/ImGuiOverlay/hjm_v2.3.sh"
with open(BIN, "rb") as f:
    data = f.read()

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True

TEXT_START = 0x47DE0
TEXT_END = 0x128400

text_code = data[TEXT_START:TEXT_END]

# "自身" string references at ADRP+ADD: 0x61ADC and 0x61AEC
# Both point to the same rodata page 0x1A000
# These are in the display function we already looked at (around 0x619xx - 0x61Cxx)

# Let's disassemble the full display function from function start to end
# The LDR at 0x61B2C reads "当前动作" value from 0x17E280(0x280)
# Before that, at:
# 0x61AD0: ldr w20, [x8]        # x8 = 0x17D000 + 0xA38 = 0x17DA38
# 0x61AD4: ldr w21, [x8, #0xcc] # w21 = [0x17DB04]

# These read from different cache locations. Let me map the entire cache layout
# and find the "自身" address display

# Let me disassemble from 0x61900 to 0x61D00 to see the full display function
ctx_start = 0x61900
ctx_end = 0x61D00
code_slice = bytes(text_code[ctx_start - TEXT_START:ctx_end - TEXT_START])
instructions = list(md.disasm(code_slice, ctx_start))

print("=" * 70)
print("辅助数据模块显示函数 (0x61900-0x61D00)")
print("=" * 70)

# Find all ADRP+ADD that resolve to addresses in the 0x17C000-0x17F000 cache range
cache_reads = {}  # address -> list of (vaddr, mnemonic, ops)

for insn in instructions:
    if insn.mnemonic == 'ldr' and '[' in insn.op_str:
        ops = insn.op_str
        brack = ops[ops.index('[')+1:ops.index(']')]
        base = brack.split(',')[0].strip()
        
        # Trace this base back to ADRP
        for prev in instructions:
            if prev.address >= insn.address:
                break
            if prev.mnemonic == 'ldr':
                dst = prev.op_str.split(',')[0].strip()
                if dst == base and '[' in prev.op_str:
                    # Check if this base comes from ADRP
                    for prev2 in instructions:
                        if prev2.address >= prev.address:
                            break
                        if prev2.mnemonic == 'mov':
                            dst2 = prev2.op_str.split(',')[0].strip()
                            if dst2 == prev.op_str[prev.op_str.index('[')+1:prev.op_str.index(']')].split(',')[0].strip():
                                break
    # Simpler: just print all LDRs from non-stack addresses
    pass

# Better approach: just print the whole function with annotations
print("\n关键区域 (0x61AC0 - 0x61C00):")
print("-" * 70)

for insn in instructions:
    if 0x61AC0 <= insn.address <= 0x61C00:
        # Annotate
        note = ""
        if insn.mnemonic == 'adrp':
            # Calculate target page
            for op in insn.operands:
                if op.type == ARM64_OP_IMM:
                    page_off = op.imm
                    page = (insn.address & ~0xFFF) + page_off
                    note = f"  ; -> page 0x{page:X}"
        elif insn.mnemonic == 'add' and '#' in insn.op_str:
            # Check if it's a string reference addition
            note = ""
        elif insn.mnemonic == 'ldr' and '[' in insn.op_str:
            note = "  ; [内存读]"
        elif insn.mnemonic == 'bl':
            note = "  ; [函数调用]"
        
        print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str:35s}{note}")

# Now let me specifically trace "自身" display
# 0x61ADC: adrp x0, #0x1a000  ; page for "自身" string
# 0x61AE0: add x0, x0, #0xd8  ; "自身:%lx" or similar
# 0x61AE4: mov w1, w20        ; w20 = some value (maybe player address?)
# 0x61AE8: bl #0xbb460        ; printf-like

# Let me check what string is at 0x1A000 + 0xD8
str_addr = 0x1A000 + 0xD8
str_start = str_addr
str_end = str_addr + 50
actual_str = data[str_start:str_end]
try:
    null_idx = actual_str.index(0)
    s = actual_str[:null_idx].decode('utf-8', errors='replace')
    print(f"\n字符串 @0x{str_addr:X}: '{s}'")
except:
    print(f"\n字符串 @0x{str_addr:X}: {actual_str[:20].hex()}")

# Also check what 0x17DA38 holds (where w20 comes from)
# w20 = [x8] where x8 = ADRP 0x17D000 + 0xA38 = 0x17DA38
# This is in the data segment - we can't know the runtime value, 
# but we can check the initial value from the binary
data_addr = 0x17DA38
if data_addr < len(data):
    val = struct.unpack_from("<I", data, data_addr)[0]
    print(f"0x17DA38 (w20 来源) 初始值: 0x{val:X} ({val})")

# Also check what's at 0x17D000 + 0xCC = 0x17D0CC for w21
data_addr2 = 0x17DB04
if data_addr2 < len(data):
    val2 = struct.unpack_from("<I", data, data_addr2)[0]
    print(f"0x17DB04 (w21 来源, offset 0xCC) 初始值: 0x{val2:X} ({val2})")

# Most importantly: let's find where the PLAYER OBJECT ADDRESS is stored
# Search for "%lx" format strings near the display function
print("\n" + "=" * 70)
print("搜索 %lx 格式字符串 (用于显示对象地址)")
print("=" * 70)

for i in range(len(data)-4):
    if data[i:i+3] == b'%lx':
        start = max(0, i - 20)
        end = min(len(data), i + 10)
        ctx = data[start:end]
        try:
            s = ctx.decode('utf-8', errors='replace')
            if any('\u4e00' <= c <= '\u9fff' for c in s):
                print(f"  0x{i:X}: '{s}'")
        except:
            pass
