#!/usr/bin/env python3
"""
卡密加密工具
用法: python encrypt_key.py <卡密> <设备ID>
输出: 加密后的二进制文件, 可 push 到手机 /data/local/tmp/maps/license.key
"""
import sys

if len(sys.argv) < 3:
    print("用法: python encrypt_key.py <卡密> <设备ID>")
    print("  例: python encrypt_key.py DEMO-KEY-001 DEV-A1B2C3D4")
    sys.exit(1)

key = sys.argv[1].encode()
did = sys.argv[2].encode()

# XOR 加密
enc = bytes(key[i] ^ did[i % len(did)] for i in range(len(key)))

with open("license.enc", "wb") as f:
    f.write(enc)

print(f"已生成 license.enc ({len(enc)} bytes)")
print(f"推送到手机: adb push license.enc /data/local/tmp/maps/license.key")
