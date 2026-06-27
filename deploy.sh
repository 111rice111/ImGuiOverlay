#!/bin/bash
# ============================================================
# deploy.sh — ImGuiOverlay 一键部署系统
# 
# 用法:
#   ./deploy.sh              → 开发部署 (只推二进制)
#   ./deploy.sh full         → 完整部署 (二进制 + 数据文件)
#   ./deploy.sh production   → 生产部署 (二进制 + 数据 + 标签)
#   ./deploy.sh verify       → 验证部署状态
#
# ============================================================
set -euo pipefail

# ========== 配置 ==========
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY_NAME="overlay"
REMOTE_PATH="/data/local/tmp/${BINARY_NAME}"
DATA_DIR="/data/local/tmp/maps"
ASSETS_DIR="${PROJECT_DIR}/app/src/main/assets"
BUILD_DIR="${PROJECT_DIR}/app/build/intermediates/cxx/RelWithDebInfo"
LOG_FILE="${PROJECT_DIR}/deploy.log"
VERSION_FILE="${PROJECT_DIR}/version-latest.txt"
CHANGELOG_FILE="${PROJECT_DIR}/CHANGELOG.txt"

# 颜色
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; NC='\033[0m'

# 路径修正 (Git Bash /e/... → E:/...)
fix_path() {
    local p="$1"
    # Git Bash /e/... → E:/... for adb
    if [[ "$p" =~ ^/([a-zA-Z])/ ]]; then
        p="${BASH_REMATCH[1]}:/${p:3}"
    fi
    echo "$p"
}

MODE="${1:-dev}"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

log()  { echo -e "[${CYAN}${TIMESTAMP}${NC}] $1" | tee -a "$LOG_FILE"; }
ok()   { echo -e "[${GREEN}  OK  ${NC}] $1" | tee -a "$LOG_FILE"; }
warn() { echo -e "[${YELLOW} WARN ${NC}] $1" | tee -a "$LOG_FILE"; }
fail() { echo -e "[${RED} FAIL ${NC}] $1" | tee -a "$LOG_FILE"; exit 1; }

# ========== 阶段 1: 连接验证 ==========
verify_connection() {
    log "===== 阶段 1/5: 设备连接检查 ====="
    
    # ADB 是否可用
    if ! command -v adb &>/dev/null; then
        fail "adb 未安装或不在 PATH 中"
    fi
    ok "adb 已找到: $(which adb)"
    
    # 设备是否连接
    local devices=$(adb devices 2>/dev/null | grep -v "List" | grep -v "^$" | wc -l)
    if [ "$devices" -eq 0 ]; then
        fail "没有检测到已连接的设备"
    fi
    
    local device_info=$(adb devices -l 2>/dev/null | grep -v "List" | head -1)
    ok "设备已连接: $device_info"
    
    # Root 权限检查
    if ! adb shell "id -u 2>/dev/null | grep -q '0'"; then
        warn "设备未 root — /data/local/tmp 写入可能失败"
    else
        ok "设备已 root"
    fi
    
    # 网络状态
    local device_ip=$(adb shell "getprop ro.build.version.release 2>/dev/null" | tr -d '\r')
    ok "Android 版本: $device_ip"
}

# ========== 阶段 2: 环境准备 ==========
prepare_environment() {
    log "===== 阶段 2/5: 环境准备 ====="
    
    # 确保远程目录存在
    adb shell "mkdir -p ${DATA_DIR} ${DATA_DIR}/calib_backup 2>/dev/null" || warn "无法创建远程目录"
    ok "远程目录已就绪: ${DATA_DIR}"
    
    # 检查磁盘空间
    local free_space=$(adb shell "df -k /data/local/tmp 2>/dev/null | tail -1 | awk '{print \$4}'")
    if [ -n "$free_space" ] && [ "$free_space" -lt 10240 ]; then
        warn "磁盘空间不足: ${free_space}KB (建议 > 10MB)"
    else
        ok "磁盘空间: ${free_space}KB"
    fi
    
    # 备份当前远程版本
    if adb shell "[ -f ${REMOTE_PATH} ]" 2>/dev/null; then
        local old_md5=$(adb shell "md5sum ${REMOTE_PATH}" 2>/dev/null | awk '{print $1}')
        log "备份远程版本 (MD5: $old_md5)"
        adb shell "cp ${REMOTE_PATH} ${REMOTE_PATH}.bak 2>/dev/null" || true
    fi
}

# ========== 阶段 3: 编译构建 ==========
build_project() {
    log "===== 阶段 3/5: 编译构建 ====="
    
    cd "$PROJECT_DIR"
    
    # 选择构建类型
    local build_cmd="./gradlew assembleRelease"
    if [ "${1:-}" = "clean" ]; then
        build_cmd="./gradlew clean assembleRelease"
        log "执行完整清理编译..."
    else
        log "执行增量编译..."
    fi
    
    # 执行构建
    if $build_cmd 2>&1 | tee -a "$LOG_FILE" | tail -3; then
        ok "构建成功"
    else
        fail "构建失败，查看日志: $LOG_FILE"
    fi
    
    # 查找生成的二进制文件
    local binary=$(find "$BUILD_DIR" -name "overlay" -type f 2>/dev/null | head -1)
    if [ -z "$binary" ]; then
        # 尝试备用路径
        binary=$(find "$(fix_path "${PROJECT_DIR}/app/build")" -name "overlay" -type f 2>/dev/null | head -1)
    fi
    if [ -z "$binary" ]; then
        fail "找不到编译产物 'overlay'"
    fi
    
    ok "二进制文件: $binary"
    local local_md5=$(md5sum "$binary" | awk '{print $1}')
    log "本地 MD5: $local_md5"
    echo "$local_md5" > "${PROJECT_DIR}/.last_deploy_md5"
    
    # 检查文件大小
    local filesize=$(stat -c%s "$binary" 2>/dev/null || stat -f%z "$binary" 2>/dev/null)
    ok "文件大小: ${filesize} bytes"
}

