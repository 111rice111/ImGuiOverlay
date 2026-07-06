#!/usr/bin/env python3
"""
分析哈基米 v2.3 二进制，找到"当前动作: %d"和"监管动作: %d"的代码引用，
追踪参数来源，提取动作ID的指针链。

策略：
1. 找到字符串在 .rodata 中的地址
2. 找到 ADRP+ADD 引用这些字符串的代码位置
3. 从每个引用点往前追踪，找到 x1/x2 寄存器值的来源（%d 参数）
4. 识别 LDR 链，提取偏移量序列
"""

import struct
from elftools.elf.elffile import ELFFile
from capstone import *
from capstone.arm64 import *

BINARY_PATH = "E:/ImGuiOverlay/hjm_v2.3.sh"

def load_elf(path):
    with open(path, 'rb') as f:
        data = f.read()
    elf = ELFFile(open(path, 'rb'))
    return elf, data

def get_section_range(elf, name):
    """获取段的起始地址和文件偏移"""
    for sec in elf.iter_sections():
        if sec.name == name:
            return sec['sh_addr'], sec['sh_offset'], sec['sh_size']
    return None, None, None

def addr_to_file_offset(elf, addr):
    """将虚拟地址转换为文件偏移"""
    for seg in elf.iter_segments():
        if seg['p_type'] == 'PT_LOAD':
            start = seg['p_vaddr']
            end = start + seg['p_filesz']
            if start <= addr < end:
                return addr - start + seg['p_offset']
    return None

def find_bytes_in_section(data, section_offset, section_size, search_bytes, section_addr=0):
    """在段中搜索字节序列，返回所有匹配的虚拟地址"""
    results = []
    pos = 0
    while True:
        pos = data[section_offset:section_offset + section_size].find(search_bytes, pos)
        if pos == -1:
            break
        vaddr = section_addr + pos if section_addr else section_offset + pos
        results.append(vaddr)
        pos += 1
    return results

def disasm_code(data, text_addr, text_offset, text_size):
    """反汇编 .text 段"""
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = True
    code = data[text_offset:text_offset + text_size]
    instructions = {}
    for insn in md.disasm(code, text_addr):
        instructions[insn.address] = insn
    return instructions

def find_string_refs(instructions, rodata_addr, rodata_size):
    """找到所有引用 .rodata 字符串的 ADRP+ADD 对"""
    refs = []
    rodata_end = rodata_addr + rodata_size
    
    for addr, insn in instructions.items():
        if insn.mnemonic != 'adrp':
            continue
        
        # ADRP 计算页对齐地址
        page = (insn.operands[1].imm)
        target_page = (addr & ~0xFFF) + page * 0x1000
        
        if not (rodata_addr <= target_page < rodata_end):
            continue
        
        # 找下一条 ADD 指令
        next_addr = addr + 4
        if next_addr in instructions:
            next_insn = instructions[next_addr]
            reg = insn.operands[0].reg
            
            if next_insn.mnemonic == 'add' and next_insn.operands[1].reg == reg:
                page_off = next_insn.operands[2].imm if len(next_insn.operands) > 2 else 0
                actual_addr = target_page + page_off
                refs.append((addr, actual_addr, insn.operands[0].reg))
    
    return refs

def get_function_bounds(instructions, addr):
    """简单估算函数边界：往前找函数入口，往后找函数出口(ret)"""
    if addr not in instructions:
        return None, None
    
    addrs = sorted(instructions.keys())
    idx = addrs.index(addr)
    
    # 往回找函数头（stp x29, x30, [sp, ...] 或 sub sp, sp, ...）
    func_start = addr
    for i in range(idx, -1, -1):
        a = addrs[i]
        insn = instructions[a]
        if insn.mnemonic == 'stp' and 'x29' in insn.op_str and 'x30' in insn.op_str:
            func_start = a
            break
        if insn.mnemonic == 'sub' and 'sp' in insn.op_str:
            func_start = a
            break
        if a < addr - 0x200:
            break
    
    # 往后找 ret
    func_end = addr
    for i in range(idx, len(addrs)):
        a = addrs[i]
        insn = instructions[a]
        if insn.mnemonic == 'ret':
            func_end = a + 4
            break
        if a > addr + 0x400:
            break
    
    return func_start, func_end

