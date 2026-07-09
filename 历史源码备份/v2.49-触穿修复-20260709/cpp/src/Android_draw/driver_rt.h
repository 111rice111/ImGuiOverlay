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
#include <linux/input.h>
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

    // ── 触摸 (evdev 注入, 跟哈基米一样) ──
    int touch_fd_ = -1;
    int touch_max_x_ = 23999, touch_max_y_ = 33919;
    int scr_w_ = 3392, scr_h_ = 2400;

    bool touch_init(int = 0) override {
        if (touch_fd_ > 0) return true;
        for (int i = 0; i < 16; i++) {
            char p[64]; snprintf(p, sizeof(p), "/dev/input/event%d", i);
            int fd = open(p, O_RDWR);
            if (fd < 0) continue;
            unsigned long bits[(ABS_MAX/64)+1] = {};
            if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0) { close(fd); continue; }
            auto chk = [&](int c) { return (bits[c/64] >> (c%64)) & 1; };
            if (!chk(ABS_MT_SLOT) || !chk(ABS_MT_TRACKING_ID) ||
                !chk(ABS_MT_POSITION_X) || !chk(ABS_MT_POSITION_Y)) { close(fd); continue; }
            struct input_absinfo ai;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0) touch_max_x_ = ai.maximum;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0) touch_max_y_ = ai.maximum;
            touch_fd_ = fd;
            printf("[RT] 触摸: %s (max %d,%d)\n", p, touch_max_x_, touch_max_y_);
            return true;
        }
        return false;
    }
    bool touch_down(int x, int y) override {
        if (touch_fd_ < 0) return false;
        // ★ XY交换: 触摸控制器轴跟屏幕轴反了
        int rx = (scr_h_ > 0) ? (y * touch_max_x_ / scr_h_) : y;
        int ry = (scr_w_ > 0) ? (x * touch_max_y_ / scr_w_) : x;
        struct input_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS; ev.code = ABS_MT_SLOT; ev.value = 0; write(touch_fd_, &ev, sizeof(ev));
        ev.code = ABS_MT_TRACKING_ID; ev.value = 0; write(touch_fd_, &ev, sizeof(ev));
        ev.type = EV_KEY; ev.code = BTN_TOUCH; ev.value = 1; write(touch_fd_, &ev, sizeof(ev));
        ev.code = BTN_TOOL_FINGER; ev.value = 1; write(touch_fd_, &ev, sizeof(ev));
        ev.type = EV_ABS; ev.code = ABS_MT_POSITION_X; ev.value = rx; write(touch_fd_, &ev, sizeof(ev));
        ev.code = ABS_MT_POSITION_Y; ev.value = ry; write(touch_fd_, &ev, sizeof(ev));
        ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0; write(touch_fd_, &ev, sizeof(ev));
        return true;
    }
    bool touch_up() override {
        if (touch_fd_ < 0) return false;
        struct input_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS; ev.code = ABS_MT_SLOT; ev.value = 0; write(touch_fd_, &ev, sizeof(ev));
        ev.code = ABS_MT_TRACKING_ID; ev.value = -1; write(touch_fd_, &ev, sizeof(ev));
        ev.type = EV_KEY; ev.code = BTN_TOUCH; ev.value = 0; write(touch_fd_, &ev, sizeof(ev));
        ev.code = BTN_TOOL_FINGER; ev.value = 0; write(touch_fd_, &ev, sizeof(ev));
        ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0; write(touch_fd_, &ev, sizeof(ev));
        return true;
    }
};
