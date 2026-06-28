/*
 * driver_registry.h — 驱动注册中心 + 自动检测加载
 *
 * 新增驱动只需两步:
 *   Step 1: 创建类继承 IDriver, 实现所有纯虚方法
 *   Step 2: 在 detect_and_load() 里加一行注册
 *
 * 使用:
 *   driver_init();                          // 自动检测, 加载第一个可用的驱动
 *   vm_readv → g_drv->read_mem(addr,buf,sz);
 */

#pragma once
#include "driver_interface.h"
#include "driver_rt.h"
#include <cstdio>
#include <cstdlib>

// 全局驱动指针
inline IDriver* g_drv = nullptr;

// 注册: 尝试探测 + 连接 + 握手
static bool try_driver(IDriver* d) {
    const char* path = d->probe();
    if (!path) { delete d; return false; }
    printf("[Registry] %s 驱动探测: %s\n", d->name(), path);
    if (d->connect() <= 0) { printf("[-] %s: 连接失败\n", d->name()); delete d; return false; }
    if (!d->handshake()) { printf("[-] %s: 握手失败\n", d->name()); d->disconnect(); delete d; return false; }
    printf("[Registry] %s 驱动就绪 (fd=%d)\n", d->name(), d->get_fd());
    return true;
}

// ── 在此添加新驱动 ──
// 顺序 = 优先级; 越靠前越优先
inline void detect_and_load() {
    if (g_drv) return;

    // 1. RT 内核驱动 (通用, ioctl 0x800-0x803)
    if (try_driver(new RTDriver())) { g_drv = g_drv ? g_drv : (IDriver*)0; /* set below */ return; }
    // g_drv 已在 try_driver 外部设置, 用下面的方式:
    // 实际: try_driver 不设置 g_drv, 我们需要在成功时设置

    // 2. QXQD 驱动 (1q2w3e4r5t 加密握手)
    // if (try_driver(new QXQDDriver())) return;

    // 3. MemRW 自有驱动
    // if (try_driver(new MemRWDriver())) return;

    // 4. ProcessVM 回退 (syscall, 无内核模块)
    // if (try_driver(new ProcessVMDriver())) return;

    printf("[Registry] !! 未找到任何可用驱动 !!\n");
    exit(1);
}

// ── 修正: try_driver 成功时设置 g_drv ──
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

// ── 统一入口 ──
inline void driver_init() {
    if (g_drv) return;

    // 按优先级尝试各驱动 (在此添加新驱动)
    if (try_load(new RTDriver())) return;
    // if (try_load(new QXQDDriver())) return;
    // if (try_load(new MemRWDriver())) return;
    // if (try_load(new ProcessVMDriver())) return;

    printf("[Registry] !! 未找到任何可用驱动 !!\n");
    exit(1);
}
