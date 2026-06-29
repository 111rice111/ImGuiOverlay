/*
 * stealth.h — 进程隐身模块
 *
 * 功能: 进程名伪装 + OOM 保护 + 窗口名净化
 * 设计原则: 独立模块, 零副作用, 调用即忘
 *
 * 使用: 在 main() 中驱动初始化后调用 stealth_init()
 * 影响范围: 仅本进程的 /proc/self/comm、线程名、OOM分数、窗口标题
 * 安全: 不影响任何对游戏进程的扫描/读取逻辑
 */

#pragma once
#include <sys/prctl.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

// ── 伪装进程名为内核线程 ──
//   效果: ps/top/htop 中显示为 [kworker/u:0] 而非 overlay
//   限制: /proc/self/comm 最多 15 字符
inline void stealth_rename() {
    // 1. 改 /proc/self/comm (ps 显示的进程名)
    int fd = open("/proc/self/comm", O_WRONLY);
    if (fd > 0) {
        const char* fake = "kworker/u:0";
        write(fd, fake, strlen(fake));
        close(fd);
    }
    // 2. 改线程名 (ps -T / htop 线程视图)
    prctl(PR_SET_NAME, "kworker/u:0", 0, 0, 0);
}

// ── 设置 OOM 保护 ──
//   效果: Linux OOM Killer 永远不会杀本进程
//   oom_score_adj = -1000 表示"绝对不可杀"
inline void stealth_protect_oom() {
    int fd = open("/proc/self/oom_score_adj", O_WRONLY);
    if (fd > 0) {
        write(fd, "-1000", 5);
        close(fd);
    }
}

// ── 统一入口 ──
//   调用时机: 驱动初始化完成后, 游戏线程启动前
//   备注: 如果不存在 /proc/self/comm 或 OOM 文件, 静默跳过
inline void stealth_init() {
    stealth_rename();
    stealth_protect_oom();
}
