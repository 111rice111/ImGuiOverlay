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

// ── 预检测: 列出当前系统可用的驱动 (不改变 g_drv) ──
struct DriverInfo { std::string name; bool available; std::string path; };

inline std::vector<DriverInfo> list_available_drivers() {
    std::vector<DriverInfo> result;

    // 探测各驱动 (试探性, 用完即销毁)
    auto probe_one = [&](IDriver* d) {
        DriverInfo di;
        di.name = d->name();
        const char* p = d->probe();
        di.available = (p != nullptr);
        di.path = p ? p : "";
        result.push_back(di);
        delete d;
    };

    // RT 驱动: 扫描 /dev/ 找 6 位设备
    probe_one(new RTDriver());
    // TWT 驱动: MY_CALL 内联汇编
    probe_one(new TWTDriver());

    return result;
}

// ── 统一入口 ──
inline void driver_init() {
    if (g_drv) return;

    // 按优先级尝试各驱动 (在此添加新驱动 — 只需一行)
    if (try_load(new TWTDriver())) return;  // TWT 优先级最高
    if (try_load(new RTDriver())) return;   // RT 通用版

    printf("[Registry] !! 未找到任何可用驱动 !!\n");
    exit(1);
}
