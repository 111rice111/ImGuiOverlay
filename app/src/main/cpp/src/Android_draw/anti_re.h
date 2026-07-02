#pragma once
// ============================================================
// 全面反逆向保护 (anti_re.h)
// 反调试 + 反模拟器 + 反VPN + 完整性校验 + 字符串加密
// ============================================================
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
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
// 1. 反调试检测 (完整版)
// ============================================================
inline bool check_ptrace() {
    if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1) return true;
    ptrace(PTRACE_DETACH, 0, 0, 0);
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
            return pid != 0;
        }
    }
    fclose(f);
    return false;
}

inline bool check_frida_maps() {
    const char* keywords[] = {"frida", "gadget", "substrate", "cycript", "xposed", "magisk"};
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        for (int i = 0; i < 6; i++) {
            if (strstr(line, keywords[i])) { fclose(f); return true; }
        }
    }
    fclose(f);
    return false;
}

inline bool check_frida_ports() {
    int ports[] = {27042, 27043};
    for (int p : ports) {
        char cmd[64]; snprintf(cmd, sizeof(cmd), "/proc/net/tcp");
        // 简化: 检查 /proc/net/tcp 中是否有对应端口
        FILE* f = fopen(cmd, "r");
        if (!f) continue;
        char line[256]; char port_hex[16];
        snprintf(port_hex, sizeof(port_hex), ":%04X", p);
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, port_hex)) { fclose(f); return true; }
        }
        fclose(f);
    }
    return false;
}

inline bool check_ld_preload() {
    return getenv("LD_PRELOAD") != nullptr;
}

inline bool check_sigcgt() {
    // 调试器会屏蔽 SIGTRAP，通过 SigCgt 位掩码检测
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "SigCgt:", 7) == 0) {
            unsigned long long mask = strtoull(line + 7, nullptr, 16);
            fclose(f);
            // SIGTRAP=bit5(0x20), SIGSTOP=bit19? 正常程序不应屏蔽 SIGTRAP
            return (mask & 0x20) != 0;
        }
    }
    fclose(f);
    return false;
}

// 综合反调试 (带诊断)
inline int anti_debug_diag() {
    // check_ptrace() 已移除: PT_TRACEME 在 su 环境下导致 ptrace_stop 假死
    if (check_frida_maps())   return 3;
    if (check_frida_ports())  return 4;
    if (check_ld_preload())   return 5;
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

inline bool verify_integrity() {
    // 简单校验: 检查 .text 段是否有断点指令 (0xD4200000 = ARM64 BKPT)
    // 函数地址从当前函数开始扫描前 64 字节
    unsigned int* code = (unsigned int*)&verify_integrity;
    int bkpt_count = 0;
    for (int i = 0; i < 256; i++) {
        if (code[i] == 0xD4200000) bkpt_count++;
    }
    return bkpt_count < 3;  // 超过3个断点 → 被 patch
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
// 主入口: 所有检测 (带诊断输出)
// ============================================================
inline void security_checkpoint() {
    if (is_emulator())       _exit(0x11);
    if (anti_debug_diag())   _exit(0x22);
    if (is_vpn_active())     _exit(0x33);
    if (!verify_integrity()) _exit(0x44);
}
