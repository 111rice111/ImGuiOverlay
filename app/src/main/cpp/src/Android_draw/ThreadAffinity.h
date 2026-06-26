#ifndef MERGED_TIMER_AFFINITY_H
#define MERGED_TIMER_AFFINITY_H
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <dirent.h>
#include <memory>
#include <sched.h>
#include <string>
#include <thread>
#include <unistd.h>
#define LITTLE_CORES_DEFAULT {4}
#define BIG_CORES_DEFAULT {5, 6}
extern char extractedString[64];
namespace CPUAffinityUtil {
inline pid_t get_pid_by_package() {
  int id = -1;
  std::unique_ptr<DIR, decltype(&closedir)> dir(::opendir("/proc"), &closedir);
  if (!dir) {
    perror("opendir /proc failed");
    return -1;
  }
  struct dirent *entry;
  char filename[64], cmdline[64];
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
      if ((std::strstr(cmdline, "dwrg") ||
           std::strstr(cmdline, "com.netease.idv")) &&
          std::strstr(cmdline, "com") && !std::strstr(cmdline, "PushService") &&
          !std::strstr(cmdline, "gcsdk")) {
        std::snprintf(extractedString, sizeof(extractedString), "%s", cmdline);
        printf("\033[1;36m[PKG]\033[0m 发现目标包名 \033[1;33m%s\033[0m  →  "
               "PID \033[1;32m%d\033[0m\n",
               extractedString, id);
        closedir(dir.get());
        return id;
      }
    }
  }
  closedir(dir.get());
  fprintf(stderr, "\033[1;31m[Error]\033[0m 未找到第五人格进程\n");
  return -1;
}
inline pid_t get_tid_by_name(pid_t pid, const char *thread_name) {
  if (pid <= 0 || !thread_name)
    return -1;
  char task_dir[64];
  snprintf(task_dir, sizeof(task_dir), "/proc/%d/task", pid);
  DIR *dir = opendir(task_dir);
  if (!dir) {
    perror("opendir task dir failed");
    return -1;
  }
  struct dirent *entry;
  pid_t tid = -1;
  char comm[128];
  while ((entry = readdir(dir)) != NULL) {
    if (!isdigit(entry->d_name[0]))
      continue;
    snprintf(comm, sizeof(comm), "%s/%s/comm", task_dir, entry->d_name);
    FILE *fp = fopen(comm, "r");
    if (!fp)
      continue;
    fgets(comm, sizeof(comm), fp);
    comm[strcspn(comm, "\n")] = '\0';
    if (strstr(comm, thread_name) != NULL) {
      tid = atoi(entry->d_name);
      fclose(fp);
      printf("\033[1;36m[THD]\033[0m 发现线程 \033[1;33m%s\033[0m  →  TID "
             "\033[1;32m%d\033[0m\n",
             thread_name, tid);
      closedir(dir);
      return tid;
    }
    fclose(fp);
  }
  closedir(dir);
  fprintf(stderr, "\033[1;31m[Error]\033[0m 进程 %d 中未找到线程 %s\n", pid,
          thread_name);
  return -1;
}
inline int set_thread_affinity(pid_t tid, const int *target_cores,
                               int core_count) {
  if (tid <= 0 || !target_cores || core_count <= 0)
    return -1;
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  for (int i = 0; i < core_count; i++)
    CPU_SET(target_cores[i], &cpuset);
  if (sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset) == -1) {
    perror("sched_setaffinity failed");
    return -1;
  }
  printf(
      "\033[1;35m[AFF]\033[0m Thread \033[1;32m%d\033[0m 已绑定到 CPU 核心：",
      tid);
  for (int i = 0; i < core_count; i++)
    printf("\033[1;34m%d\033[0m ", target_cores[i]);
  printf("\n");
  return 0;
}
inline int set_draw_thread_affinity(pid_t tid) {
  static const int little_cores[] = LITTLE_CORES_DEFAULT;
  int core_count = sizeof(little_cores) / sizeof(int);
  return set_thread_affinity(tid, little_cores, core_count);
}
inline int set_data_thread_affinity(pid_t tid) {
  static const int big_cores[] = BIG_CORES_DEFAULT;
  int core_count = sizeof(big_cores) / sizeof(int);
  return set_thread_affinity(tid, big_cores, core_count);
}
inline int auto_bind_draw_thread(const char *thread_name = "DrawThread") {
  pid_t pid = get_pid_by_package();
  if (pid == -1)
    return -1;
  pid_t tid = get_tid_by_name(pid, thread_name);
  if (tid == -1)
    return -1;
  return set_draw_thread_affinity(tid);
}
inline int auto_bind_data_thread(const char *thread_name = "DataThread") {
  pid_t pid = get_pid_by_package();
  if (pid == -1)
    return -1;
  pid_t tid = get_tid_by_name(pid, thread_name);
  if (tid == -1)
    return -1;
  return set_data_thread_affinity(tid);
}
} // namespace CPUAffinityUtil
struct Timer {
  bool fps_io = false;
  int events = 0;
  void *data = nullptr;
  struct timespec start_ts{}, end_ts{}, old_ts{}, now_ts{};
  long long lod_time = 0;
  long long loop_time = 0;
  long long run_time = 0;
  std::string name;
  unsigned long target_fps_interval = 0;
  long sleep_time = 0;
  unsigned long old_time = 0;
  unsigned long now_time = 0;
  bool is_loop_timer_start = false;
  Timer() = default;
  explicit Timer(const char *name_) : name(name_) {}
  explicit Timer(unsigned int fps) { SetFps(fps); }
  inline void SetName(const char *name_) { name = name_; }
  inline void Start() { clock_gettime(CLOCK_MONOTONIC, &start_ts); }
  inline float Stop(bool show_log = false) {
    clock_gettime(CLOCK_MONOTONIC, &end_ts);
    run_time = (1000000000LL * end_ts.tv_sec + end_ts.tv_nsec) -
               (1000000000LL * start_ts.tv_sec + start_ts.tv_nsec);
    float cost_ms = run_time / 1000000.0f;
    if (show_log && !name.empty())
      printf("\033[1;32m[Timer]\033[0m \033[1;37m%-12s\033[0m "
             "耗时：\033[1;33m%7.2f\033[0m ms\n",
             name.c_str(), cost_ms);
    return cost_ms;
  }
  inline void SetFps(unsigned int fps) {
    if (fps == 0)
      fps = 60;
    target_fps_interval = 1000000000ULL / fps;
  }
  inline void InitFpsControl() {
    clock_gettime(CLOCK_MONOTONIC, &old_ts);
    sleep_time = target_fps_interval;
    Start();
  }
  inline float ControlFps() {
    clock_gettime(CLOCK_MONOTONIC, &now_ts);
    old_time = (1000000000LL * now_ts.tv_sec + now_ts.tv_nsec) -
               (1000000000LL * old_ts.tv_sec + old_ts.tv_nsec);
    sleep_time = target_fps_interval - old_time;
    if (sleep_time < 0)
      sleep_time = 0;
    NanoSleep(sleep_time);
    clock_gettime(CLOCK_MONOTONIC, &old_ts);
    return (old_time + sleep_time) / 1000000.0f;
  }
  inline bool CheckTime(unsigned int ms) {
    if (lod_time == 0)
      Start();
    clock_gettime(CLOCK_MONOTONIC, &end_ts);
    lod_time = (1000000000LL * end_ts.tv_sec + end_ts.tv_nsec) -
               (1000000000LL * start_ts.tv_sec + start_ts.tv_nsec);
    lod_time /= 1000;
    if (lod_time >= ms) {
      lod_time = 0;
      return true;
    }
    return false;
  }
  inline void StartLoopTimer() {
    is_loop_timer_start = true;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);
  }
  inline void StopLoopTimer() { is_loop_timer_start = false; }
  inline long GetLoopTimeNs() {
    if (!is_loop_timer_start)
      return 0;
    clock_gettime(CLOCK_MONOTONIC, &end_ts);
    loop_time = (1000000000LL * end_ts.tv_sec + end_ts.tv_nsec) -
                (1000000000LL * start_ts.tv_sec + start_ts.tv_nsec);
    return loop_time;
  }
  inline void NanoSleep(long delay_ns) {
    if (delay_ns <= 0)
      return;
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = delay_ns;
    while (nanosleep(&req, &rem) == -1)
      req = rem;
  }
  inline int GetCpuCoreCount() {
    static int core_count = []() {
      pid_t tid = gettid();
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      sched_getaffinity(tid, sizeof(cpuset), &cpuset);
      int count = 0;
      while (CPU_ISSET(count, &cpuset))
        count++;
      return count;
    }();
    return core_count;
  }
  inline int BindCurrentThreadToCores(bool is_draw_thread,
                                      const char *thread_name) {
    pid_t tid = gettid();
    printf("\033[1;36m[INFO]\033[0m 当前线程 \033[1;37m%s\033[0m 的 "
           "TID：\033[1;32m%d\033[0m\n",
           thread_name, tid);
    if (is_draw_thread)
      return CPUAffinityUtil::set_draw_thread_affinity(tid);
    else
      return CPUAffinityUtil::set_data_thread_affinity(tid);
  }
};
#endif