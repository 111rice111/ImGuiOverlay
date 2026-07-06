#!/usr/bin/env python3
"""在哈基米二进制中搜索"辅助数据""当前动作"等UI字符串，找动作ID读取代码"""
import struct

BIN = "E:/ImGuiOverlay/hjm_v2.3.sh"

with open(BIN, "rb") as f:
    data = f.read()

# 搜索所有相关字符串
keywords = [
    "当前动作",
    "辅助数据",
    "监管动作", 
    "动作",
    "Action",
    "当前",
]

print("=" * 60)
print("搜索 UI 相关字符串")
print("=" * 60)

for kw in keywords:
    kbytes = kw.encode("utf-8")
    pos = 0
    found = []
    while True:
        idx = data.find(kbytes, pos)
        if idx == -1:
            break
        found.append(idx)
        pos = idx + 1
    if found:
        print(f"'{kw}': {len(found)} 处")
        for off in found:
            # 显示上下文
            start = max(0, off - 4)
            end = min(len(data), off + len(kbytes) + 20)
            ctx = data[start:end]
            print(f"  0x{off:06X}: ...{ctx.hex(' ')}...")
            # 尝试 UTF-8 解码
            try:
                s = ctx.decode('utf-8', errors='replace')
                print(f"         '{s}'")
            except:
                pass

# 搜索 format 字符串 "%d" 附近的内容
print("\n" + "=" * 60)
print("搜索含 %d 的字符串（可能是 printf/sprintf 格式）")
print("=" * 60)
fd_bytes = "%d".encode("utf-8")
pos = 0
while True:
    idx = data.find(fd_bytes, pos)
    if idx == -1:
        break
    start = max(0, idx - 30)
    end = min(len(data), idx + 10)
    ctx = data[start:end]
    try:
        s = ctx.decode('utf-8', errors='replace')
        # 只显示含中文的
        if any('\u4e00' <= c <= '\u9fff' for c in s):
            print(f"  0x{idx:06X}: '{s}'")
    except:
        pass
    pos = idx + 1
