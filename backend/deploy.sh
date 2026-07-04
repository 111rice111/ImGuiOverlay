#!/bin/bash
# ============================================================
# ImGuiOverlay 卡密系统 v3.0 — 腾讯云部署脚本
# ============================================================
# 适用: 腾讯云轻量应用服务器 / CVM (Ubuntu 20.04+/Debian 11+)
# 用法: chmod +x deploy.sh && sudo bash deploy.sh
# ============================================================

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log() { echo -e "${GREEN}[+]${NC} $1"; }
warn() { echo -e "${YELLOW}[!]${NC} $1"; }
err() { echo -e "${RED}[x]${NC} $1"; exit 1; }

APP_NAME="ImGuiOverlay"
APP_USER="overlay"
APP_DIR="/opt/overlay"
SERVICE_NAME="overlay-server"
DOMAIN=""
USE_DOMAIN=false

# ── 解析参数 ──
while [[ $# -gt 0 ]]; do
    case $1 in
        --domain) DOMAIN="$2"; USE_DOMAIN=true; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        *) shift ;;
    esac
done

PORT=${PORT:-8080}

echo "============================================="
echo "  ${APP_NAME} — 腾讯云服务器一键部署"
echo "============================================="
if $USE_DOMAIN; then
    echo "  域名: ${DOMAIN}"
else
    echo "  模式: IP 直连 (建议使用 --domain 配置域名)"
fi
echo "  端口: ${PORT}"
echo "============================================="
echo ""

# ── 1. 系统基础 ──
log "Step 1/7: 更新系统并安装依赖..."
apt-get update -qq && apt-get upgrade -y -qq
apt-get install -y -qq python3 python3-pip nginx certbot python3-certbot-nginx ufw curl sqlite3
log "系统依赖安装完成"

# ── 2. 创建服务用户 ──
log "Step 2/7: 创建服务用户 ${APP_USER}..."
if ! id -u ${APP_USER} >/dev/null 2>&1; then
    useradd -r -s /bin/false -m ${APP_USER}
fi

# ── 3. 部署应用 ──
log "Step 3/7: 部署应用代码..."
mkdir -p ${APP_DIR}
cp server_v3.py ${APP_DIR}/
chown -R ${APP_USER}:${APP_USER} ${APP_DIR}
chmod +x ${APP_DIR}/server_v3.py
log "应用代码已部署到 ${APP_DIR}"

# ── 4. 创建 systemd 服务 ──
log "Step 4/7: 创建 systemd 服务..."
cat > /etc/systemd/system/${SERVICE_NAME}.service << SYSTEMD_EOF
[Unit]
Description=ImGuiOverlay Card Key System v3.0
After=network.target

[Service]
Type=simple
User=${APP_USER}
Group=${APP_USER}
WorkingDirectory=${APP_DIR}
ExecStart=/usr/bin/python3 ${APP_DIR}/server_v3.py --port ${PORT} --host 127.0.0.1
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=${SERVICE_NAME}

# 安全加固
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=${APP_DIR}
ReadOnlyPaths=/etc/ssl/certs

[Install]
WantedBy=multi-user.target
SYSTEMD_EOF

systemctl daemon-reload
systemctl enable ${SERVICE_NAME}
log "systemd 服务已创建"

# ── 5. 配置 Nginx ──
log "Step 5/7: 配置 Nginx 反向代理..."

if $USE_DOMAIN; then
    # 域名模式 — HTTP 初始配置 (稍后 certbot 会改写成 HTTPS)
    cat > /etc/nginx/sites-available/${APP_NAME} << NGINX_DOMAIN
# ImGuiOverlay v3.0 — Nginx 反向代理
# 域名: ${DOMAIN}

