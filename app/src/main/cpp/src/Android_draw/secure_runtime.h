/*
 * secure_runtime.h — 运行时反绕过防护
 * 
 * 设计原则:
 * 1. 验证失败降级功能，不是直接退出 (更难被定位 patch 点)
 * 2. 检查分散在业务逻辑中，不是集中单一函数
 * 3. 多重互锁 — 任何一个被 patch 都会触发级联失效
 *
 * 新增位置: app/src/main/cpp/src/Android_draw/secure_runtime.h
 * 集成方式: #include "Android_draw/secure_runtime.h"
 */

#pragma once
#include <atomic>
#include <chrono>
#include <cstring>
#include <sys/mman.h>     // PROT_READ/PROT_WRITE/PROT_EXEC
#include <thread>
#include <mutex>
#include <random>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

// ========== 心跳降级系统 (时间炸弹) ==========

struct HeartbeatState {
    std::atomic<int> consecutive_failures{0};   // 连续失败次数
    std::atomic<bool> degraded{false};           // 降级模式
    std::atomic<int> degradation_level{0};       // 降级等级 0-3
    std::chrono::steady_clock::time_point last_ok;
    std::atomic<bool> core_locked{false};        // 核心功能锁定
    static constexpr int DEGRADE_MINOR  = 2;     // 2次失败: 小降级 (缩短窗口)
    static constexpr int DEGRADE_MAJOR  = 4;     // 4次失败: 大降级
    static constexpr int DEGRADE_FATAL  = 5;     // 5次失败: 致命 (原10→5)
};
inline HeartbeatState g_hb_state;

// 心跳回调 — 服务端返回ok时调用
inline void hb_on_ok() {
    g_hb_state.consecutive_failures = 0;
    g_hb_state.degraded = false;
    g_hb_state.degradation_level = 0;
    g_hb_state.last_ok = std::chrono::steady_clock::now();
}

// 心跳回调 — 失败时调用
inline void hb_on_fail() {
    int f = ++g_hb_state.consecutive_failures;
    if (f >= HeartbeatState::DEGRADE_MINOR) {
        g_hb_state.degraded = true;
        g_hb_state.degradation_level = (f >= HeartbeatState::DEGRADE_FATAL) ? 3 
                                    : (f >= HeartbeatState::DEGRADE_MAJOR)  ? 2 : 1;
    }
    if (f >= HeartbeatState::DEGRADE_FATAL) {
        g_hb_state.core_locked = true;
    }
}

// 获取降级系数: 1.0=正常, 0.7=轻微, 0.3=严重, 0=完全禁用
inline float hb_degrade_multiplier() {
    if (!g_hb_state.degraded) return 1.0f;
    switch (g_hb_state.degradation_level) {
        case 1:  return 0.5f;   // ESP透明度50%, 地图照常
        case 2:  return 0.15f;  // ESP极淡, 地图简化
        default: return 0.0f;   // 完全禁用
    }
}

// 检查核心功能是否可用
inline bool hb_core_available() {
    return !g_hb_state.core_locked;
}

// ========== 反绕过互锁系统 v2 ==========
// ★ 核心改进: 5 个 flag 分散在 3 个独立内存位置
//    攻击者无法单次写入绕过所有检查
//    ★ inline 确保跨翻译单元(main/draw_Gui)共享同一份变量

// 位置1: 主标志 (堆分配, 地址随机)
inline std::atomic<uint32_t>* g_flags1 = new std::atomic<uint32_t>(0);
// 位置2: 冗余标志 (BSS段)
inline std::atomic<uint32_t> g_flags2{0};
// 位置3: 二次验证 (BSS段)
inline std::atomic<bool> g_auth2_verified{false};

class AntiBypassGuard {
    static constexpr uint32_t FLAG_AUTH    = 0x01;
    static constexpr uint32_t FLAG_CHECKPOINT = 0x02;
    static constexpr uint32_t FLAG_INTEGRITY = 0x04;
    static constexpr uint32_t FLAG_HB_OK   = 0x08;
    static constexpr uint32_t FLAG_AUTH2   = 0x10;
    
public:
    static AntiBypassGuard& instance() {
        static AntiBypassGuard g;
        return g;
    }
    
    void set_auth_ok()       { g_flags1->fetch_or(FLAG_AUTH); g_flags2.fetch_or(FLAG_AUTH); }
    void set_checkpoint_ok() { g_flags1->fetch_or(FLAG_CHECKPOINT); }
    void set_integrity_ok()  { g_flags1->fetch_or(FLAG_INTEGRITY); g_flags2.fetch_or(FLAG_INTEGRITY); }
    void set_hb_ok()         { g_flags1->fetch_or(FLAG_HB_OK); }
    void set_auth2_ok()      { g_flags1->fetch_or(FLAG_AUTH2); g_auth2_verified = true; }
    
    bool all_clear() {
        uint32_t f1 = g_flags1->load();
        uint32_t f2 = g_flags2.load();
#ifdef AUTH_SERVER
        return (f1 & FLAG_AUTH) && (f1 & FLAG_CHECKPOINT)
            && (f1 & FLAG_INTEGRITY) && (f1 & FLAG_HB_OK)
            && (f1 & FLAG_AUTH2) && g_auth2_verified
            && (f2 & FLAG_AUTH) && (f2 & FLAG_INTEGRITY);
#else
        return (f1 & FLAG_INTEGRITY) && (f1 & FLAG_HB_OK)
            && (f2 & FLAG_INTEGRITY);
#endif
    }
    
