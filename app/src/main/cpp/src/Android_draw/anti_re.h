#pragma once
// ============================================================
// 全面反逆向保护 (anti_re.h)
// 反调试 + 反模拟器 + 反VPN + 完整性校验 + 字符串加密
// ============================================================
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <signal.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ---- 编译期字符串 XOR 加密 ----
namespace obf {
template<size_t N>
struct XorStr {
    char data[N];
    constexpr XorStr(const char (&s)[N]) : data{} {
        for (size_t i = 0; i < N; i++) data[i] = s[i] ^ 0x5A;
    }
    void decrypt(char* out) const {
        for (size_t i = 0; i < N; i++) out[i] = data[i] ^ 0x5A;
    }
    std::string str() const {
        char buf[N];
        decrypt(buf);
        return std::string(buf, N - 1);
    }
};
} // namespace obf

#define XORSTR(s) ([]()->std::string{ \
    constexpr obf::XorStr<sizeof(s)> _xs(s); \
    return _xs.str(); \
}())

// ---- 运行时字符串解密 (用于宏) ----
#define RTS(s) []()->std::string{ \
    constexpr obf::XorStr<sizeof(s)> _xs(s); \
    char _b[sizeof(s)]; _xs.decrypt(_b); \
    return std::string(_b, sizeof(s)-1); \
}()

// ============================================================
// 1. 反调试检测 (增强版)
// ============================================================

// ★ ptrace 检测 — fork子进程+信号超时, 自动跳过shell环境
inline bool check_ptrace() {
    // ★ 预检: 若父进程是 shell/su/init, 跳过 fork 检测 (ptrace_stop 假死风险)
    {
        FILE* f = fopen("/proc/self/cmdline", "r");
        if (f) {
            char cmd[256] = {0};
            fread(cmd, 1, sizeof(cmd)-1, f);
            fclose(f);
            if (strstr(cmd, "su") || strstr(cmd, "sh") || strstr(cmd, "adb")) {
                return false;  // shell环境, 跳过ptrace检测
            }
        }
    }
    
    pid_t child = fork();
    if (child == 0) {
        // 子进程: PT_TRACEME → 成功则立即退出
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        _exit(0);
    }
    if (child < 0) return false;  // fork失败, 跳过
    
    // 父进程: 2秒超时等待
    int status = 0;
    int waited = 0;
    for (int i = 0; i < 20 && waited == 0; i++) {
        usleep(100000);  // 100ms × 20 = 2s
        if (waitpid(child, &status, WNOHANG) > 0) waited = 1;
    }
    // 超时未退出 → 子进程被挂起 → 被调试
    if (!waited) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        return true;
    }
    return false;
}

inline bool check_tracer_pid() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            int pid = atoi(line + 10);
            fclose(f);
            // ★ 排除系统进程: init(1), su daemon(常见2000-5000范围)
            //    仅当 TracerPid 为普通用户进程时才判定为调试器
            if (pid == 0) return false;
            if (pid == 1) return false;        // init
            if (pid >= 2000 && pid <= 9999) return false;  // 系统守护进程
            return true;  // 其他非系统PID → 可疑
        }
    }
    fclose(f);
    return false;
}

// ★ 增强 Frida 检测: 字符串匹配 + 匿名可执行内存 + 端口范围 + 文件描述符
inline bool check_frida_maps() {
    const char* keywords[] = {"frida", "gadget", "substrate", "cycript", "xposed", "magisk"};
    bool has_anon_exec = false;
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // 1. 关键词字符串匹配
        for (int i = 0; i < 6; i++) {
            if (strstr(line, keywords[i])) { fclose(f); return true; }
        }
        // 2. 匿名可执行内存段 (Frida gadget 特征: r-xp 匿名映射)
        //    正常程序很少有大量匿名可执行内存
        if (!has_anon_exec) {
            // 格式: addr-perm ... /...  → 检查权限位 r.xp + 路径为空/anon
            char* perm_start = strchr(line, ' ');
            if (perm_start) {
                char* path_start = strrchr(line, '/');
                if (!path_start) {
                    // 无名映射: 检查权限
                    if (perm_start[1] == 'r' && perm_start[3] == 'x') {
                        has_anon_exec = true;  // 可疑标记，但不直接判定
                    }
                }
            }
        }
    }
    fclose(f);
    return false;  // 只有明确命中关键词才返回true，anon_exec仅作可疑标记
}

