# ImGuiOverlay 卡密系统 v3.0 — 腾讯云部署完整指南

## ==================== 概述 ====================

本系统是 ImGuiOverlay 的卡密验证后端, 已升级到 v3.0, 支持:

- **多时长卡密**: 小时/天/周/月/年/永久, 共12种预设
- **公用卡**: 不绑定设备, 可多人共用, 支持并发数限制
- **公告推送**: 横幅/弹窗模式, 优先级排序, 生效时间段
- **强制更新**: 最低版本强制弹窗, 可选更新提示
- **Web 管理面板**: 生成/管理/统计/日志/公告/更新, 一站式管理

## ==================== 前置准备 ====================

你需要在腾讯云准备:
1. 一台服务器 (轻量应用服务器或 CVM, Ubuntu 20.04+ 推荐)
2. (可选) 一个域名, 用于 HTTPS 访问

## ==================== 快速部署 ====================

### 步骤 1: 上传文件到服务器

```bash
# 在你的 Windows 电脑上, 把文件用 SCP 上传
scp server_v3.py deploy.sh root@你的服务器IP:/root/
```

### 步骤 2: SSH 登录服务器

```bash
ssh root@你的服务器IP
```

### 步骤 3: 运行部署脚本

```bash
# 有域名
chmod +x /root/deploy.sh
bash /root/deploy.sh --domain your-domain.com

# 无域名 (IP 直连)
bash /root/deploy.sh
```

脚本会自动完成: 安装依赖 → 创建服务 → Nginx → SSL → 防火墙 → 启动服务

### 步骤 4: 访问管理后台

- 有域名: `https://your-domain.com/admin`
- 无域名: `http://服务器IP/admin`
- 默认账号: `admin` / `admin888`
- **⚠️ 登录后立即修改密码!**

## ==================== 手动部署 (不想用脚本) ====================

### 1. 安装依赖
```bash
apt-get update && apt-get install -y python3 python3-pip nginx
```

### 2. 启动服务
```bash
# 直接运行 (调试用)
cd /opt/overlay && python3 server_v3.py --port 8080

# 或创建 systemd 服务 (参考 deploy.sh 中的 service 配置)
```

### 3. 配置 Nginx 反向代理
参考 `nginx-ssl.conf` 模板

### 4. 配置 SSL (腾讯云免费证书)
1. 前往 https://console.cloud.tencent.com/ssl 申请免费 DV 证书
2. 下载 Nginx 格式证书
3. 上传到服务器 `/etc/nginx/ssl/`
4. 使用 `nginx-ssl.conf` 模板配置

## ==================== 客户端配置 ====================

部署完成后, 需要更新客户端的服务器地址:

### 方法 1: 配置文件 (推荐)
```
adb push overlay_config.txt /data/local/bin/overlay_config.txt
```

编辑 `overlay_config.txt`:
```
server_host=your-domain.com
server_port=8080
```

### 方法 2: 端口文件 (v2 兼容)
```
adb shell "echo '8080' > /data/local/bin/overlay_port.txt"
```

## ==================== 防火墙端口 ====================

| 端口 | 协议 | 用途 |
|------|------|------|
| 22   | TCP  | SSH 管理 |
| 80   | TCP  | HTTP (自动跳转 HTTPS) |
| 443  | TCP  | HTTPS (管理面板) |
| 8080 | TCP  | Python 服务 (仅本地, 不对外开放) |

腾讯云需要在**安全组**中额外放行这些端口!

## ==================== 常用运维命令 ====================

```bash
# 服务管理
systemctl status overlay-server       # 查看状态
systemctl restart overlay-server      # 重启服务
systemctl stop overlay-server         # 停止服务
journalctl -u overlay-server -f       # 实时日志
journalctl -u overlay-server -n 50    # 最近50条日志

# Nginx
nginx -t                              # 测试配置
systemctl reload nginx                # 重载配置
tail -f /var/log/nginx/overlay-*.log  # 查看 Nginx 日志

# 数据库
sqlite3 /opt/overlay/overlay_v3.db ".tables"    # 查看表
sqlite3 /opt/overlay/overlay_v3.db "SELECT COUNT(*) FROM cards"  # 卡密数量

# 备份数据库
cp /opt/overlay/overlay_v3.db /opt/overlay/backup_$(date +%Y%m%d).db
```

## ==================== 数据库路径 ====================

- 主数据库: `/opt/overlay/overlay_v3.db` (SQLite)
- 自动创建, 无需手动配置
- 建议每天备份

## ==================== 安全建议 ====================

1. ⚠️ **首次登录后立即修改管理员密码** (管理面板 → 系统设置)
2. 启用腾讯云服务器安全组, 只开放必要端口
3. 定期备份数据库
4. 不要将 `server_v3.py` 暴露在公网端口 (走 Nginx 反向代理)
5. 考虑启用腾讯云 WAF 防护
6. 管理员密码使用强密码 (12位以上, 含大小写+数字+符号)

## ==================== 升级说明 ====================

从 v2.x 升级到 v3.0:
- v3.0 使用独立的数据库 `overlay_v3.db`, 不会覆盖旧版
- 如需要迁移旧卡密, 可用 SQL 脚本手动导入
- 客户端 net_client.h 已更新为 v3.0 协议 (向下兼容)
