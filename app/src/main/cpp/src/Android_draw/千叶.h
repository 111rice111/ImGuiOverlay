#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <span>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

inline std::atomic<int> pid = -1;

// ★ 内核驱动接口 (优先使用, 更安全)
#include "mem_rw_client.h"
static KernelMemRW* g_kernel_rw = nullptr;
static bool g_use_kernel_drv = false;

// 自动扫描并打开内核驱动设备
inline bool kernel_driver_init() {
    if (g_kernel_rw) return g_use_kernel_drv;

    // 1. 先尝试已知路径
    const char* known_paths[] = {
        "/dev/mem_rw",
        nullptr
    };
    for (int i = 0; known_paths[i]; i++) {
        if (access(known_paths[i], F_OK) == 0) {
            g_kernel_rw = new KernelMemRW();
            if (g_kernel_rw->init()) {
                g_use_kernel_drv = true;
                printf("[Driver] 内核驱动已加载: %s\n", known_paths[i]);
                return true;
            }
            delete g_kernel_rw; g_kernel_rw = nullptr;
        }
    }

    // 2. 自动扫描 /dev/ 目录 (哈基米风格)
    DIR* dir = opendir("/dev");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir))) {
            const char* name = entry->d_name;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            // 跳过系统设备
            if (strstr(name, "binder") || strstr(name, "ashmem") ||
                strchr(name, '_') || strchr(name, '-') || strchr(name, ':')) continue;
            if (strcmp(name, "common") == 0 || strcmp(name, "stdin") == 0 ||
                strcmp(name, "stdout") == 0 || strcmp(name, "stderr") == 0) continue;

            char path[256]; snprintf(path, sizeof(path), "/dev/%s", name);
            struct stat st;
            if (stat(path, &st) < 0) continue;
            // 只接受字符设备，size=0，root 所有
            if (!S_ISCHR(st.st_mode)) continue;
            if (st.st_size != 0 || st.st_gid != 0 || st.st_uid != 0) continue;

            int fd = open(path, O_RDWR);
            if (fd >= 0) {
                close(fd);
                g_kernel_rw = new KernelMemRW();
                if (g_kernel_rw->init_via_path(path)) {
                    g_use_kernel_drv = true;
                    printf("[Driver] 自动发现驱动: %s\n", path);
                    closedir(dir);
                    return true;
                }
                delete g_kernel_rw; g_kernel_rw = nullptr;
            }
        }
        closedir(dir);
    }

    printf("[Driver] 未找到内核驱动，回退到 process_vm_readv\n");
    return false;
}

#if defined(__arm__)
inline constexpr int process_vm_readv_syscall = 376;
inline constexpr int process_vm_writev_syscall = 377;
#elif defined(__aarch64__)
inline constexpr int process_vm_readv_syscall = 270;
inline constexpr int process_vm_writev_syscall = 271;
#elif defined(__i386__)
inline constexpr int process_vm_readv_syscall = 347;
inline constexpr int process_vm_writev_syscall = 348;
#else
inline constexpr int process_vm_readv_syscall = 310;
inline constexpr int process_vm_writev_syscall = 311;
#endif

class PhysicalAddressCache {
private:
  std::unordered_map<std::size_t, bool> cache;
  int pagemap_fd = -1;
  int current_pid = -1;

  void ensure_pagemap_open() {
    int expected_pid = pid.load();
    if (expected_pid != current_pid || pagemap_fd == -1) {
      if (pagemap_fd != -1) {
        ::close(pagemap_fd);
        pagemap_fd = -1;
      }
      if (expected_pid > 0) {
        char pagemap_file[256];
        std::snprintf(pagemap_file, sizeof(pagemap_file), "/proc/%d/pagemap",
                      expected_pid);
        pagemap_fd = ::open(pagemap_file, O_RDONLY);
        current_pid = expected_pid;
      }
    }
  }

  bool calculate_physical_address_direct(std::uintptr_t virtual_address) {
    ensure_pagemap_open();
    if (pagemap_fd == -1)
      return false;

    std::uint64_t item{};
    std::size_t page_nr = virtual_address / getpagesize();

    bool ok = ::pread(pagemap_fd, &item, sizeof(item),
                      page_nr * sizeof(item)) == sizeof(item) &&
              (item & (1ULL << 63));
    return ok;
  }

public:
  ~PhysicalAddressCache() {
    if (pagemap_fd != -1) {
      ::close(pagemap_fd);
    }
  }

  bool is_address_valid(std::uintptr_t virtual_address) {
    if (pid < 0)
      return false;
    std::size_t page_nr = virtual_address / getpagesize();
    auto it = cache.find(page_nr);
    if (it != cache.end())
      return it->second;

    bool valid = calculate_physical_address_direct(virtual_address);
    cache[page_nr] = valid;
    return valid;
  }

