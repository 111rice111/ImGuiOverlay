#!/bin/bash
# ============================================================
# deploy.sh — ImGuiOverlay 手机推送脚本
#
# 用法:
#   ./deploy.sh                    → 推送当前版本（读取 version-latest.txt）
#   ./deploy.sh v2.45-stable        → 推送指定版本
#   ./deploy.sh -d                  → 仅推送开发版 overlay
#   ./deploy.sh -u                  → 仅推送用户版 overlay_user
#
# ============================================================
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
ARCHIVE_DIR="${PROJECT_DIR}/archive/历史版本二进制"
VERSION_FILE="${PROJECT_DIR}/version-latest.txt"
LOG_DIR="${PROJECT_DIR}/archive/更新日志归档"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

ok()   { echo -e "[${GREEN}  OK  ${NC}] $1"; }
fail() { echo -e "[${RED} FAIL ${NC}] $1"; exit 1; }
info() { echo -e "[${CYAN} INFO ${NC}] $1"; }

# ========== 解析参数 ==========
PUSH_DEV=true
PUSH_USER=true
VERSION=""

for arg in "$@"; do
    case "$arg" in
        -d) PUSH_USER=false ;;
        -u) PUSH_DEV=false ;;
        *)  VERSION="$arg" ;;
    esac
done

# 自动读取当前版本
if [ -z "$VERSION" ]; then
    VERSION=$(head -1 "$VERSION_FILE" 2>/dev/null | tr -d '\r\n ')
    if [ -z "$VERSION" ]; then
        fail "无法读取版本号，请手动指定: ./deploy.sh v2.45-stable"
    fi
fi

# 提取版本代码（如 v2.45-stable → 245）
VER_CODE=$(echo "$VERSION" | grep -oP '\d+' | head -1)
# 提取日期用于查找更新日志
TODAY=$(date '+%Y-%m-%d')

info "版本: $VERSION (代码: $VER_CODE)"

# ========== 连接检查 ==========
if ! command -v adb &>/dev/null; then
    fail "adb 未安装"
fi

DEVICES=$(adb devices 2>/dev/null | grep -v "List" | grep -v "^$" | wc -l)
if [ "$DEVICES" -eq 0 ]; then
    fail "没有检测到已连接的设备"
fi
ok "设备已连接"

# ========== 推送开发版 ==========
if $PUSH_DEV; then
    DEV_BIN="${ARCHIVE_DIR}/overlay-${VERSION}"
    if [ ! -f "$DEV_BIN" ]; then
        fail "找不到开发版: $DEV_BIN"
    fi
    info "推送开发版 → /data/local/bin/overlay"
    adb push "$DEV_BIN" /sdcard/overlay_dev_tmp || fail "推送失败"
    adb shell "su -c 'cp /sdcard/overlay_dev_tmp /data/local/bin/overlay && chmod 777 /data/local/bin/overlay && rm /sdcard/overlay_dev_tmp'" || fail "部署失败"
    ok "开发版已就绪"
fi

# ========== 推送用户版 ==========
if $PUSH_USER; then
    USER_BIN="${ARCHIVE_DIR}/overlay-${VERSION}-user"
    if [ ! -f "$USER_BIN" ]; then
        fail "找不到用户版: $USER_BIN"
    fi

    info "推送用户版 → /data/local/bin/overlay_user"
    adb push "$USER_BIN" /sdcard/overlay_user_tmp || fail "推送失败"
    adb shell "su -c 'cp /sdcard/overlay_user_tmp /data/local/bin/overlay_user && chmod 777 /data/local/bin/overlay_user && rm /sdcard/overlay_user_tmp'" || fail "部署失败"
    ok "用户版已就绪"

    # 版本备份
    info "推送版本备份 → /data/local/bin/overlay_user_v${VER_CODE}"
    adb push "$USER_BIN" /sdcard/overlay_user_v${VER_CODE} || fail "推送失败"
    adb shell "su -c 'cp /sdcard/overlay_user_v${VER_CODE} /data/local/bin/overlay_user_v${VER_CODE} && chmod 777 /data/local/bin/overlay_user_v${VER_CODE} && rm /sdcard/overlay_user_v${VER_CODE}'" || fail "部署失败"
    ok "版本备份已就绪"
fi

# ========== 推送版本信息 ==========
info "推送版本信息"
adb push "$VERSION_FILE" /sdcard/version-latest.txt || fail "推送失败"
adb shell "su -c 'cp /sdcard/version-latest.txt /data/local/bin/version-latest.txt && chmod 666 /data/local/bin/version-latest.txt && rm /sdcard/version-latest.txt'" || fail "部署失败"
ok "版本信息已就绪"

# ========== 推送更新日志 ==========
CHANGELOG=$(find "$LOG_DIR" -name "更新日志_${VERSION%%-stable}*_${TODAY}.txt" 2>/dev/null | head -1)
if [ -z "$CHANGELOG" ]; then
    # 尝试查找最近日期的
    CHANGELOG=$(find "$LOG_DIR" -name "更新日志_${VERSION%%-stable}*" 2>/dev/null | sort -r | head -1)
fi
if [ -n "$CHANGELOG" ]; then
    info "推送更新日志 → changelog_v${VER_CODE}.txt"
    adb push "$CHANGELOG" /sdcard/changelog_v${VER_CODE}.txt || warn "推送失败"
    adb shell "su -c 'cp /sdcard/changelog_v${VER_CODE}.txt /data/local/bin/changelog_v${VER_CODE}.txt && chmod 666 /data/local/bin/changelog_v${VER_CODE}.txt && rm /sdcard/changelog_v${VER_CODE}.txt'" 2>/dev/null || true
    ok "更新日志已就绪"
else
    info "(未找到更新日志文件，跳过)"
fi

# ========== 验证 ==========
echo ""
info "=== 验证 ==="
adb shell "su -c 'ls -la /data/local/bin/overlay /data/local/bin/overlay_user /data/local/bin/overlay_user_v${VER_CODE} /data/local/bin/version-latest.txt /data/local/bin/changelog_v${VER_CODE}.txt'" 2>/dev/null

echo ""
echo -e "${GREEN}✓ 部署完成${NC}"