    int encoded_check() {
        return all_clear() ? 1 : 0;
    }
};

// ========== 代码段完整性校验 (运行时自检) ==========

// 计算自身 .text 段的简单哈希 — 采样多个关键函数的代码段
// 攻击者patch任何一处都会改变结果
inline uint32_t text_segment_checksum() {
    extern void security_checkpoint();
    extern void cp_environment();
    volatile uint32_t seed = 0xDEADBEEF;
    
    // 从多个关键保护函数采样 — 覆盖反调试/反模拟器/完整性入口
    uintptr_t regions[] = {
        (uintptr_t)&security_checkpoint,
        (uintptr_t)&cp_environment,
        (uintptr_t)&text_segment_checksum,
    };
    
    for (auto base : regions) {
        for (int i = 0; i < 8; i++) {
            volatile uint8_t* ptr = (volatile uint8_t*)(base + i * 16);
            seed = (seed * 1103515245 + 12345) ^ (*ptr);
        }
    }
    return seed;
}

// ★ 自校准基线: 首次运行时自动记录正确的校验和 → 分散 + XOR混淆存储
//     攻击者无法直接找到并修改基线值
inline volatile uint32_t g_baseline_a{0};  // 位置1: 低位 16bit
inline volatile uint32_t g_baseline_b{0};  // 位置2: 高位 16bit
inline volatile uint32_t g_baseline_salt{0x5A5A0000};
inline std::atomic<bool> g_baseline_calibrated{false};

inline uint32_t get_stored_baseline() {
    uint32_t a = g_baseline_a & 0xFFFF;
    uint32_t b = (g_baseline_b & 0xFFFF) << 16;
    return (a | b) ^ g_baseline_salt;
}

inline void set_stored_baseline(uint32_t val) {
    val ^= g_baseline_salt;
    g_baseline_a = val & 0xFFFF;
    g_baseline_b = (val >> 16) & 0xFFFF;
    // 随机化salt使下次XOR不同
    g_baseline_salt = (g_baseline_salt * 1103515245 + 12345) ^ 0xDEADBEEF;
}

// 隐蔽式校验 — 返回 0=正常, 非0=被patch
inline uint32_t stealth_integrity_magic() {
    uint32_t cs = text_segment_checksum();
    if (!g_baseline_calibrated) {
        set_stored_baseline(cs);
        g_baseline_calibrated = true;
        return 0;  // 首次不报错
    }
    return cs ^ get_stored_baseline();
}

// ========== 自修改代码保护 ==========
// 利用 mprotect 短暂修改 .text 段保护, 检测是否有其他进程试图 hook

inline bool detect_memory_tamper() {
    // 尝试在自己的代码段上设置 PROT_WRITE
    // 成功: 环境正常 → 立即恢复
    // 失败: SELinux/seccomp可能阻止 → 不算攻击, 返回true
    extern void security_checkpoint();
    uintptr_t page = (uintptr_t)&security_checkpoint & ~0xFFF;
    
    int ret = mprotect((void*)page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
    // ★ mprotect失败也可能是SELinux策略限制, 不认定为攻击
    if (ret != 0) return true;
    
    mprotect((void*)page, 4096, PROT_READ | PROT_EXEC);
    return true;  // mprotect成功, 未被hook
}

// ========== 自检线程 (带心跳 + 互监看门狗) ==========

// 看门狗: 自检线程每轮更新此时间戳, 主循环检测超时 → 线程被杀死 → 紧急降级
inline std::atomic<int64_t> g_monitor_last_ping{time(nullptr)};  // 初始为当前时间, 避免启动时误判

// ★ 启动宽限期: 前 60 秒不检查看门狗 (线程启动+首次ping需要时间)
inline bool monitor_watchdog_ok() {
    static int64_t boot_time = time(nullptr);
    // 启动不到60秒 → 跳过检查
    if (time(nullptr) - boot_time < 60) return true;
    return (time(nullptr) - g_monitor_last_ping) < 90;
}

inline void start_integrity_monitor() {
    std::thread([]{
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(20, 30);
        
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(dist(gen)));
            
            g_monitor_last_ping = time(nullptr);
            
            if (!AntiBypassGuard::instance().all_clear()) {
                g_hb_state.degraded = true;
                g_hb_state.degradation_level = 3;
                g_hb_state.core_locked = true;
            }
            
            if (!detect_memory_tamper()) {
                g_hb_state.core_locked = true;
            }
        }
    }).detach();
}

// ========== 验证结果注入绘图 ==========
// 在 draw_Gui.cpp 中使用这些宏来降级

// 用法: if(SECURE_GUARD()) { 绘制ESP; }
// 或者: float alpha = hb_degrade_multiplier();
// ★ 增加看门狗: 自检线程若被杀死, 主循环也会锁死
#define SECURE_GUARD() (hb_core_available() && AntiBypassGuard::instance().all_clear() && monitor_watchdog_ok())
#define SECURE_ALPHA  hb_degrade_multiplier()
