@echo off
chcp 65001 >nul 2>&1
setlocal EnableDelayedExpansion

:: ============================================================
:: ImGuiOverlay v3.0 — 一键部署脚本 (Windows Server + 宝塔)
:: 用法: 右键 → 以管理员身份运行
:: ============================================================

title ImGuiOverlay v3.0 一键部署
color 0A
echo.
echo  ╔══════════════════════════════════════════════════╗
echo  ║     ImGuiOverlay 卡密系统 v3.0 一键部署            ║
echo  ║     服务器: 43.136.183.71                        ║
echo  ╚══════════════════════════════════════════════════╝
echo.

:: ── Step 1: 检查/安装 Python ──
echo [1/5] 检查 Python 环境...
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo    Python 未检测到, 尝试查找已安装的 Python...
    where python3 >nul 2>&1 && set "PY=python3" || (
        for %%p in (C:\Python3*\python.exe C:\Python3*\Scripts\python.exe C:\Users\Administrator\AppData\Local\Programs\Python\Python3*\python.exe) do (
            if exist "%%p" set "PY=%%p"
        )
    )
)
if not defined PY set PY=python

%PY% --version >nul 2>&1
if %errorlevel% neq 0 (
    echo    [!] 未找到 Python, 正在下载...
    powershell -Command "Invoke-WebRequest -Uri 'https://www.python.org/ftp/python/3.11.9/python-3.11.9-amd64.exe' -OutFile 'C:\overlay\python_installer.exe'" 2>nul
    if exist C:\overlay\python_installer.exe (
        echo    [+] 已下载, 正在静默安装...
        start /wait C:\overlay\python_installer.exe /quiet InstallAllUsers=1 PrependPath=1 Include_test=0
        del /f C:\overlay\python_installer.exe
        set PY=C:\Python311\python.exe
    ) else (
        echo    [x] 自动下载失败!
        echo    请手动安装 Python 3.8+: https://www.python.org/downloads/
        echo    安装后重新运行此脚本。
        pause
        exit /b 1
    )
) else (
    for /f "tokens=2" %%v in ('%PY% --version 2^>^&1') do echo    [OK] Python %%v
)

:: ── Step 2: 创建目录和解压服务文件 ──
echo.
echo [2/5] 部署应用文件...
if not exist C:\overlay mkdir C:\overlay

:: 使用 PowerShell 解码 Base64 内嵌的服务端代码
powershell -NoProfile -Command ^
"$b64 = Get-Content 'C:\overlay\server_v3.b64' -Raw; ^
 $bytes = [System.Convert]::FromBase64String($b64.Trim()); ^
 [System.IO.File]::WriteAllBytes('C:\overlay\server_v3.py', $bytes); ^
 Write-Host '   [OK] server_v3.py 已写入 ('$($bytes.Length) bytes)'"

if not exist C:\overlay\server_v3.py (
    echo    [x] 文件解压失败! 请确保 deploy.bat 和 server_v3.b64 在同一目录
    pause
    exit /b 1
)

:: ── Step 3: 配置防火墙 ──
echo.
echo [3/5] 配置防火墙规则...
netsh advfirewall firewall delete rule name="ImGuiOverlay API" >nul 2>&1
netsh advfirewall firewall add rule name="ImGuiOverlay API" dir=in action=allow protocol=TCP localport=8080 enable=yes >nul 2>&1
netsh advfirewall firewall delete rule name="ImGuiOverlay HTTP" >nul 2>&1
netsh advfirewall firewall add rule name="ImGuiOverlay HTTP" dir=in action=allow protocol=TCP localport=80 enable=yes >nul 2>&1
netsh advfirewall firewall delete rule name="ImGuiOverlay HTTPS" >nul 2>&1
netsh advfirewall firewall add rule name="ImGuiOverlay HTTPS" dir=in action=allow protocol=TCP localport=443 enable=yes >nul 2>&1
echo    [OK] 防火墙已配置 (8080/80/443)

:: ── Step 4: 启动服务 ──
echo.
echo [4/5] 启动卡密验证服务...
:: 先杀掉可能残留的旧进程
taskkill /F /IM python.exe /FI "WINDOWTITLE eq *server_v3*" >nul 2>&1
timeout /t 1 /nobreak >nul

:: 后台启动服务 (输出日志到文件)
start "" /B cmd /c "cd /d C:\overlay && %PY% server_v3.py --port 8080 --host 127.0.0.1 >> C:\overlay\server.log 2>&1"

:: 等待启动
timeout /t 4 /nobreak >nul

:: 检查是否在运行
curl -s -o nul -w "%%{http_code}" http://127.0.0.1:8080/api/status >nul 2>&1
if %errorlevel% equ 0 (
    echo    [OK] 服务已成功启动!
) else (
    echo    [!] 服务可能未正常启动, 请检查 C:\overlay\server.log
    type C:\overlay\server.log 2>nul | findstr /i "error trace fail"
)

:: ── Step 5: 创建开机自启 ──
echo.
echo [5/5] 配置开机自启...
:: 创建 VBS 脚本实现隐藏后台运行
echo Set WshShell = CreateObject("WScript.Shell") > C:\overlay\start_hidden.vbs
echo WshShell.Run "cmd /c cd /d C:\overlay && %PY% server_v3.py --port 8080 --host 127.0.0.1 >> C:\overlay\server.log 2>&1", 0, False >> C:\overlay\start_hidden.vbs

:: 添加到启动文件夹
copy /y C:\overlay\start_hidden.vbs "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\ImGuiOverlay.vbs" >nul 2>&1
echo    [OK] 开机自启已配置

:: ── 完成! ──
echo.
echo  ╔══════════════════════════════════════════════════╗
echo  ║              部署完成!                         ║
echo  ╠══════════════════════════════════════════════════╣
echo  ║                                              ║
echo  ║  管理后台: http://43.136.183.71:8080/admin     ║
echo  ║  登录页:   http://43.136.183.71:8080/login      ║
echo  ║  账号密码: admin / admin888                   ║
echo  ║                                              ║
echo  ║  ⚠ 登录后请立即修改管理员密码!                ║
echo  ║                                              ║
echo  ║  日志文件: C:\overlay\server.log               ║
echo  ║  服务停止: taskkill /F /IM python.exe          ║
echo  ║  服务重启: 双击本脚本即可                     ║
echo  ║                                              ║
echo  ╚══════════════════════════════════════════════════╝
echo.
echo  下一步 (可选):
echo    1. 在宝塔面板中配置 Nginx 反向代理 (端口80→8080)
echo       网站 ^> 添加站点 ^> 反向代理 ^> 目标URL: http://127.0.0.1:8080
echo    2. 配置 SSL 证书启用 HTTPS (宝塔面板 ^> SSL)
echo.

:: 自动打开浏览器
start http://43.136.183.71:8080/login

pause
