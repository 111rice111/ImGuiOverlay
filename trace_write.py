#!/usr/bin/env python3
"""找到写入 0x17E280 (动作ID缓存) 的 STR 指令"""
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
TARGET = 0x17E280

def sign_extend_21(val):
    if val & (1 << 20): return val - (1 << 21)
    return val

def decode_adrp(raw):
    immhi = (raw >> 5) & 0x7FFFF
    immlo = (raw >> 29) & 0x3
    return raw & 0x1F, sign_extend_21((immhi << 2) | immlo) << 12

text_code = data[TEXT_START:TEXT_END]
instr_words = [struct.unpack_from("<I", text_code, i*4)[0] for i in range(len(text_code)//4)]

# For each instruction, if it's a STR/STP to [Xy, #off] where Xy comes from ADRP to 0x17E000,
# check if the final address matches 0x17E280
print("搜索 STR/STP 写入 0x17E280...")

for i, raw in enumerate(instr_words):
    i_addr = TEXT_START + i * 4
    
    # Decode with capstone for STR/STP instructions
    # STR Wt, [Xn, #off] and STR Xt, [Xn, #off]
    # STR (register): doesn't matter
    # STP Wt1, Wt2, [Xn, #off]
    
    # Check STP (store pair) - 64-bit: 10101001 00 xxxxxx xxttttt ttttttnnnnn
    # STP 32-bit:     00101001 00 xxxxxx xxttttt ttttttnnnnn
    # STP 64-bit:     10101001 00
    # STR 32-bit unscaled: 10111000 00
    # STR 64-bit unscaled: 11111000 00
    
    op_31_22 = (raw >> 22) & 0x3FF
    
    is_str = False
    is_str_w = False
    is_stp = False
    base_reg = None
    offset = 0
    
    # STP (32-bit): 00101001 00xxxxxx
    if (raw >> 22) == 0x0A4:  # 00101001 00 = 0xA4
        is_stp = True
        base_reg = (raw >> 5) & 0x1F
        imm7 = (raw >> 15) & 0x7F
        if imm7 & 0x40: imm7 -= 0x80
        offset = imm7 << 2  # scaled by 4 for 32-bit
    # STP (64-bit): 10101001 00xxxxxx  
    elif (raw >> 22) == 0x2A4:  # 10101001 00 = 0x2A4
        is_stp = True
        base_reg = (raw >> 5) & 0x1F
        imm7 = (raw >> 15) & 0x7F
        if imm7 & 0x40: imm7 -= 0x80
        offset = imm7 << 3  # scaled by 8 for 64-bit
    # STR (32-bit unsigned): 10111001 01
    elif ((raw >> 22) & 0x3F) == 0x2E:  # 101110 01
        is_str_w = True
        base_reg = (raw >> 5) & 0x1F
        imm12 = (raw >> 10) & 0xFFF
        offset = imm12 << 2
    # STR (64-bit unsigned): 11111001 01  
    elif ((raw >> 22) & 0x7F) == 0x7A:  # 111110 01
        is_str_w = True
        base_reg = (raw >> 5) & 0x1F
        imm12 = (raw >> 10) & 0xFFF
        offset = imm12 << 3
    
    if not (is_str_w or is_stp):
        continue
    
    if base_reg is None:
        continue
    
    # Find ADRP that sets this base register to 0x17E000 page
    for j in range(max(0, i-30), i):
        raw2 = instr_words[j]
        if (raw2 & 0x9F000000) == 0x90000000:
            rd2, page_off = decode_adrp(raw2)
            prev_addr = TEXT_START + j * 4
            target_page = (prev_addr & ~0xFFF) + page_off
            if target_page == 0x17E000 and rd2 == base_reg:
                # Check if there's an ADD between ADRP and STR
                for k in range(j+1, i):
                    raw3 = instr_words[k]
                    if ((raw3 >> 24) & 0x7F) == 0x11 and ((raw3 >> 23) & 1) == 0:
                        rd3 = raw3 & 0x1F
                        rn3 = (raw3 >> 5) & 0x1F
                        imm12_3 = (raw3 >> 10) & 0xFFF
                        if rn3 == rd2 and rd3 == base_reg:
                            # Final resolved address
                            resolved = 0x17E000 + imm12_3 + offset
                            
                            if resolved == TARGET or abs(resolved - TARGET) < 0x10:
                                final_addr = TEXT_START + i * 4
                                print(f"\n{'='*70}")
                                print(f"★ 找到写入 0x{resolved:X}!")
                                print(f"  ADRP@0x{prev_addr:X} -> 0x17E000")
                                print(f"  ADD @0x{TEXT_START + k*4:X} -> +0x{imm12_3:X}")
                                print(f"  {'STP' if is_stp else 'STR'} @0x{final_addr:X} -> +0x{offset:X}")
                                print(f"  最终地址: 0x{resolved:X}")
                                print(f"{'='*70}")
                                
                                # Disassemble context
                                ctx_s = max(0, (j - 40) * 4)
                                ctx_e = min(len(text_code), (i + 15) * 4)
                                ctx_code = bytes(text_code[ctx_s:ctx_e])
                                ctx_addr = TEXT_START + ctx_s
                                
                                for insn in md.disasm(ctx_code, ctx_addr):
                                    rel = (insn.address - final_addr) // 4
                                    markers = []
                                    if insn.address == prev_addr: markers.append("ADRP")
                                    if insn.address == TEXT_START + k*4: markers.append("ADD")
                                    if insn.address == final_addr: markers.append("STP/STR")
                                    mk = " <<< " + ",".join(markers) if markers else ""
                                    if -30 <= rel <= 10:
                                        print(f"  {rel:+4d} | 0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str:35s}{mk}")
                                
                                # Trace the source register
                                if is_stp:
                                    rt1 = raw & 0x1F
                                    rt2 = (raw >> 10) & 0x1F
                                    print(f"\n  写入寄存器: r{rt1}, r{rt2}")
                                else:
                                    rt = raw & 0x1F
                                    is_64 = (raw >> 30) & 1
                                    reg = f'x{rt}' if is_64 else f'w{rt}'
                                    print(f"\n  写入寄存器: {reg}")
                                    
                                    # Trace back the source
                                    for insn in md.disasm(ctx_code, ctx_addr):
                                        if insn.address >= final_addr:
                                            break
                                        for op in insn.operands:
                                            if op.type == ARM64_OP_REG:
                                                rn_name = insn.reg_name(op.reg)
                                                if rn_name == reg:
                                                    if insn.mnemonic == 'ldr':
                                                        print(f"  >>> 源寄存器来自: 0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
                                                        
                                                        # Parse the LDR to find game memory access
                                                        ops = insn.op_str
                                                        if '[' in ops:
                                                            brack = ops[ops.index('[')+1:ops.index(']')]
                                                            parts = brack.split(',')
                                                            src_base = parts[0].strip()
                                                            src_off = parts[1].strip().lstrip('#') if len(parts) > 1 else '0'
                                                            print(f"  >>> 游戏内存: [{src_base}, #{src_off}]")
                                                            
                                                            # Trace src_base further
                                                            for insn2 in md.disasm(ctx_code, ctx_addr):
                                                                if insn2.address >= insn.address:
                                                                    break
                                                                for op2 in insn2.operands:
                                                                    if op2.type == ARM64_OP_REG:
                                                                        if insn2.reg_name(op2.reg) == src_base:
                                                                            if insn2.mnemonic == 'ldr' and '[' in insn2.op_str:
                                                                                ops2 = insn2.op_str
                                                                                brack2 = ops2[ops2.index('[')+1:ops2.index(']')]
                                                                                parts2 = brack2.split(',')
                                                                                print(f"  >>> {src_base} = LDR [{parts2[0]}, #{parts2[1].lstrip('#')}]")
                                                                            elif insn2.mnemonic in ('mov', 'add'):
                                                                                print(f"  >>> {src_base} = {insn2.mnemonic} {insn2.op_str}")
                                break
                break

print("\n\n搜索完成")
