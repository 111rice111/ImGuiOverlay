# ============================================================
# ImGuiOverlay v3.0 — Windows 宝塔面板部署脚本
# ============================================================
# 适用: 腾讯云轻量应用服务器 (Windows Server + 宝塔面板)
# 用法:
#   1. 通过宝塔面板终端或远程桌面运行此脚本
#   2. 或在 PowerShell 中执行
#
# 服务器信息:
#   IP: 43.136.183.71
#   系统: Windows Server (宝塔面板已预装)
#   规格: 2核 2GB 40GB
# ============================================================

$ErrorActionPreference = "Stop"

function Write-Log($msg) {
    $ts = Get-Date -Format "HH:mm:ss"
    Write-Host "[$ts] [+] $msg" -ForegroundColor Green
}
function Write-Warn($msg) {
    $ts = Get-Date -Format "HH:mm:ss"
    Write-Host "[$ts] [!] $msg" -ForegroundColor Yellow
}

$APP_DIR = "C:\overlay"
$SERVICE_NAME = "OverlayServer"
$PYTHON_EXE = "python"
$PORT = 8080

Write-Host ""
Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "  ImGuiOverlay 卡密系统 v3.0 — Windows 部署" -ForegroundColor Cyan
Write-Host "  目标目录: $APP_DIR" -ForegroundColor Gray
Write-Host "  监听端口: $PORT" -ForegroundColor Gray
Write-Host "=============================================" -ForegroundColor Cyan
Write-Host ""

# ── Step 1: 检查 Python ──
Write-Log "Step 1/5: 检查环境..."
try {
    $pyVer = & python --version 2>&1
    Write-Log "Python: $pyVer"
} catch {
    Write-Warn "未检测到 Python, 请先安装 Python 3.8+ 并加入 PATH"
    exit 1
}

# ── Step 2: 创建目录并复制文件 ──
Write-Log "Step 2/5: 准备应用目录..."
if (-not (Test-Path $APP_DIR)) {
    New-Item -ItemType Directory -Path $APP_DIR -Force | Out-Null
    Write-Log "创建目录 $APP_DIR"
} else {
    Write-Log "目录已存在"
}

# 检查 server_v3.py 是否存在
if (-not (Test-Path "$APP_DIR\server_v3.py")) {
    # 尝试从当前目录复制
    if (Test-Path ".\server_v3.py") {
        Copy-Item ".\server_v3.py" "$APP_DIR\server_v3.py" -Force
        Copy-Item ".\deploy.ps1" "$APP_DIR\deploy.ps1" -Force -ErrorAction SilentlyContinue
        Write-Log "server_v3.py 已复制到目标目录"
    } else {
        Write-Warn "请先将 server_v3.py 上传到此目录或 $APP_DIR"
        Write-Warn "手动操作: 将 server_v3.py 放入 C:\overlay\ 目录后重新运行"
        exit 1
    }
}

# ── Step 3: 创建 Windows 服务注册脚本 (NSSM 或 WinSW) ──
Write-Log "Step 3/5: 创建启动脚本..."

# 方案 A: 使用 NSSM (推荐)
$nsmmCheck = Get-Command nssm -ErrorAction SilentlyContinue
if ($nsmmCheck) {
    Write-Log "检测到 NSSM, 注册服务..."
    nssm install $SERVICE_NAME $PYTHON_EXE "$APP_DIR\server_v3.py --port $PORT --host 127.0.0.1"
    nssm set $SERVICE_NAME AppDirectory $APP_DIR
    nsmm set $SERVICE_NAME DisplayName "ImGuiOverlay Card System v3.0"
    nsmm set $SERVICE_NAME Description "ImGuiOverlay 卡密验证服务端"
    nsmm set $SERVICE_NAME Start SERVICE_AUTO_START
    nssm start $SERVICE_NAME
    Write-Log "服务已通过 NSSM 注册并启动"
} else {
    # 方案 B: 使用 VBS 后台运行 (无需额外软件)
    Write-Warn "未安装 NSSM, 使用 VBS 脚本方式..."

    $vbsContent = @"
Set WshShell = CreateObject("WScript.Shell")
WshShell.Run "cmd /c cd /d $APP_DIR && python server_v3.py --port $PORT --host 127.0.0.1 >> $APP_DIR\server.log 2>&1", 0, False
"@
    Set-Content -Path "$APP_DIR\start_hidden.vbs" -Value $vbsContent -Encoding ASCII

    # 创建开机自启快捷方式
    $startupFolder = [Environment]::GetFolderPath("Startup")
    $ws = New-Object -ComObject WScript.Shell
    $sc = $ws.CreateShortcut("$startupFolder\$SERVICE_NAME.lnk")
    $sc.TargetPath = "wscript.exe"
    $sc.Arguments = "`"$APP_DIR\start_hidden.vbs`""
    $sc.Description = "ImGuiOverlay 卡密服务"
    $sc.Save()
    
    # 启动服务进程
    Start-Process -FilePath "wscript.exe" -ArgumentList "`"$APP_DIR\start_hidden.vbs`"" -WindowStyle Hidden
    Write-Log "VBS 启动脚本已创建, 服务正在后台运行"
    Write-Log "日志输出: $APP_DIR\server.log"
}

