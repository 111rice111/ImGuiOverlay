#!/usr/bin/env python3
"""追踪"有效对象: %d"字符串引用 — 找到哈基米的扫描函数，看动作ID怎么读的"""
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

def sign_extend_21(val):
    if val & (1 << 20): return val - (1 << 21)
    return val

def decode_adrp(raw):
    immhi = (raw >> 5) & 0x7FFFF
    immlo = (raw >> 29) & 0x3
    return raw & 0x1F, sign_extend_21((immhi << 2) | immlo) << 12

text_code = data[TEXT_START:TEXT_END]
instr_words = [struct.unpack_from("<I", text_code, i*4)[0] for i in range(len(text_code)//4)]

# "有效对象: %d" at 0x1A0E6
# "自身" at around 0x1A0xx
STR_EFFECTIVE = 0x1A0E6
STR_PAGE = 0x1A000

print("=" * 70)
print("追踪 '有效对象: %d' (0x1A0E6) 和 '自身' 字符串")
print("=" * 70)

# Find ADRP+ADD to "有效对象: %d"
effective_hits = []
for i, raw in enumerate(instr_words):
    if (raw & 0x9F000000) != 0x90000000:
        continue
    rd, page_off = decode_adrp(raw)
    i_addr = TEXT_START + i * 4
    target_page = (i_addr & ~0xFFF) + page_off
    
    if target_page != STR_PAGE:
        continue
    
    for j in range(i+1, min(i+30, len(instr_words))):
        raw2 = instr_words[j]
        if ((raw2 >> 24) & 0x7F) == 0x11 and ((raw2 >> 23) & 1) == 0:
            rn = (raw2 >> 5) & 0x1F
            imm12 = (raw2 >> 10) & 0xFFF
            if rn == rd and STR_PAGE + imm12 == STR_EFFECTIVE:
                effective_hits.append((i, j, i_addr, TEXT_START + j*4))

print(f"'有效对象: %d' 引用: {len(effective_hits)} 处")
for i, j, a1, a2 in effective_hits:
    print(f"  ADRP@0x{a1:X} ADD@0x{a2:X}")

# Search for "自身" string 
# "自身" in UTF-8: E8 87 AA E8 BA AB
self_bytes = "自身".encode('utf-8')
self_offsets = []
pos = 0
while True:
    idx = data.find(self_bytes, pos)
    if idx == -1: break
    self_offsets.append(idx)
    pos = idx + 1

print(f"\n'自身' 字符串位置: {[f'0x{x:X}' for x in self_offsets]}")

# Now search for "自身" references
# Let me focus on the one near "有效对象" context
for self_off in self_offsets:
    if 0x1A000 <= self_off <= 0x1A200:
        print(f"\n追踪 '自身' @0x{self_off:X} 的 ADRP 引用...")
        self_page = self_off & ~0xFFF
        for i, raw in enumerate(instr_words):
            if (raw & 0x9F000000) != 0x90000000: continue
            rd, page_off = decode_adrp(raw)
            i_addr = TEXT_START + i * 4
            target_page = (i_addr & ~0xFFF) + page_off
            if target_page != self_page: continue
            
            for j in range(i+1, min(i+30, len(instr_words))):
                raw2 = instr_words[j]
                if ((raw2 >> 24) & 0x7F) == 0x11 and ((raw2 >> 23) & 1) == 0:
                    rn = (raw2 >> 5) & 0x1F
                    imm12 = (raw2 >> 10) & 0xFFF
                    if rn == rd and self_page + imm12 == self_off:
                        print(f"  ADRP+ADD '自身' @0x{i_addr:X}")

# Now let's focus on "有效对象: %d" hits and disassemble the function
print("\n" + "=" * 70)
print("分析 '有效对象: %d' 所在函数")
print("=" * 70)

for hit_idx, (i, j, adrp_addr, add_addr) in enumerate(effective_hits):
    # Disassemble wide context - we want the entire function
    # Look for function start (STP/SUB SP pattern) going backwards
    func_start = i
    for k in range(i, max(0, i-200), -1):
        raw_k = instr_words[k]
        # STP X29, X30, [SP, #...] or SUB SP, SP, #...
        if (raw_k >> 22) == 0x2A8:  # STP 64-bit with SP base
            if (raw_k & 0x1F) == 31:  # SP base
                func_start = k
                break
    
    # Disassemble from func_start to ~200 instructions after
    ctx_s = max(0, (func_start - 2) * 4)
    ctx_e = min(len(text_code), (i + 200) * 4)
    ctx_code = bytes(text_code[ctx_s:ctx_e])
    ctx_addr = TEXT_START + ctx_s
    
    instructions = list(md.disasm(ctx_code, ctx_addr))
    
    # Filter: only show relevant parts - look for LDR instructions that access game memory
    # Specifically, look for patterns like:
    # LDR Xx, [some_global]  ; load game pointer
    # LDR Wx, [Xx, #offset]  ; read value from game
    
    game_ldrs = []
    for insn in instructions:
        if insn.mnemonic == 'ldr' and '[' in insn.op_str:
            # Check if loading from non-SP, non-FP register
            ops = insn.op_str
            brack = ops[ops.index('[')+1:ops.index(']')]
            base = brack.split(',')[0].strip()
            if base not in ('sp', 'x29', 'fp'):
                # Check the offset value
                parts = brack.split(',')
                if len(parts) > 1:
                    off_str = parts[1].strip().lstrip('#')
                    try:
                        off_val = int(off_str, 0)
                        if off_val > 0:  # non-zero offset = game field access
                            game_ldrs.append((insn.address, insn.mnemonic, insn.op_str, off_val))
                    except:
                        pass
    
    if not game_ldrs:
        print(f"\n[Hit {hit_idx}] 函数@0x{ctx_addr:X} - 范围内无游戏内存访问")
        continue
    
    print(f"\n[Hit {hit_idx}] 函数@0x{ctx_addr:X}")
    print("  游戏内存 LDR 访问:")
    
    # Show unique offsets
    seen_offsets = set()
    for addr, mnem, ops, off_val in game_ldrs:
        if off_val not in seen_offsets:
            seen_offsets.add(off_val)
            print(f"    0x{addr:X}: {mnem} {ops} (offset=0x{off_val:X})")
    
    # Disassemble the full function
    print(f"\n  完整反汇编:")
    for insn in instructions:
        # Skip to relevant area
        rel = (insn.address - add_addr) // 4
        if -100 <= rel <= 30:
            marker = ""
            if insn.address == add_addr: marker = " <<< '有效对象'"
            # Highlight game memory loads
            for addr, _, _, _ in game_ldrs:
                if insn.address == addr:
                    marker = " <<< 游戏内存读"
                    break
            print(f"    {rel:+4d} | 0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str:35s}{marker}")
    
    print()

# Let's also look at the broader scan loop
# Search for "自身: %lx" or similar patterns that would indicate self-object scanning
print("\n" + "=" * 70)
print("寻找 '自身' 相关代码 — 扫描玩家对象的循环")
print("=" * 70)

# Search for string "自身" at 0x1A0xx area 
# The pattern would be: ADRP X0, #page -> ADD X0, X0, #off -> (print "自身: %lx", obj_addr)
# or "描当前对象" at 0x1D2F0

# "扫描当前对象" at 0x1D2F0
SCAN_CUR = 0x1D2F0
SCAN_PAGE = 0x1D000

print(f"\n搜索 '扫描当前对象' (0x{SCAN_CUR:X})...")
for i, raw in enumerate(instr_words):
    if (raw & 0x9F000000) != 0x90000000: continue
    rd, page_off = decode_adrp(raw)
    i_addr = TEXT_START + i * 4
    target_page = (i_addr & ~0xFFF) + page_off
    if target_page != SCAN_PAGE: continue
    
    for j in range(i+1, min(i+30, len(instr_words))):
        raw2 = instr_words[j]
        if ((raw2 >> 24) & 0x7F) == 0x11 and ((raw2 >> 23) & 1) == 0:
            rn = (raw2 >> 5) & 0x1F
            imm12 = (raw2 >> 10) & 0xFFF
            if rn == rd and SCAN_PAGE + imm12 == SCAN_CUR:
                print(f"  找到! ADRP@0x{i_addr:X} ADD@0x{TEXT_START + j*4:X}")
                
                # Disassemble this function
                ctx_s2 = max(0, (i - 100) * 4)
                ctx_e2 = min(len(text_code), (i + 150) * 4)
                ctx_code2 = bytes(text_code[ctx_s2:ctx_e2])
                ctx_addr2 = TEXT_START + ctx_s2
                
                print(f"  函数@0x{ctx_addr2:X}:")
                for insn in md.disasm(ctx_code2, ctx_addr2):
                    rel = (insn.address - i_addr) // 4
                    if -30 <= rel <= 40:
                        mk = " <<< '扫描当前对象'" if insn.address == i_addr else ""
                        if insn.mnemonic == 'ldr' and '[' in insn.op_str:
                            mk += " [LDR]"
                        print(f"    {rel:+4d} | 0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str:35s}{mk}")
                break
