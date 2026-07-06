#!/usr/bin/env python3
"""追踪哈基米中"当前动作: %d"的代码引用，找到动作ID的读取方式"""
import struct
from capstone import *
from capstone.arm64 import *

BIN = "E:/ImGuiOverlay/hjm_v2.3.sh"
with open(BIN, "rb") as f:
    data = f.read()

# ===================== 工具函数 =====================
def sign_extend_21(val):
    if val & (1 << 20): return val - (1 << 21)
    return val

def decode_adrp(raw):
    immhi = (raw >> 5) & 0x7FFFF
    immlo = (raw >> 29) & 0x3
    imm = sign_extend_21((immhi << 2) | immlo)
    return raw & 0x1F, imm << 12  # (rd, page_offset)

def decode_add_imm(raw):
    # ADD Xd, Xn, #imm12  (no shift): 10010001 00 xxxxx xxxxxx xxxxxxxx
    # bit 23 must be 0 for 12-bit immediate (not shifted)
    if ((raw >> 24) & 0x7F) != 0x11: return None
    if (raw >> 23) & 1: return None  # shifted, not our case
    imm12 = (raw >> 10) & 0xFFF
    rn = (raw >> 5) & 0x1F
    rd = raw & 0x1F
    return rd, rn, imm12

# ===================== 目标字符串 =====================
TARGETS = [
    ("当前动作: %d", 0x171D7),
    ("监管动作: %d", 0x1D2B2),
    ("[辅助数据]", 0x1B334),
]

print("目标字符串:")
for name, off in TARGETS:
    print(f"  '{name}' at file/vaddr 0x{off:X}, page=0x{off & ~0xFFF:X}, lo12=0x{off & 0xFFF:X}")

# ===================== 在 .text 范围扫描 ADRP =====================
# From earlier analysis: .text is at file offset ~0x47DE0
# But since the first LOAD segment is file 0x0 vaddr 0x0, file==vaddr for code in first segment
# Let's scan from 0x47DE0 where .text starts (after .rodata ends around 0x25A84)
TEXT_START = 0x47DE0  # .text file offset
TEXT_END = 0x128400    # end of first LOAD segment

print(f"\n扫描范围: 0x{TEXT_START:X} - 0x{TEXT_END:X}")