def trace_ldr_chain(instructions, ref_addr, target_reg, max_instructions=100):
    """
    从格式化调用点往前追踪，找到传入 %d 参数（x1/x2）的来源。
    返回 LDR 指令的偏移链。
    
    在 ARM64 中，printf("当前动作: %d", val) 的调用约定：
    - x0 = 格式字符串地址
    - x1 = 第一个 %d 参数值
    如果 x0 存放格式字符串地址，则 x1 就是我们要找的动作ID。
    """
    addrs = sorted(instructions.keys())
    idx = addrs.index(ref_addr)
    
    # 先找到 x0 是哪个寄存器（格式字符串的寄存器）
    fmt_reg = None
    for insn in instructions.values():
        if insn.address == ref_addr:
            # 这可能是 ADRP，格式字符串在目标寄存器
            fmt_reg = insn.operands[0].reg
            break
    
    if fmt_reg is None:
        # 如果 ref 周围有 bl printf，找调用前的指令
        pass
    
    # 从 ref 往前追踪 x1 寄存器的值（通常是动作ID参数）
    # 假设 x1 被用作 %d 参数
    chain = []
    current_reg = ARM64_REG_X1  # 先追踪 x1
    found_ldr = False
    
    for i in range(idx, max(0, idx - max_instructions), -1):
        a = addrs[i]
        insn = instructions[a]
        
        # 跳过我们不关心的指令
        if insn.mnemonic in ['adrp', 'add', 'mov', 'nop', 'stp', 'str', 'stur', 'b', 'bl']:
            continue
        
        if insn.mnemonic in ['ldr', 'ldur']:
            # 检查目标寄存器
            dest_reg = insn.operands[0].reg
            if dest_reg == current_reg:
                # 这是一个 LDR，加载值到我们的目标寄存器
                # 提取源寄存器和偏移
                if len(insn.operands) > 1:
                    op1 = insn.operands[1]
                    if op1.type == ARM64_OP_MEM:
                        chain.append({
                            'addr': a,
                            'insn': f"{insn.mnemonic} {insn.op_str}",
                            'base_reg': op1.mem.base,
                            'offset': op1.mem.disp,
                            'dest_reg': dest_reg
                        })
                        current_reg = op1.mem.base  # 追踪基址寄存器
                        found_ldr = True
        
        elif insn.mnemonic == 'ldp':
            # LDP 可能加载一对，检查是否包含我们的寄存器
            for op in insn.operands:
                if op.type == ARM64_OP_REG and op.reg == current_reg:
                    # 找到内存操作数
                    for op2 in insn.operands:
                        if op2.type == ARM64_OP_MEM:
                            chain.append({
                                'addr': a,
                                'insn': f"ldp {insn.op_str}",
                                'base_reg': op2.mem.base,
                                'offset': op2.mem.disp,
                                'dest_reg': current_reg
                            })
                            current_reg = op2.mem.base
                            found_ldr = True
                            break
                    break
    
    return chain

def analyze_string_usage(instructions, str_addr, rodata_addr):
    """分析单个字符串的使用点，追踪动作ID参数来源"""
    results = []
    
    # 1. 找到 ADRP 引用
    refs = find_string_refs(instructions, rodata_addr, 0x20000)
    direct_refs = [r for r in refs if r[1] == str_addr]
    
    for ref_addr, _, fmt_reg in direct_refs:
        # 2. 找到这个引用所在的函数
        func_start, func_end = get_function_bounds(instructions, ref_addr)
        
        if func_start is None:
            continue
        
        # 3. 在这个函数内搜索 BL （函数调用）指令
        addrs = sorted(instructions.keys())
        func_instructions = {a: instructions[a] for a in addrs 
                           if func_start <= a <= (func_end or func_start + 0x400)}
        
        # 4. 找到所有 BL 调用，检查调用前 x0 是否指向格式化字符串
        calls = []
        for addr, insn in func_instructions.items():
            if insn.mnemonic == 'blr' or (insn.mnemonic == 'bl' and addr > ref_addr):
                # 检查调用前几条指令中 x0 的值
                # 往前找 x0 的来源
                x0_source = None
                x1_source = None
                
                for check_idx in range(addr - 4, addr - 20, -4):
                    if check_idx not in func_instructions:
                        break
                    prev = func_instructions[check_idx]
                    
                    if prev.mnemonic in ['add', 'mov']:
                        if len(prev.operands) >= 2:
                            dest = prev.operands[0].reg
                            src = prev.operands[1].reg if len(prev.operands) > 1 else None
                            
                            if dest == ARM64_REG_X0 and src == fmt_reg:
                                x0_source = ('fmt_string', check_idx)
                            if dest == ARM64_REG_X1:
                                x1_source = ('x1_set', check_idx, prev.op_str)
                
                if x0_source and x0_source[0] == 'fmt_string':
                    # 找到了！这是格式化调用，x1 就是动作ID
                    # 往前追踪 x1 的来源
                    chain = trace_ldr_chain(func_instructions, addr, ARM64_REG_X1)
                    
                    results.append({
                        'ref_addr': ref_addr,
                        'call_addr': addr,
                        'x1_set_at': x1_source,
                        'chain': chain
                    })
    
    return results