  void clear() { cache.clear(); }
};

inline PhysicalAddressCache physical_cache;

class MemoryBatchReader {
private:
  std::vector<iovec> local_iovs;
  std::vector<iovec> remote_iovs;

public:
  void add_read(uintptr_t remote_addr, void *local_buf, size_t size) {
    local_iovs.push_back({local_buf, size});
    remote_iovs.push_back({reinterpret_cast<void *>(remote_addr), size});
  }

  bool execute_batch() {
    if (local_iovs.empty())
      return true;
    ssize_t result =
        syscall(process_vm_readv_syscall, pid.load(), local_iovs.data(),
                local_iovs.size(), remote_iovs.data(), remote_iovs.size(), 0);
    bool success = result != -1;
    local_iovs.clear();
    remote_iovs.clear();
    return success;
  }
};

inline bool calculate_physical_address(std::uintptr_t virtual_address) {
  return physical_cache.is_address_valid(virtual_address);
}

inline bool vm_readv(std::uintptr_t address, void *buffer, std::size_t size) {
  if (pid < 0) return false;
  // ★ 优先走内核驱动
  if (g_use_kernel_drv && g_kernel_rw) {
    return g_kernel_rw->read(pid.load(), address, buffer, size) > 0;
  }
  // 回退: process_vm_readv 系统调用
  struct iovec local = {buffer, size};
  struct iovec remote = {reinterpret_cast<void *>(address), size};
  ssize_t bytes =
      syscall(process_vm_readv_syscall, pid.load(), &local, 1, &remote, 1, 0);
  return static_cast<std::size_t>(bytes) == size;
}

// ★ 内核级写入: 优先内核驱动, 回退 process_vm_writev
inline bool vm_writev(std::uintptr_t address, const void *buffer, std::size_t size) {
  if (pid < 0) return false;
  // ★ 优先走内核驱动
  if (g_use_kernel_drv && g_kernel_rw) {
    return g_kernel_rw->write(pid.load(), address, buffer, size) > 0;
  }
  // 回退: process_vm_writev 系统调用
  struct iovec local = {const_cast<void *>(buffer), size};
  struct iovec remote = {reinterpret_cast<void *>(address), size};
  ssize_t bytes =
      syscall(process_vm_writev_syscall, pid.load(), &local, 1, &remote, 1, 0);
  return static_cast<std::size_t>(bytes) == size;
}

inline float getfloat(std::uintptr_t addr) {
  float var = 0.0f;
  vm_readv(addr, &var, sizeof(var));
  return var;
}

inline float getFloat(std::uintptr_t addr) { return getfloat(addr); }

inline int getdword(std::uintptr_t addr) {
  int var = 0;
  vm_readv(addr, &var, sizeof(var));
  return var;
}

inline int getDword(std::uintptr_t addr) { return getdword(addr); }

inline std::uintptr_t getPtr64(std::uintptr_t addr) {
  std::uintptr_t var = 0;
  vm_readv(addr, &var, sizeof(var));
  return calculate_physical_address(var) ? var : 0;
}

inline int get_name_pid(const char *packageName) {
  int id = -1;
  std::unique_ptr<DIR, decltype(&closedir)> dir(::opendir("/proc"), &closedir);
  if (!dir)
    return -1;
  struct dirent *entry;
  char filename[64]{};
  char cmdline[256]{};
  while ((entry = ::readdir(dir.get()))) {
    id = std::atoi(entry->d_name);
    if (id <= 0)
      continue;
    std::snprintf(filename, sizeof(filename), "/proc/%d/cmdline", id);
    std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(filename, "r"),
                                                &fclose);
    if (!fp)
      continue;
    if (std::fgets(cmdline, sizeof(cmdline), fp.get())) {
      if (std::strcmp(packageName, cmdline) == 0) {
        pid = id;
        return id;
      }
    }
  }
  return -1;
}

inline long getModuleBase(char *module_name) {
  char filename[64];
  std::snprintf(filename, sizeof(filename), "/proc/%d/maps", pid.load());
  std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(filename, "r"),
                                              &fclose);
  if (!fp)
    return 0;
  char line[1024]{};
  unsigned long addr = 0;
  while (std::fgets(line, sizeof(line), fp.get())) {
    if (std::strstr(line, module_name)) {
      char *pch = std::strtok(line, "-");
      addr = std::strtoul(pch, nullptr, 16);
      if (addr == 0x8000)
        addr = 0;
      break;
    }
  }
  return static_cast<long>(addr);
}
