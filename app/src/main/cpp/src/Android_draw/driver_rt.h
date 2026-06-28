/*
 * driver_rt.h — RT 内核驱动实现
 *
 * ioctl 命令: OP_INIT_KEY=0x800, OP_READ_MEM=0x801, OP_WRITE_MEM=0x802, OP_MODULE_BASE=0x803
 * 设备特征: /dev/ 下 6 位纯字母名, size=0, uid=gid=0, atime==ctime
 */

#pragma once
#include "driver_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <vector>

class RTDriver : public IDriver {
    int fd_ = -1;
    pid_t pid_ = -1;
    char device_path_[256] = {};

    struct COPY_MEMORY { pid_t pid; uintptr_t addr; void* buf; size_t size; };
    struct MODULE_BASE  { pid_t pid; char* name; uintptr_t base; };

    enum { OP_INIT=0x800, OP_READ=0x801, OP_WRITE=0x802, OP_MOD=0x803 };

public:
    const char* name() const override { return "RT"; }
    int get_fd() const override { return fd_; }
    bool is_connected() const override { return fd_ > 0; }

    const char* probe() override {
        DIR* dir = opendir("/dev");
        if (!dir) return nullptr;
        const std::vector<std::string> excl = {"binder","common","ashmem","stdin","stdout","stderr"};
        struct dirent* e;
        while ((e = readdir(dir))) {
            const char* n = e->d_name;
            if (strcmp(n,".")==0||strcmp(n,"..")==0) continue;
            if (strstr(n,"gpiochip")||strchr(n,'_')||strchr(n,'-')||strchr(n,':')) continue;
            if (strlen(n) != 6) continue;
            bool skip = false;
            for (auto& x : excl) { if (strcmp(n,x.c_str())==0) {skip=true;break;} }
            if (skip) continue;
            char p[256]; snprintf(p,sizeof(p),"/dev/%s",n);
            struct stat st;
            if (stat(p,&st)<0) continue;
            if (!S_ISCHR(st.st_mode)&&!S_ISBLK(st.st_mode)) continue;
            if (localtime(&st.st_ctime)->tm_year+1900<=1980) continue;
            if (st.st_size==0 && st.st_gid==0 && st.st_uid==0 && st.st_atime==st.st_ctime) {
                snprintf(device_path_, sizeof(device_path_), "%s", p);
                closedir(dir);
                return device_path_;
            }
        }
        closedir(dir);
        return nullptr;
    }

    int connect() override {
        if (device_path_[0] == 0) return -1;
        fd_ = open(device_path_, O_RDWR);
        return fd_;
    }

    void initialize(pid_t p) override { pid_ = p; }
    void disconnect() override { if (fd_>0) { close(fd_); fd_=-1; } }

    bool read_mem(uintptr_t addr, void* buf, size_t size) override {
        if (fd_<0||pid_<=0) return false;
        COPY_MEMORY cm = {pid_, addr, buf, size};
        return ioctl(fd_, OP_READ, &cm) == 0;
    }

    bool write_mem(uintptr_t addr, const void* buf, size_t size) override {
        if (fd_<0||pid_<=0) return false;
        COPY_MEMORY cm;
        cm.pid=pid_; cm.addr=addr; cm.buf=const_cast<void*>(buf); cm.size=size;
        return ioctl(fd_, OP_WRITE, &cm) == 0;
    }

    uintptr_t get_module_base(const char* name) override {
        if (fd_<0||pid_<=0) return 0;
        MODULE_BASE mb; char kb[256];
        strcpy(kb, name); mb.pid=pid_; mb.name=kb;
        if (ioctl(fd_, OP_MOD, &mb)!=0) return 0;
        return mb.base;
    }
};
