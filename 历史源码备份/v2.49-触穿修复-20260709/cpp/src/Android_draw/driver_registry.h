/*
 * driver_registry.h — 驱动注册中心 + 交互式选择
 *
 * 新增驱动三步:
 *   Step 1: 创建类继承 IDriver, 实现所有纯虚方法
 *   Step 2: #include 对应的头文件
 *   Step 3: 在 probe_all_drivers() / driver_init_by_name() / driver_init() 中注册
 *
 * 交互流程:
 *   probe_all_drivers() → 列出可用驱动 → 用户选择 → driver_init_by_name() 加载
 */

#pragma once
#include "driver_interface.h"
#include "driver_rt.h"
#include "driver_twt.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// 全局驱动指针
inline IDriver* g_drv = nullptr;

// ── 尝试加载驱动 ──
inline bool try_load(IDriver* d) {
    const char* path = d->probe();
    if (!path) { delete d; return false; }
    printf("[Registry] %s 驱动探测: %s\n", d->name(), path);
    if (d->connect() <= 0) { printf("[-] %s: 连接失败\n", d->name()); delete d; return false; }
    if (!d->handshake()) { printf("[-] %s: 握手失败\n", d->name()); d->disconnect(); delete d; return false; }
    g_drv = d;
    printf("[Registry] %s 驱动就绪 (fd=%d)\n", d->name(), d->get_fd());
    return true;
}

// ── 探测已适配驱动 (不实际连接, 仅 probe) ──
struct ProbeResult { std::string name; std::string desc; bool found; std::string device_path; };

inline std::vector<ProbeResult> probe_all_drivers() {
    std::vector<ProbeResult> result;
    
    {   RTDriver* d = new RTDriver();
        const char* p = d->probe();
        result.push_back({"RT", "RT 内核 (/dev/ 自动探测)", p != nullptr, p ? p : ""});
        delete d;
    }
    {   TWTDriver* d = new TWTDriver();
        const char* p = d->probe();
        result.push_back({"TWT", "TWT 内核 (MY_CALL 探测)", p != nullptr, p ? p : ""});
        delete d;
    }
    return result;
}

// ── 按名称加载指定驱动 ──
inline bool driver_init_by_name(const char* name) {
    if (g_drv) return true;  // 已加载
    if (strcmp(name, "RT") == 0)  return try_load(new RTDriver());
    if (strcmp(name, "TWT") == 0) return try_load(new TWTDriver());
    printf("[Registry] 未知驱动: %s\n", name);
    return false;
}

// ── 预检测: 列出编译时已适配的驱动 + 运行时状态 ──
struct DriverInfo { std::string name; bool available; std::string path; };

inline std::vector<DriverInfo> list_available_drivers() {
    // 不实际探测 (避免与 g_drv 争用 fd), 只报告编译时适配了哪些
    std::vector<DriverInfo> result;
    result.push_back({"TWT", g_drv && strcmp(g_drv->name(),"TWT")==0, g_drv ? "已加载" : "已适配"});
    result.push_back({"RT",  g_drv && strcmp(g_drv->name(),"RT" )==0, g_drv ? "已加载" : "已适配"});
    return result;
}

// ── 统一入口 ──
inline void driver_init() {
    if (g_drv) return;

    // 按优先级尝试各驱动 (在此添加新驱动 — 只需一行)
    if (try_load(new RTDriver())) return;   // RT 优先 (已验证可用 /dev/ssZDdB)
    if (try_load(new TWTDriver())) return;  // TWT 备选

    printf("[Registry] !! 未找到任何可用驱动 !!\n");
    exit(1);
}
