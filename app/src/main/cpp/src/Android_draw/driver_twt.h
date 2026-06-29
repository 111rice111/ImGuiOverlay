/*
 * driver_twt.h — TWT 内核驱动实现
 *
 * 通过内联汇编 MY_CALL 获取 fd, ioctl 命令: READ_MEM=4/WRITE_MEM=5/MODULE_BASE=1
 * 探测方式: MY_CALL(0x114514,0x1919810,0x2778,&fd) 或 /proc/self/fd 扫描
 */

#pragma once
#include "driver_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

class TWTDriver : public IDriver {
    int fd_ = -1;
    pid_t pid_ = -1;

    struct _request { pid_t pid; uintptr_t addr; void* buf; size_t size; };
    // ★ ioctl 命令必须用 _IOW 宏编码, 不是裸数字!
    enum {
        TWT_GET_PID   = _IOW('T', 0, _request),
        TWT_MOD_BASE  = _IOW('T', 1, _request),
        TWT_MOD_BSS   = _IOW('T', 3, _request),
        TWT_READ      = _IOW('T', 4, _request),
        TWT_WRITE     = _IOW('T', 5, _request),
        TWT_READ_V2   = _IOW('T', 11, _request),
    };

    // TWT 内联汇编 — 获取驱动 fd
    static int twt_call() {
        int fd = -1;
        register long x0 __asm__("x0") = 0x114514;
        register long x1 __asm__("x1") = 0x1919810;
        register long x2 __asm__("x2") = 0x2778;
        register long x3 __asm__("x3") = (long)&fd;
        register long x8 __asm__("x8") = __NR_reboot;
        __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x8) : "memory","cc");
        return fd;
    }

    // 备用: 扫描 /proc/self/fd 找 TwT_driver
    static int twt_scan_fd() {
        DIR* dir = opendir("/proc/self/fd");
        if (!dir) return -1;
        struct dirent* e; char link[256], path[256];
        while ((e = readdir(dir))) {
            if (strcmp(e->d_name,".")==0||strcmp(e->d_name,"..")==0) continue;
            snprintf(path,sizeof(path),"/proc/self/fd/%s",e->d_name);
            ssize_t n = readlink(path, link, sizeof(link)-1);
            if (n<=0) continue; link[n]=0;
            if (strstr(link,"TwT_driver") && strstr(link,"anon_inode:")) {
                closedir(dir);
                return atoi(e->d_name);
            }
        }
        closedir(dir);
        return -1;
    }

public:
    const char* name() const override { return "TWT"; }
    int get_fd() const override { return fd_; }
    bool is_connected() const override { return fd_ > 0; }

    const char* probe() override {
        if (fd_ > 0) return "TwT";
        int fd = twt_call();
        printf("[TWT] MY_CALL → fd=%d\n", fd);
        if (fd < 0) {
            fd = twt_scan_fd();
            printf("[TWT] scan_fd → fd=%d\n", fd);
        }
        if (fd > 0) { fd_ = fd; return "TwT"; }
        return nullptr;
    }

    bool handshake() override {
        if (fd_ <= 0) return false;
        ioctl(fd_, _IO('T', 19), 0);  // BP_INIT_CMD
        return true;
    }

    int connect() override {
        if (fd_ > 0) return fd_;
        return -1;
    }

    void initialize(pid_t p) override { pid_ = p; }
    void disconnect() override { if (fd_>0) { close(fd_); fd_=-1; } }

    bool read_mem(uintptr_t addr, void* buf, size_t size) override {
        if (fd_<0||pid_<=0) return false;
        _request req = {pid_, addr & 0xFFFFFFFFFFFFULL, buf, size};
        int r = ioctl(fd_, TWT_READ, &req);
        if (r != 0 && errno == EPERM) {
            // 未映射页, 用 V2 重试
            r = ioctl(fd_, TWT_READ_V2, &req);
        }
        return r == 0;
    }

    bool write_mem(uintptr_t addr, const void* buf, size_t size) override {
        if (fd_<0||pid_<=0) return false;
        _request req = {pid_, addr & 0xFFFFFFFFFFFFULL, const_cast<void*>(buf), size};
        return ioctl(fd_, TWT_WRITE, &req) == 0;
    }

    uintptr_t get_module_base(const char* name) override {
        if (fd_<0||pid_<=0||!name) return 0;
        _request req = {pid_, 0, nullptr, 0};
        char nb[256]; snprintf(nb,sizeof(nb),"%s",name); req.buf=nb;
        if (ioctl(fd_, TWT_MOD_BASE, &req)!=0) return 0;
        return req.addr;
    }

    // ── 触摸 (通过 TWT 内核驱动, 绕过 /dev/input/event* 封锁) ──
    struct touch_ev { int slot, x, y; };

    bool touch_init(int mode = 0) override {
        touch_ev t = {mode, 0, 0};
        int r = ioctl(fd_, _IOW('T', 6, touch_ev), &t);
        if (r != 0) {
            printf("[TWT] touch_init(%d) failed ret=%d errno=%d\n", mode, r, errno);
            if (errno == EALREADY) { printf("[TWT] touch already inited\n"); return true; }
            return false;
        }
        printf("[TWT] touch_init ok mode=%d\n", mode);
        return true;
    }
    bool touch_down(int x, int y) override {
        touch_ev t = {0, x, y};
        return ioctl(fd_, _IOW('T', 7, touch_ev), &t) == 0;
    }
    bool touch_up() override {
        touch_ev t = {0, 0, 0};
        return ioctl(fd_, _IOW('T', 8, touch_ev), &t) == 0;
    }
};