def main():
    print("=" * 70)
    print("哈基米 v2.3 — 动作ID读取链分析")
    print("=" * 70)
    
    elf, data = load_elf(BINARY_PATH)
    
    # 获取段信息
    text_addr, text_offset, text_size = get_section_range(elf, '.text')
    rodata_addr, rodata_offset, rodata_size = get_section_range(elf, '.rodata')
    
    print(f"\n.text:  addr=0x{text_addr:x}  offset=0x{text_offset:x}  size=0x{text_size:x}")
    print(f".rodata: addr=0x{rodata_addr:x}  offset=0x{rodata_offset:x}  size=0x{rodata_size:x}")
    
    # 找到字符串
    str_current = "当前动作: %d".encode('utf-8') + b'\x00'
    str_supervisor = "监管动作: %d".encode('utf-8') + b'\x00'
    str_action = "当前动作".encode('utf-8')  # 可能变体
    
    current_addrs = find_bytes_in_section(data, rodata_offset, rodata_size, str_current, rodata_addr)
    sv_addrs = find_bytes_in_section(data, rodata_offset, rodata_size, str_supervisor, rodata_addr)
    
    print(f"\n字符串位置:")
    for a in current_addrs:
        print(f"  '当前动作: %d' @ 0x{a:x}")
    for a in sv_addrs:
        print(f"  '监管动作: %d' @ 0x{a:x}")
    
    if not current_addrs:
        print("\n[!] 未找到字符串，尝试搜索短版...")
        current_addrs = find_bytes_in_section(data, rodata_offset, rodata_size, str_action, rodata_addr)
        for a in current_addrs:
            print(f"  '当前动作' @ 0x{a:x}")
    
    # 反汇编 .text
    print(f"\n反汇编 .text 段... (这可能需要几秒钟)")
    instructions = disasm_code(data, text_addr, text_offset, text_size)
    print(f"共 {len(instructions)} 条指令")
    
    # 找到所有 ADRP 引用 .rodata 的位置
    print(f"\n搜索 .rodata 引用...")
    all_refs = find_string_refs(instructions, rodata_addr, rodata_size)
    print(f"共找到 {len(all_refs)} 个 ADRP 引用 .rodata")
    
    # 检查哪些引用指向我们的目标字符串
    for str_addr in current_addrs + sv_addrs:
        matches = [r for r in all_refs if r[1] == str_addr]
        print(f"\n字符串 0x{str_addr:x}: {len(matches)} 个直接引用")
        
        for ref_addr, target, reg in matches:
            func_start, func_end = get_function_bounds(instructions, ref_addr)
            print(f"  ADRP @ 0x{ref_addr:x} (函数: 0x{func_start:x}-0x{func_end:x})")
            
            # 从这个引用点周围找 BL 调用
            func_addrs = sorted([a for a in instructions if func_start <= a <= (func_end or func_start + 0x400)])
            
            for i, a in enumerate(func_addrs):
                insn = instructions[a]
                if insn.mnemonic in ['bl', 'blr'] and a >= ref_addr:
                    # 检查这个调用前 x0, x1 的来源
                    print(f"    函数调用 @ 0x{a:x}: {insn.op_str}")
                    
                    # 往前追踪 x1 (动作ID参数)
                    x1_chain = trace_ldr_chain(instructions, a, ARM64_REG_X1, max_instructions=50)
                    if x1_chain:
                        print(f"    x1 读取链:")
                        for step in x1_chain:
                            print(f"      0x{step['addr']:x}: {step['insn']}  (base=r{step['base_reg']}, off=0x{step['offset']:x})")
                    else:
                        # 也追踪 x0
                        x0_chain = trace_ldr_chain(instructions, a, ARM64_REG_X0, max_instructions=50)
                        if x0_chain:
                            print(f"    x0 读取链:")
                            for step in x0_chain:
                                print(f"      0x{step['addr']:x}: {step['insn']}  (base=r{step['base_reg']}, off=0x{step['offset']:x})")
                    
                    # 打印调用周围的所有LDR指令（辅助分析）
                    print(f"    调用附近的LDR指令:")
                    for check_a in range(a - 0x40, a + 4, 4):
                        if check_a in instructions:
                            ci = instructions[check_a]
                            if ci.mnemonic in ['ldr', 'ldur'] and 'sp' not in ci.op_str:
                                # 提取基址寄存器和偏移
                                for op in ci.operands:
                                    if op.type == ARM64_OP_MEM:
                                        if op.mem.base != ARM64_REG_SP:
                                            print(f"      0x{check_a:x}: {ci.mnemonic} r{ci.operands[0].reg}, [r{op.mem.base}, #0x{op.mem.disp:x}]")
                    break  # 只看第一个调用

if __name__ == '__main__':
    main()