Start-Sleep -Seconds 2

# ── Step 4: 测试服务是否正常 ──
Write-Log "Step 4/5: 测试服务连接..."
try {
    $response = Invoke-RestMethod -Uri "http://127.0.0.1:$PORT/api/status" -TimeoutSec 5 -ErrorAction Stop
    if ($response.ok -eq $true) {
        Write-Log "服务正常运行! 版本: $($response.version)"
    } else {
        Write-Warn "服务响应异常"
    }
} catch {
    Write-Warn "无法连接到服务, 正在检查端口..."
    netstat -an | Select-String ":$PORT"
    Write-Warn "请查看上方输出排查问题"
}

# ── Step 5: 防火墙配置 ──
Write-Log "Step 5/5: 配置防火墙..."
# 添加防火墙规则 (仅允许外部访问 8080 端口)
try {
    $existingRule = Get-NetFirewallRule -DisplayName "ImGuiOverlay API" -ErrorAction SilentlyContinue
    if ($existingRule) {
        Remove-NetFirewallRule -DisplayName "ImGuiOverlay API" -ErrorAction SilentlyContinue
    }
    New-NetFirewallRule `
        -DisplayName "ImGuiOverlay API" `
        -Direction Inbound `
        -LocalPort $PORT `
        -Protocol TCP `
        -Action Allow `
        -Description "ImGuiOverlay 卡密验证服务" | Out-Null
    
    # 如果使用宝塔 Nginx 反向代理, 也放行 80 和 443
    New-NetFirewallRule `
        -DisplayName "ImGuiOverlay HTTP" `
        -Direction Inbound `
        -LocalPort 80 `
        -Protocol TCP `
        -Action Allow | Out-Null
        
    New-NetFirewallRule `
        -DisplayName "ImGuiOverlay HTTPS" `
        -Direction Inbound `
        -LocalPort 443 `
        -Protocol TCP `
        -Action Allow | Out-Null
        
    Write-Log "防火墙规则已添加 (8080/80/443)"
} catch {
    Write-Warn "防火墙配置失败 (可能需要管理员权限): $_"
}

# ── 完成! 输出访问信息 ──
Write-Host ""
Write-Host "=============================================" -ForegroundColor Green
Write-Host "  部署完成!" -ForegroundColor Green
Write-Host "=============================================" -ForegroundColor Green
Write-Host ""
Write-Host "  访问地址:" -ForegroundColor White
Write-Host "    管理后台: http://43.136.183.71:${PORT}/admin" -ForegroundColor Cyan
Write-Host "    登录页:   http://43.136.183.71:${PORT}/login" -ForegroundColor Cyan
Write-Host "    默认账号: admin / admin888" -ForegroundColor Yellow
Write-Host ""
Write-Host "  ⚠ 重要提醒:" -ForegroundColor Red
Write-Host "    1. 腾讯云安全组需放行 8080/80/443 端口!" -ForegroundColor Yellow
Write-Host "    2. 登录后立即修改管理员密码!" -ForegroundColor Yellow
Write-Host "    3. 如使用宝塔 Nginx, 需配置反向代理" -ForegroundColor Yellow
Write-Host ""
Write-Host "  服务管理:" -ForegroundColor White
if ($nsmmCheck) {
    Write-Host "    重启: nssm restart $SERVICE_NAME" -ForegroundColor Gray
    Write-Host "    停止: nssm stop $SERVICE_NAME" -ForegroundColor Gray
    Write-Host "    日志: type $APP_DIR\server.log" -ForegroundColor Gray
} else {
    Write-Host "    日志: type $APP_DIR\server.log" -ForegroundColor Gray
    Write-Host "    手动停止: taskkill /F /IM python.exe /FI `"WINDOWTITLE eq *server_v3*`"" -ForegroundColor Gray
}
Write-Host ""
