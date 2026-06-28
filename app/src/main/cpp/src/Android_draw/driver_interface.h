/*
 * driver_interface.h — 通用驱动抽象接口
 *
 * 所有内核驱动必须实现此接口。
 * 新增驱动: 继承 IDriver，实现纯虚方法，注册到 DriverRegistry 即可。
 *
 * 方法:
 *   probe()    — 探测驱动是否存在 (返回设备路径或null)
 *   connect()  — 打开设备, 建立连接
 *   handshake()— 握手验证 (可选, 默认返回true)
 *   initialize()— 设置目标进程 PID
 *   read()     — 读取目标进程内存
 *   write()    — 写入目标进程内存
 *   get_module_base() — 获取模块基址
 *   disconnect()— 关闭连接
 *   name()     — 驱动名称 (用于日志)
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <sys/types.h>

class IDriver {
public:
    virtual ~IDriver() = default;

    // ── 生命周期 ──

    /** 探测: 返回驱动设备路径, 未找到返回 nullptr */
    virtual const char* probe() = 0;

    /** 连接: 打开设备, 返回 fd (>0 成功, <=0 失败) */
    virtual int connect() = 0;

    /** 握手: 验证驱动可用性 (可选) */
    virtual bool handshake() { return true; }

    /** 设置目标进程 PID */
    virtual void initialize(pid_t pid) = 0;

    /** 断开连接 */
    virtual void disconnect() = 0;

    // ── 内存操作 ──

    /** 读取目标进程内存 */
    virtual bool read_mem(uintptr_t addr, void* buf, size_t size) = 0;

    /** 写入目标进程内存 */
    virtual bool write_mem(uintptr_t addr, const void* buf, size_t size) = 0;

    /** 获取模块基址 (可选, 默认返回0) */
    virtual uintptr_t get_module_base(const char*) { return 0; }

    // ── 元信息 ──

    /** 驱动名称 */
    virtual const char* name() const = 0;

    /** 当前文件描述符 */
    virtual int get_fd() const = 0;

    /** 连接状态 */
    virtual bool is_connected() const = 0;
};
