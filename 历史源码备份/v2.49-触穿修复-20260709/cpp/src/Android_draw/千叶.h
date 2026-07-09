#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/syscall.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

inline std::atomic<int> pid = -1;

// ★ 内核驱动 — 唯一内存读写路径
#include "driver.h"

inline bool vm_readv(std::uintptr_t address, void *buffer, std::size_t size) {
  if (pid < 0 || !g_drv) return false;
  return g_drv->read_mem(address, buffer, size);
}

inline bool vm_writev(std::uintptr_t address, const void *buffer, std::size_t size) {
  if (pid < 0 || !g_drv) return false;
  return g_drv->write_mem(address, buffer, size);
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
  return var;
}

inline int get_name_pid(const char *packageName) {
  int id = -1;
  std::unique_ptr<DIR, decltype(&closedir)> dir(::opendir("/proc"), &closedir);
  if (!dir) return -1;
  struct dirent *entry;
  char filename[64]{};
  char cmdline[256]{};
  while ((entry = ::readdir(dir.get()))) {
    id = std::atoi(entry->d_name);
    if (id <= 0) continue;
    std::snprintf(filename, sizeof(filename), "/proc/%d/cmdline", id);
    std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(filename, "r"), &fclose);
    if (!fp) continue;
    if (std::fgets(cmdline, sizeof(cmdline), fp.get())) {
      if (std::strcmp(packageName, cmdline) == 0) {
        pid = id;
        if (g_drv) g_drv->initialize(id);
        return id;
      }
    }
  }
  return -1;
}

inline long getModuleBase(char *module_name) {
  char filename[64];
  std::snprintf(filename, sizeof(filename), "/proc/%d/maps", pid.load());
  std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(filename, "r"), &fclose);
  if (!fp) return 0;
  char line[1024]{};
  unsigned long addr = 0;
  while (std::fgets(line, sizeof(line), fp.get())) {
    if (std::strstr(line, module_name)) {
      char *pch = std::strtok(line, "-");
      addr = std::strtoul(pch, nullptr, 16);
      if (addr == 0x8000) addr = 0;
      break;
    }
  }
  return static_cast<long>(addr);
}