text_code = data[TEXT_START:TEXT_END]
instr_words = [struct.unpack_from("<I", text_code, i*4)[0] for i in range(len(text_code)//4)]

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True

# ===================== 搜索 =====================
all_hits = []  # (str_name, adrp_offset, add_offset, adrp_instr_word)

for i in range(len(instr_words) - 30):
    raw = instr_words[i]
    if (raw & 0x9F000000) != 0x90000000:
        continue
    
    rd, page_off = decode_adrp(raw)
    instr_addr = TEXT_START + i * 4
    target_page = (instr_addr & ~0xFFF) + page_off
    
    for name, str_off in TARGETS:
        if target_page != (str_off & ~0xFFF):
            continue
        
        # ADRP matches this string's page. Look for matching ADD
        for j in range(i+1, min(i+50, len(instr_words))):
            raw2 = instr_words[j]
            result = decode_add_imm(raw2)
            if result and result[1] == rd:  # same source register
                if target_page + result[2] == str_off:
                    all_hits.append((name, i, j, instr_addr, raw, raw2))
                    break
        break

print(f"\n找到 {len(all_hits)} 处 ADRP+ADD 引用字符串:")
for name, i, j, addr, _, _ in all_hits:
    print(f"  '{name}' ADRP@+0x{i*4:X}(0x{addr:X}) ADD@+0x{j*4:X}")

# ===================== 对每个匹配进行深度分析 =====================
print("\n" + "="*70)
print("深度分析: 反汇编引用点周围代码")
print("="*70)

for hit_idx, (name, i, j, base_addr, _, _) in enumerate(all_hits):
    print(f"\n{'='*70}")
    print(f"[{hit_idx}] 分析 '{name}' (ADRP@0x{base_addr:X})")
    print(f"{'='*70}")
    
    # Disassemble ~30 instr before ADRP to ~40 instr after ADD
    start_off = max(0, (i - 30) * 4)
    end_off = min(len(text_code), (j + 40) * 4)
    
    code_slice = bytes(text_code[start_off:end_off])
    addr_start = TEXT_START + start_off
    
    instructions = list(md.disasm(code_slice, addr_start))
    
    adrp_addr = TEXT_START + i * 4
    add_addr = TEXT_START + j * 4
    
    # Print disassembly
    for insn in instructions:
        rel = (insn.address - adrp_addr) // 4
        marker = ""
        if insn.address == adrp_addr:
            marker = "  <<< ADRP(字符串引用)"
        elif insn.address == add_addr:
            marker = "  <<< ADD(完成地址)"
        
        if -25 <= rel <= 30:
            print(f"  {rel:+4d} | 0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str:35s}{marker}")
    
    # === 关键分析: 追踪 %d 参数 (X1/W1) 的来源 ===
    print(f"\n  --- 追踪 %d 参数 (W1/X1) 来源 ---")
    
    # Find all instructions after ADD that use X1/W1
    # and trace back to the memory read
    w1_setters = []
    w1_chain = []  # for tracing MOV/MOVK chains
    
    for insn in instructions:
        if insn.address > add_addr:
            ops = insn.op_str
            # Check if this instruction writes to X1 or W1
            if insn.mnemonic in ('blr', 'bl', 'ret'):
                # Print context before the call
                if w1_setters:
                    print(f"    调用前 W1 写入:")
                    for s in w1_setters:
                        print(f"      0x{s[0]:X}: {s[1]:8s} {s[2]}")
                break
            
            # Track W1/X1 writes
            # For ARM64, W1 is the lower 32 bits of X1
            writes_w1 = False
            for op in insn.operands:
                if op.type == ARM64_OP_REG:
                    reg_name = insn.reg_name(op.reg)
                    if reg_name in ('w1', 'x1'):
                        writes_w1 = True
                        break
            
            if writes_w1:
                w1_setters.append((insn.address, insn.mnemonic, insn.op_str))
    
    # === 向后追溯: 从 X1 的来源追踪到内存读取 ===
    print(f"\n  --- 向后追溯数据流 ---")
    
    # Trace back from the W1 setter
    # Common pattern: LDR W1, [Xn, #offset] where Xn is loaded from game memory
    # Or: MOV W1, Wx where Wx was loaded from game memory
    
    # Let's find the complete data flow
    # 1. Look for LDR Wx, [Xy, #off] instructions that feed into W1
    # 2. Then trace Xy to find what object it points to
    
    # Find all LDR instructions that load into registers used by W1
    traced_regs = {}  # reg -> (source_insn_addr, mnemonic, op_str)
    
    for insn in instructions:
        if insn.address <= add_addr:
            # LDR Wt, [Xn, #offset] 
            if insn.mnemonic == 'ldr':
                ops = insn.op_str
                # Parse "wX, [xY, #Z]" or "xX, [xY, #Z]"
                if '[' in ops:
                    dst = ops.split(',')[0].strip()
                    traced_regs[dst] = (insn.address, insn.mnemonic, insn.op_str)
    
    # Now check if any W1 setter uses a register that was LDR-loaded
    for w1_addr, w1_mnem, w1_ops in w1_setters:
        print(f"    W1写入: 0x{w1_addr:X}: {w1_mnem} {w1_ops}")
        
        # If MOV W1, Wx, check if Wx was LDR-loaded
        if w1_mnem == 'mov':
            parts = w1_ops.split(',')
            if len(parts) == 2:
                src = parts[1].strip()
                if src in traced_regs:
                    ld_addr, ld_mnem, ld_ops = traced_regs[src]
                    print(f"      -> 来源: 0x{ld_addr:X}: {ld_mnem} {ld_ops}")
                    
                    # Parse the offset from LDR
                    if '#' in ld_ops:
                        off_str = ld_ops.split('#')[1].split(']')[0].strip()
                        try:
                            offset_val = int(off_str, 0)
                            print(f"      >>> 读取偏移: 0x{offset_val:X} ({offset_val})")
                        except:
                            print(f"      偏移: {off_str}")
        
        # If it's an LDR itself
        if w1_mnem == 'ldr' and '#' in w1_ops:
            off_str = w1_ops.split('#')[1].split(']')[0].strip()
            try:
                offset_val = int(off_str, 0)
                print(f"      >>> W1 直接 LDR 偏移: 0x{offset_val:X} ({offset_val})")
            except:
                print(f"      W1 直接 LDR: {w1_ops}")
    
    # Also look for the base register (Xn in LDR [Xn, #off])
    # Trace back to find what object pointer this is
    print(f"\n  --- 基址寄存器追踪 ---")
    base_regs_loaded = {}
    for insn in instructions:
        if insn.address <= add_addr:
            if insn.mnemonic == 'ldr':
                ops = insn.op_str
                if '[' in ops:
                    # Extract base register from [xN, #off]
                    bracket = ops[ops.index('[')+1:ops.index(']')]
                    base = bracket.split(',')[0].strip()
                    if base not in base_regs_loaded:
                        base_regs_loaded[base] = (insn.address, insn.mnemonic, insn.op_str)
    
    # For each base register in the W1-related LDRs
    for w1_addr, w1_mnem, w1_ops in w1_setters:
        if w1_mnem == 'ldr' and '[' in w1_ops:
            bracket = w1_ops[w1_ops.index('[')+1:w1_ops.index(']')]
            base = bracket.split(',')[0].strip()
            print(f"    W1 LDR 基址: {base}")
            
            # Look for where this base register was loaded
            for insn in instructions:
                if insn.address < w1_addr:
                    if insn.mnemonic == 'ldr':
                        ops = insn.op_str
                        dst = ops.split(',')[0].strip()
                        if dst == base and '[' in ops:
                            print(f"      -> {base} 来自: 0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
                            bracket2 = ops[ops.index('[')+1:ops.index(']')]
                            base2 = bracket2.split(',')[0].strip()
                            off2 = bracket2.split(',')[1].strip().lstrip('#')
                            try:
                                print(f"      >>> 第2级偏移: 0x{int(off2,0):X} ({off2})")
                            except:
                                print(f"      第2级偏移: {off2}")
                            
                            # Trace base2
                            for insn2 in instructions:
                                if insn2.address < insn.address:
                                    if insn2.mnemonic == 'ldr':
                                        ops2 = insn2.op_str
                                        dst2 = ops2.split(',')[0].strip()
                                        if dst2 == base2 and '[' in ops2:
                                            print(f"        -> {base2} 来自: 0x{insn2.address:X}: {insn2.mnemonic} {insn2.op_str}")
                                            bracket3 = ops2[ops2.index('[')+1:ops2.index(']')]
                                            base3 = bracket3.split(',')[0].strip()
                                            off3 = bracket3.split(',')[1].strip().lstrip('#')
                                            try:
                                                print(f"        >>> 第3级偏移: 0x{int(off3,0):X} ({off3})")
                                            except:
                                                print(f"        第3级偏移: {off3}")
                                            
                                            # One more level
                                            for insn3 in instructions:
                                                if insn3.address < insn2.address:
                                                    if insn3.mnemonic == 'ldr':
                                                        ops3 = insn3.op_str
                                                        dst3 = ops3.split(',')[0].strip()
                                                        if dst3 == base3 and '[' in ops3:
                                                            bracket4 = ops3[ops3.index('[')+1:ops3.index(']')]
                                                            base4 = bracket4.split(',')[0].strip()
                                                            off4 = bracket4.split(',')[1].strip().lstrip('#')
                                                            try:
                                                                print(f"          -> {base3} 来自: 0x{insn3.address:X}: {insn3.mnemonic} {insn3.op_str}")
                                                                print(f"          >>> 第4级偏移(对象): 0x{int(off4,0):X} ({off4})")
                                                            except:
                                                                pass
                                                            break
                                            break
                            break
