# ============================================================
# ImGuiOverlay v3.0 — 快速部署 (Windows 宝塔面板)
# 你的服务器: 43.136.183.71 (成都)
# 系统: Windows Server (宝塔面板)
# 规格: 2核 2GB 40GB / 3Mbps带宽
#
# 最快部署: 只需3步!
# ============================================================

## 第一步: 上传文件到服务器

### 方法 A: 宝塔面板文件管理 (推荐)
1. 浏览器打开 `http://43.136.183.71:8888`
2. 登录宝塔面板
3. 左侧 → **文件** → 进入 `C:\` 盘
4. 新建文件夹 `overlay` (即 `C:\overlay\`)
5. 上传这2个文件到 `C:\overlay\`:
   - ✅ server_v3.py
   - ✅ deploy.ps1

### 方法 B: 远程桌面 + SCP
```bash
# 在你电脑的 PowerShell 中执行:
scp E:\ImGuiOverlay-User\backend\server_v3.py Administrator@43.136.183.71:C:\overlay\
scp E:\ImGuiOverlay-User\backend\deploy.ps1 Administrator@43.136.183.71:C:\overlay\
```

---

## 第二步: 运行部署脚本

### 方法 A: 远程桌面 PowerShell (推荐)
1. 远程桌面连接 `43.136.183.71` (管理员账号密码在腾讯云控制台重置)
2. 打开 PowerShell (**管理员**身份)
3. 执行:
```powershell
cd C:\overlay
.\deploy.ps1
```

### 方法 B: 宝塔终端
宝塔面板左侧 → **终端** → 输入:
```powershell
cd C:\overlay && powershell -ExecutionPolicy Bypass -File deploy.ps1
```

脚本会自动完成:
✅ 创建应用目录
✅ 注册/启动服务
✅ 配置防火墙
✅ 测试连接
✅ 输出访问地址

---

## 第三步: 配置宝塔 Nginx 反向代理 (可选但推荐)

这样可以用 80 端口访问, 不需要带端口号:

1. 宝塔面板 → **网站** → **添加站点**
   - 域名: `43.136.183.71` (或你的域名)
   - PHP版本: 选 **纯静态**
   
2. 点击站点 → **设置** → **反向代理** → **添加**
   - 目标URL: `http://127.0.0.1:8080`

3. 完成! 现在可以通过 `http://43.136.183.71/admin` 访问

---

## 访问信息

| 项目 | 地址 |
|------|------|
| 直接访问 | http://43.136.183.71:8080/admin |
| 通过Nginx | http://43.136.183.71/admin |
| 账号 | admin |
| 密码 | admin888 |
| **⚠️ 登录后立即改密码!!** | |

---

## 腾讯云安全组配置 (必须!)

腾讯云默认只开放少量端口, 你需要手动添加:

1. 登录 [腾讯云控制台](https://console.cloud.tencent.com/cvm/lighthouse/instance/detail?lhins-jrfj6w3q&tab=firewall)
2. 点击 **防火墙** 标签
3. 添加规则:

| 协议 | 端口 | 来源 | 说明 |
|------|------|------|------|
| TCP | 8080 | 0.0.0.0/0 | API服务 (直连) |
| TCP | 80 | 0.0.0.0/0 | HTTP (通过Nginx) |
| TCP | 443 | 0.0.0.0/0 | HTTPS (SSL) |

---

## 常见问题

### Q: 宝塔面板打不开?
A: 安全组需要放行 8888 端口 (宝塔默认端口)

### Q: Python 没装怎么办?
A: 宝塔面板 → 软件商店 → Python → 安装, 或者从 python.org 下载安装并勾选 "Add to PATH"

### Q: 服务启动失败?
A: 查看 `C:\overlay\server.log` 日志, 常见原因: 端口占用、Python版本太低(需3.7+)

### Q: 怎么更新代码?
A: 上传新 server_v3.py 到 C:\overlay\, 然后 PowerShell 执行 `nssm restart OverlayServer` 或重启服务器