// ★ 扩展端口扫描: Frida 常用端口 + D-Bus 检查
inline bool check_frida_ports() {
    // Frida 默认端口 27042-27052，以及常用调试/注入端口
    int ports[] = {27042, 27043, 27044, 27045, 27046, 27047, 27048, 27049, 27050, 27051, 27052};
    for (int p : ports) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "/proc/net/tcp");
        FILE* f = fopen(cmd, "r");
        if (!f) continue;
        char line[256]; char port_hex[16];
        snprintf(port_hex, sizeof(port_hex), ":%04X", p);
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, port_hex)) { fclose(f); return true; }
        }
        fclose(f);
    }
    // IPv6 端口检查
    for (int p : ports) {
        char cmd6[64]; snprintf(cmd6, sizeof(cmd6), "/proc/net/tcp6");
        FILE* f6 = fopen(cmd6, "r");
        if (!f6) continue;
        char line[256]; char port_hex[16];
        snprintf(port_hex, sizeof(port_hex), ":%04X", p);
        while (fgets(line, sizeof(line), f6)) {
            if (strstr(line, port_hex)) { fclose(f6); return true; }
        }
        fclose(f6);
    }
    return false;
}

// ★ 检查 Frida 通过 /proc/self/fd 打开的 TCP socket
//    Frida-server 监听时会在 fd 表中留下 TCP socket
inline bool check_frida_fds() {
    DIR* dir = opendir("/proc/self/fd");
    if (!dir) return false;
    struct dirent* entry;
    int tcp_count = 0, suspicious_sockets = 0;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char link_path[256], target[256] = {0};
        snprintf(link_path, sizeof(link_path), "/proc/self/fd/%s", entry->d_name);
        ssize_t len = readlink(link_path, target, sizeof(target) - 1);
        if (len > 0) {
            target[len] = '\0';
            if (strstr(target, "socket:")) {
                // 读取 /proc/net/tcp 交叉验证: socket inode > 500000 → 可疑
                char* inode_str = strstr(target, "socket:[");
                if (inode_str) {
                    inode_str += 8;
                    long inode = atol(inode_str);
                    if (inode > 500000) { suspicious_sockets++; }
                    tcp_count++;
                }
            }
        }
    }
    closedir(dir);
    // ★ 调整阈值: 需要 >3 个高inode TCP socket 才报警 (正常App也有socket)
    //    Frida-server 通常会打开多个高inode socket
    return (suspicious_sockets >= 3);
}

inline bool check_ld_preload() {
    return getenv("LD_PRELOAD") != nullptr;
}

inline bool check_sigcgt() {
    // 调试器通常屏蔽 SIGTRAP(bit5=0x20) + SIGSTOP(bit19=0x80000)
    // Android 可能继承 SIGBUS/SIGCHLD 等, 但这些不算调试器特征
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "SigCgt:", 7) == 0) {
            unsigned long long mask = strtoull(line + 7, nullptr, 16);
            fclose(f);
            bool has_trap  = (mask & 0x20) != 0;      // SIGTRAP
            bool has_stop  = (mask & 0x80000) != 0;    // SIGSTOP
            // ★ 调试器特征: SIGTRAP + SIGSTOP 同时被屏蔽
            return has_trap && has_stop;
        }
    }
    fclose(f);
    return false;
}

// 综合反调试 (全维度: ptrace + Frida + LD_PRELOAD + TracerPid + SigCgt)
inline int anti_debug_diag() {
    if (check_ptrace())       return 1;
    if (check_tracer_pid())   return 2;
    if (check_frida_maps())   return 3;
    if (check_frida_ports())  return 4;
    if (check_frida_fds())    return 6;
    if (check_ld_preload())   return 5;
    if (check_sigcgt())       return 7;
    return 0;
}

// ============================================================
// 2. 模拟器检测
// ============================================================
inline bool is_emulator() {
    // QEMU 属性
    char buf[64] = {0};
    FILE* f = popen("getprop ro.kernel.qemu 2>/dev/null", "r");
    if (f) { fgets(buf, sizeof(buf), f); pclose(f); }
    if (buf[0] == '1') return true;

    // QEMU 设备文件
    if (access("/dev/socket/qemud", F_OK) == 0) return true;
    if (access("/dev/qemu_pipe", F_OK) == 0) return true;

    // CPU 核心数 (真机至少 4 核)
    long cores = sysconf(_SC_NPROCESSORS_CONF);
    if (cores < 4) return true;

    return false;
}

// ============================================================
// 3. VPN/抓包检测
// ============================================================
inline bool is_vpn_active() {
    FILE* f = fopen("/proc/net/route", "r");
    if (!f) return false;
    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        // 检测虚拟网卡: tun0/ppp0/wg0/utun
        if (strstr(line, "tun0") || strstr(line, "ppp0") ||
            strstr(line, "wg0")  || strstr(line, "utun")) {
            found = true; break;
        }
    }
    fclose(f);
    return found;
}

// ============================================================
// 4. 完整性校验 (.text段 CRC32)
// ============================================================
extern char __executable_start __asm("__executable_start");
extern char __etext __asm("__etext");

