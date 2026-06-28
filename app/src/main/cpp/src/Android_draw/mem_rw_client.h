/*
 * mem_rw_client.h — 用户态接口：通过 /dev/mem_rw 内核模块读写进程内存
 *
 * 用法:
 *   #include "mem_rw_client.h"
 *   KernelMemRW rw;
 *   rw.init();
 *   rw.read(pid, addr, buf, size);
 *   rw.write(pid, addr, buf, size);
 */

#pragma once
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <cstdint>
#include <cstdio>

#define MEM_RW_MAGIC 'M'
#define MEM_RW_READ  _IOWR(MEM_RW_MAGIC, 1, struct mem_rw_req)
#define MEM_RW_WRITE _IOW(MEM_RW_MAGIC, 2, struct mem_rw_req)

struct mem_rw_req {
    uint32_t pid;
    uint64_t addr;
    uint64_t size;
    uint64_t data[512]; // 4096 bytes buffer
};

class KernelMemRW {
    int fd;
public:
    bool init() {
        return init_via_path("/dev/mem_rw");
    }

    bool init_via_path(const char* device_path) {
        fd = open(device_path, O_RDWR);
        if (fd < 0) return false;
        printf("[KernelRW] 驱动就绪: %s\n", device_path);
        return true;
    }

    // 读取目标进程内存
    ssize_t read(pid_t pid, uint64_t addr, void* buf, size_t size) {
        if (fd < 0) return -1;
        struct mem_rw_req req = {};
        req.pid = pid;
        req.addr = addr;
        req.size = size;

        if (ioctl(fd, MEM_RW_READ, &req) < 0)
            return -1;

        memcpy(buf, req.data, size);
        return (ssize_t)size;
    }

    // 写入目标进程内存
    ssize_t write(pid_t pid, uint64_t addr, const void* buf, size_t size) {
        if (fd < 0) return -1;
        struct mem_rw_req req = {};
        req.pid = pid;
        req.addr = addr;
        req.size = size;
        memcpy(req.data, buf, size);

        if (ioctl(fd, MEM_RW_WRITE, &req) < 0)
            return -1;

        return (ssize_t)size;
    }

    void close_fd() {
        if (fd >= 0) { close(fd); fd = -1; }
    }

    ~KernelMemRW() { close_fd(); }
};