# ========== 阶段 4: 部署执行 ==========
deploy_binary() {
    log "===== 阶段 4/5: 部署执行 ====="
    
    local binary="$1"
    
    # 推送到手机
    log "推送二进制到 ${REMOTE_PATH}..."
    if adb push "$(fix_path "$binary")" "${REMOTE_PATH}" 2>/dev/null; then
        ok "推送成功"
    else
        # 恢复备份
        adb shell "[ -f ${REMOTE_PATH}.bak ] && cp ${REMOTE_PATH}.bak ${REMOTE_PATH}" 2>/dev/null || true
        fail "推送失败"
    fi
    
    # 设置权限
    log "设置权限 777..."
    adb shell "chmod 777 ${REMOTE_PATH}" || fail "权限设置失败"
    ok "权限已设置"
    
    # 验证远程 MD5
    local remote_md5=$(adb shell "md5sum ${REMOTE_PATH}" 2>/dev/null | awk '{print $1}')
    local local_md5=$(cat "${PROJECT_DIR}/.last_deploy_md5")
    if [ "$remote_md5" = "$local_md5" ]; then
        ok "MD5 校验一致: $remote_md5"
    else
        fail "MD5 不匹配! 本地=$local_md5 远程=$remote_md5"
    fi
}

deploy_data() {
    log "部署数据文件..."
    
    local pushed=0
    
    # map_config.json
    if [ -f "${ASSETS_DIR}/map_config_new.json" ]; then
        adb push "$(fix_path "${ASSETS_DIR}/map_config_new.json")" "${DATA_DIR}/map_config.json" 2>/dev/null && ((pushed++))
    fi
    
    # musicbox_stools.json
    if [ -f "${ASSETS_DIR}/musicbox_stools.json" ]; then
        adb push "$(fix_path "${ASSETS_DIR}/musicbox_stools.json")" "${DATA_DIR}/musicbox_stools.json" 2>/dev/null && ((pushed++))
    fi
    
    ok "推送了 ${pushed} 个数据文件"
}
deploy_changelog() {
    # 更新版本号
    local version="${1:-unknown}"
    echo "v${version} - ${TIMESTAMP}" > "$VERSION_FILE"
    adb push "$VERSION_FILE" "${DATA_DIR}/version-latest.txt" 2>/dev/null || true
    
    echo "[${TIMESTAMP}] v${version} deployed" >> "$CHANGELOG_FILE"
    adb push "$CHANGELOG_FILE" "${DATA_DIR}/CHANGELOG.txt" 2>/dev/null || true
}

# ========== 阶段 5: 部署后验证 ==========
verify_deployment() {
    log "===== 阶段 5/5: 部署后验证 ====="
    
    local all_ok=true
    
    # 检查二进制存在且有执行权限
    if adb shell "[ -x ${REMOTE_PATH} ]" 2>/dev/null; then
        ok "二进制存在且可执行"
    else
        warn "二进制权限异常"
        all_ok=false
    fi
    
    # 检查数据文件
    for file in "map_config.json" "musicbox_stools.json"; do
        if adb shell "[ -f ${DATA_DIR}/${file} ]" 2>/dev/null; then
            ok "${file} 存在"
        else
            warn "${file} 缺失"
            all_ok=false
        fi
    done
    
    # 获取日志路径
    ok "部署日志: $LOG_FILE"
    
    if $all_ok; then
        ok "✓ 部署验证通过"
    else
        warn "⚠ 部署验证有警告，但二进制已就绪"
    fi
}

# ========== 主流程 ==========
main() {
    echo ""
    echo -e "${BLUE}╔══════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║  ImGuiOverlay 自动化部署系统             ║${NC}"
    echo -e "${BLUE}║  模式: ${YELLOW}${MODE}${BLUE}                          ║${NC}"
    echo -e "${BLUE}╚══════════════════════════════════════════╝${NC}"
    echo ""
    
    # 初始化日志
    echo "=== 部署开始: ${TIMESTAMP} 模式=${MODE} ===" > "$LOG_FILE"
    
    case "$MODE" in
        verify)
            verify_deployment
            exit 0
            ;;
    esac
    
    # 流水线执行
    verify_connection
    prepare_environment
    build_project
    local bin=$(find "$BUILD_DIR" -name "overlay" -type f 2>/dev/null | head -1)
    deploy_binary "$bin"
    
    if [ "$MODE" = "full" ] || [ "$MODE" = "production" ]; then
        deploy_data
    fi
    
    deploy_changelog "2.13-dev"
    verify_deployment
    
    # Git 提交 (仅 production)
    if [ "$MODE" = "production" ]; then
        log "Git 提交..."
        cd "$PROJECT_DIR"
        git add -A
        git commit -m "deploy: production ${TIMESTAMP}" 2>/dev/null || true
        git push origin main 2>/dev/null || warn "Git push 失败 (网络可能不通)"
    fi
    
    echo ""
    echo -e "${GREEN}╔══════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║  ✓ 部署完成                              ║${NC}"
    echo -e "${GREEN}║  重新启动 overlay 即可生效               ║${NC}"
    echo -e "${GREEN}╚══════════════════════════════════════════╝${NC}"
}

main "$@"