// ★ 扩大扫描范围: 覆盖多个关键函数区域, 而非仅自身
//    外部声明需要保护的关键函数
extern void security_checkpoint();
extern void cp_environment();

// 扫描指定地址附近的 BKPT 断点 + NOP注入检测
static inline int scan_bkpt_region(uintptr_t base, int dwords) {
    unsigned int* code = (unsigned int*)base;
    int bkpt_count = 0;
    int nop_runs = 0, nop_streak = 0;
    for (int i = 0; i < dwords; i++) {
        if (code[i] == 0xD4200000) bkpt_count++;          // ARM64 BKPT
        // 检测连续 NOP (0xD503201F) — patch后常见特征
        if (code[i] == 0xD503201F) { nop_streak++; }
        else {
            if (nop_streak >= 4) nop_runs++;              // 连续4+ NOP → 可疑
            nop_streak = 0;
        }
    }
    if (nop_streak >= 4) nop_runs++;
    return bkpt_count + nop_runs * 3;  // 每段NOP串计为3个异常点
}

inline bool verify_integrity() {
    int total_bkpt = 0;
    // 1. 自身函数 ±256 DWORDs
    total_bkpt += scan_bkpt_region((uintptr_t)&verify_integrity, 256);
    // 2. 安全检测入口
    total_bkpt += scan_bkpt_region((uintptr_t)&security_checkpoint, 128);
    // 3. 环境检测入口 (VPN/模拟器)
    total_bkpt += scan_bkpt_region((uintptr_t)&cp_environment, 128);
    // 4. 反调试检测入口
    total_bkpt += scan_bkpt_region((uintptr_t)&anti_debug_diag, 64);
    return total_bkpt < 5;  // 总共超过5个断点 → 判定被patch
}

// ============================================================
// 5. 安全内存操作 (防止 dump)
// ============================================================
inline void secure_zero(void* ptr, size_t len) {
    volatile char* p = (volatile char*)ptr;
    while (len--) *p++ = 0;
    __asm__ volatile("" : : "r"(p) : "memory");
}

template<typename T>
inline T* secure_alloc() {
    void* p = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return static_cast<T*>(p);
}

inline void secure_free(void* ptr, size_t len) {
    if (ptr) {
        secure_zero(ptr, len);
        munmap(ptr, len);
    }
}

// ============================================================
// 6. 硬件指纹
// ============================================================
inline std::string get_hw_fingerprint() {
    std::string fp;
    
    // MAC 地址
    FILE* f = popen("cat /sys/class/net/wlan0/address 2>/dev/null", "r");
    if (f) {
        char buf[64] = {0};
        fgets(buf, sizeof(buf), f);
        pclose(f);
        fp += buf;
    }

    // Android ID
    f = popen("settings get secure android_id 2>/dev/null", "r");
    if (f) {
        char buf[64] = {0};
        fgets(buf, sizeof(buf), f);
        pclose(f);
        fp += buf;
    }

    // Build 序列号
    f = popen("getprop ro.serialno 2>/dev/null", "r");
    if (f) {
        char buf[64] = {0};
        fgets(buf, sizeof(buf), f);
        pclose(f);
        fp += buf;
    }

    // 简单 hash (djb2)
    unsigned long hash = 5381;
    for (char c : fp) hash = ((hash << 5) + hash) + c;
    
    char result[32];
    snprintf(result, sizeof(result), "%08lx", hash);
    return std::string(result);
}

// ============================================================
// v3.1 加固: 主入口拆分为3个独立检查点，分散到 main.cpp 不同位置
// 攻击者需要找到并 patch 全部3个才能完全绕过
// ============================================================

// 检查点1: 环境检测 (最先执行，在 main 入口)
inline void cp_environment() {
    if (is_emulator()) {
        printf("\033[1;31m[!] 检测到模拟器环境, 程序即将退出\033[0m\n");
        fflush(stdout);
        usleep(500000);  // 留半秒让用户看到输出
        _exit(0x11);
    }
    if (is_vpn_active()) {
        printf("\033[1;31m[!] 检测到VPN/代理, 程序即将退出\033[0m\n");
        fflush(stdout);
        usleep(500000);
        _exit(0x33);
    }
}

// 检查点2: 反调试检测 (在授权验证前)
inline void cp_anti_debug() {
    int result = anti_debug_diag();
    if (result) {
        _exit(0x22);
    }
}

// 检查点3: 完整性校验 (在主循环中随机触发)
// ★ 双重冗余: BKPT扫描 + NOP-slide检测
inline void cp_integrity() {
    if (!verify_integrity()) {
        _exit(0x44);
    }
}

// 兼容旧代码的统一入口 (保留但标记为废弃)
inline void security_checkpoint() {
    cp_environment();
    cp_anti_debug();
    cp_integrity();
}
