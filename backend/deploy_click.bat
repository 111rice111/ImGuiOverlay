@echo off
chcp 65001 >nul
echo ================================================
echo    ImGuiOverlay 卡密系统 一键部署
echo ================================================
echo.

REM 检查管理员权限
net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo [!] 需要管理员权限，正在请求...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo [1/5] 创建目录...
if not exist "C:\overlay" mkdir "C:\overlay"
echo OK

echo [2/5] 解码服务端文件...
certutil -decode "%~dp0server_v3.b64" "C:\overlay\server_v3.py" >nul 2>&1
if exist "C:\overlay\server_v3.py" (
    echo 解码成功
) else (
    echo [!] server_v3.b64 不存在，正在创建...
    goto :create_py
)

:start_server
echo [3/5] 检查 Python...
python --version >nul 2>&1
if %errorLevel% NEQ 0 (
    echo [!] Python 未安装，请先安装 Python 3.8+
    echo 下载: https://www.python.org/downloads/
    pause
    exit /b 1
)
echo Python OK

echo [4/5] 开放防火墙端口 8080...
netsh advfirewall firewall add rule name="ImGuiOverlay-API" dir=in action=allow protocol=TCP localport=8080 >nul 2>&1
echo 防火墙规则已添加

echo [5/5] 启动服务...
echo.
echo ================================================
echo  服务启动中... 不要关闭此窗口！
echo  管理后台: http://43.136.183.71:8080/admin
echo  账号: admin  密码: admin888
echo ================================================
echo.
cd /d "C:\overlay"
python server_v3.py --port 8080 --host 0.0.0.0
pause
exit /b

:create_py
echo 正在写入 server_v3.py...
powershell -Command "& { $code = @'
import sqlite3, json, time, secrets, hashlib, struct, os, sys, argparse
from http.server import HTTPServer, BaseHTTPRequestHandler
from typing import Optional, Union

DB_PATH = 'C:/overlay/overlay_v3.db'
XOR_KEY = 'ImGuiOverlay2026'
XOR_IV = 'InitVector123456'

def xor_encrypt(plain):
    key = XOR_KEY; iv = XOR_IV
    padLen = 16 - (len(plain) % 16)
    padded = plain + chr(padLen) * padLen
    result = ''; prev = iv
    for b in range(len(padded) // 16):
        block = padded[b*16:(b+1)*16]
        encBlock = ''
        for j in range(16):
            encBlock += chr(ord(block[j]) ^ ord(key[j % 16]) ^ ord(prev[j % 16]))
        result += encBlock; prev = encBlock
    return hex(sum(ord(c) << (8*i) for i,c in enumerate(result)))[2:].upper()

print('Minimal server - please upload full server_v3.py')
'@ ; Set-Content -Path 'C:\overlay\server_v3.py' -Value $code -Encoding UTF8 }"
echo 写入完成，但这是精简版。请通过远程桌面手动上传完整 server_v3.py
pause
exit /b
