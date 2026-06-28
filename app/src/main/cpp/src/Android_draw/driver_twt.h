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
    enum { TWT_READ=4, TWT_READ_V2=11, TWT_WRITE=5, TWT_MOD_BASE=1, TWT_MOD_BSS=3, TWT_GET_PID=0 };

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
        if (fd_ > 0) return "TwT";  // already connected
        int fd = twt_call();
        if (fd < 0) fd = twt_scan_fd();
        if (fd > 0) { fd_ = fd; return "TwT (syscall)"; }
        return nullptr;
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
        return ioctl(fd_, TWT_READ, &req) == 0;
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
};
