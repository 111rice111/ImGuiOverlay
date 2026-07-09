/*
 * anti_debug.h — 基础反调试/防破解
 *
 * 检测项: ptrace 附加, /proc/self/status TracerPid, Frida/xposed 痕迹
 * 触发后延迟退出，避免立即崩被定位
 */

#pragma once
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/ptrace.h>
#include <thread>
#include <chrono>

// 检查是否被 ptrace 附加
inline bool is_ptraced() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return true; // 打不开也当有问题
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            int pid = atoi(line + 10);
            fclose(f);
            return pid != 0;
        }
    }
    fclose(f);
    return false;
}

// 检查常见调试/Hook 框架痕迹
inline bool check_hook_traces() {
    // Frida
    if (fopen("/data/local/tmp/frida-server", "r")) return true;
    if (fopen("/data/local/tmp/re.frida.server", "r")) return true;
    // Xposed
    if (fopen("/data/data/de.robv.android.xposed.installer", "r")) return true;
    // IDA android_server
    FILE* f = popen("ps | grep android_server 2>/dev/null", "r");
    if (f) {
        char buf[256] = {};
        fgets(buf, sizeof(buf), f);
        pclose(f);
        if (strstr(buf, "android_server")) return true;
    }
    return false;
}

// 自身完整性校验 (简单 CRC)
inline bool self_integrity_ok() {
    FILE* f = fopen("/proc/self/exe", "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 100000 || size > 5000000) { fclose(f); return false; }
    fclose(f);
    return true;
}

// 反调试启动检查 (在 main 循环前调用)
inline void anti_debug_init() {
    if (is_ptraced()) {
        printf("[Security] 检测到调试器，5秒后退出\n");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        exit(1);
    }
    if (check_hook_traces()) {
        printf("[Security] 检测到 Hook 框架痕迹\n");
    }
    if (!self_integrity_ok()) {
        printf("[Security] 自身完整性校验失败\n");
        std::this_thread::sleep_for(std::chrono::seconds(3));
        exit(1);
    }
}