server {
    listen 80;
    server_name ${DOMAIN};

    # 日志
    access_log /var/log/nginx/overlay-access.log;
    error_log  /var/log/nginx/overlay-error.log;

    # 客户端请求大小限制
    client_max_body_size 10m;

    # 安全头
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;

    location / {
        proxy_pass http://127.0.0.1:${PORT};
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
        proxy_read_timeout 300s;
        proxy_connect_timeout 10s;
    }

    # API 路径 — 允许更大超时 (文件导出)
    location /api/ {
        proxy_pass http://127.0.0.1:${PORT};
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
        proxy_read_timeout 600s;
        proxy_connect_timeout 10s;
    }

    # 静态资源缓存
    location ~* \.(ico|css|js|gif|jpe?g|png|svg|woff2?)\$ {
        proxy_pass http://127.0.0.1:${PORT};
        expires 7d;
        add_header Cache-Control "public, immutable";
    }
}
NGINX_DOMAIN
else
    # IP 直连模式
    cat > /etc/nginx/sites-available/${APP_NAME} << NGINX_IP
# ImGuiOverlay v3.0 — 无域名模式 (IP 直连)

server {
    listen 80 default_server;
    server_name _;

    access_log /var/log/nginx/overlay-access.log;
    error_log  /var/log/nginx/overlay-error.log;
    client_max_body_size 10m;

    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;

    location / {
        proxy_pass http://127.0.0.1:${PORT};
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
        proxy_read_timeout 300s;
        proxy_connect_timeout 10s;
    }

    location /api/ {
        proxy_pass http://127.0.0.1:${PORT};
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
        proxy_read_timeout 600s;
        proxy_connect_timeout 10s;
    }
}
NGINX_IP
fi

# 启用站点
ln -sf /etc/nginx/sites-available/${APP_NAME} /etc/nginx/sites-enabled/${APP_NAME}
rm -f /etc/nginx/sites-enabled/default

nginx -t || err "Nginx 配置语法错误"
systemctl reload nginx
log "Nginx 配置完成"

# ── 6. SSL 证书 (域名模式) ──
if $USE_DOMAIN; then
    log "Step 6/7: 申请 SSL 证书 (Let's Encrypt)..."
    certbot --nginx -d ${DOMAIN} --non-interactive --agree-tos --email admin@${DOMAIN} --redirect 2>/dev/null || {
        warn "自动申请证书失败, 请手动运行: sudo certbot --nginx -d ${DOMAIN}"
        warn "你也可以在腾讯云控制台申请免费 SSL 证书 → 手动配置到 Nginx"
    }
    log "SSL 证书配置完成 (如果申请成功)"
else
    log "Step 6/7: 跳过 SSL (无域名模式)"
    warn "建议绑定域名并运行: sudo certbot --nginx 来启用 HTTPS"
    warn "腾讯云免费 SSL: https://console.cloud.tencent.com/ssl"
fi

# ── 7. 防火墙 ──
log "Step 7/7: 配置防火墙..."
ufw allow 22/tcp          # SSH
if $USE_DOMAIN; then
    ufw allow 80/tcp      # HTTP
    ufw allow 443/tcp     # HTTPS
else
    ufw allow 80/tcp      # HTTP
fi
ufw --force enable
log "防火墙已配置"

# ── 启动服务 ──
log "启动服务..."
systemctl start ${SERVICE_NAME}
sleep 2

if systemctl is-active --quiet ${SERVICE_NAME}; then
    log "服务启动成功!"
else
    warn "服务可能未正常启动, 请检查: journalctl -u ${SERVICE_NAME} -n 20"
fi

echo ""
echo "============================================="
echo -e "  ${GREEN}部署完成!${NC}"
echo "============================================="
if $USE_DOMAIN; then
    echo "  管理后台: https://${DOMAIN}/admin"
    echo "  登录页:   https://${DOMAIN}/login"
else
    echo "  管理后台: http://$(curl -s ifconfig.me 2>/dev/null || echo '服务器IP')/admin"
    echo "  登录页:   http://$(curl -s ifconfig.me 2>/dev/null || echo '服务器IP')/login"
fi
echo "  默认账号: admin / admin888"
echo ""
echo "  常用命令:"
echo "    systemctl status ${SERVICE_NAME}   # 服务状态"
echo "    systemctl restart ${SERVICE_NAME}  # 重启服务"
echo "    journalctl -u ${SERVICE_NAME} -f   # 查看日志"
echo "    systemctl reload nginx             # 重载 Nginx"
echo ""
echo "  ⚠ 首次登录后请立即修改密码!"
echo "============================================="
