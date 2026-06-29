/*
 * driver_registry.h — 驱动注册中心 + 自动检测加载
 *
 * 新增驱动三步:
 *   Step 1: 创建类继承 IDriver, 实现所有纯虚方法
 *   Step 2: #include 对应的头文件
 *   Step 3: 在 driver_init() 里加一行 try_load(new XxxDriver())
 */

#pragma once
#include "driver_interface.h"
#include "driver_rt.h"
#include "driver_twt.h"
#include <cstdio>
#include <cstdlib>
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
