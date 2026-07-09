#include "TouchHelperA.h"
#include "Utils.h"
#include "imgui.h"
#include "spinlock.h"
#include <cerrno>
#include <cctype>
#include <cmath>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#define maxE 5
#define maxF 10
#define UNGRAB 0
#define GRAB 1
// TODO 触摸穿透
namespace Touch {
static struct {
  input_event downEvent[2]{{{}, EV_KEY, BTN_TOUCH, 1},
                           {{}, EV_KEY, BTN_TOOL_FINGER, 1}};
  input_event event[512]{0};
} input;
static My_Vector2 touch_scale;
static My_Vector2 screenSize;
static int screenX_max = 0;  // v2.42: 触摸驱动 absX.maximum（用于判断 absX 对应长/短边）
static int screenY_max = 0;  // v2.42: 触摸驱动 absY.maximum
static int screenX_min = 0;  // ★ v2.45: absX.minimum（部分设备非零）
static int screenY_min = 0;  // ★ v2.45: absY.minimum
static int screenX_range = 0; // ★ v2.45: absX.max - absX.min
static int screenY_range = 0; // ★ v2.45: absY.max - absY.min
static std::vector<Device> devices;
static int nowfd;
static int orientation = 0;
static bool initialized = false;
static bool readOnly = false;
static bool otherTouch = false;
static std::function<void(std::vector<Device> *)> callback;
static spinlock lock;
void Upload() {
  static bool isFirstDown = true;
  int tmpCnt = 0, tmpCnt2 = 0;
  for (auto &device : devices) {
    for (auto &finger : device.Finger) {
      if (finger.isDown) {
        if (tmpCnt2++ > 20) {
          goto finish;
        }
        input.event[tmpCnt].type = EV_ABS;
        input.event[tmpCnt].code = ABS_X;
        input.event[tmpCnt].value = (int)finger.pos.x;
        tmpCnt++;
        input.event[tmpCnt].type = EV_ABS;
        input.event[tmpCnt].code = ABS_Y;
        input.event[tmpCnt].value = (int)finger.pos.y;
        tmpCnt++;
        input.event[tmpCnt].type = EV_ABS;
        input.event[tmpCnt].code = ABS_MT_POSITION_X;
        input.event[tmpCnt].value = (int)finger.pos.x;
        tmpCnt++;
        input.event[tmpCnt].type = EV_ABS;
        input.event[tmpCnt].code = ABS_MT_POSITION_Y;
        input.event[tmpCnt].value = (int)finger.pos.y;
        tmpCnt++;
        input.event[tmpCnt].type = EV_ABS;
        input.event[tmpCnt].code = ABS_MT_TRACKING_ID;
        input.event[tmpCnt].value = finger.id;
        tmpCnt++;
        input.event[tmpCnt].type = EV_SYN;
        input.event[tmpCnt].code = SYN_MT_REPORT;
        input.event[tmpCnt].value = 0;
        tmpCnt++;
      }
    }
  }
finish:
  bool is = false;
  if (tmpCnt == 0) {
    input.event[tmpCnt].type = EV_SYN;
    input.event[tmpCnt].code = SYN_MT_REPORT;
    input.event[tmpCnt].value = 0;
    tmpCnt++;
    if (!isFirstDown) {
      isFirstDown = true;
      input.event[tmpCnt].type = EV_KEY;
      input.event[tmpCnt].code = BTN_TOUCH;
      input.event[tmpCnt].value = 0;
      tmpCnt++;
      input.event[tmpCnt].type = EV_KEY;
      input.event[tmpCnt].code = BTN_TOOL_FINGER;
      input.event[tmpCnt].value = 0;
      tmpCnt++;
    }
  } else {
    is = true;
  }
  input.event[tmpCnt].type = EV_SYN;
  input.event[tmpCnt].code = SYN_REPORT;
  input.event[tmpCnt].value = 0;
  tmpCnt++;
  if (is && isFirstDown) {
    isFirstDown = false;
    write(nowfd, &input, sizeof(struct input_event) * (tmpCnt + 2));
  } else {
    write(nowfd, input.event, sizeof(struct input_event) * tmpCnt);
  }
}
/*void *TypeB(void *arg) {
int i = (int) (long) arg;
Device &device = devices[i];
int latest = 0;
input_event inputEvent[64]{0};
while (Touch_initialized) {
auto readSize = (int32_t) read(origfd[i], inputEvent, sizeof(inputEvent));
if (readSize <= 0 || (readSize % sizeof(input_event)) != 0) {
continue;
}
size_t count = size_t(readSize) / sizeof(input_event);
for (size_t j = 0; j < count; j++) {
input_event &ie = inputEvent[j];
if (latest < 0)
latest = 0;
if (latest >= 10)
continue;
if (ie.code == ABS_MT_TRACKING_ID) {
if (ie.value < 0) {
Finger[i][latest].isDown = false;
} else {
Finger[i][latest].isDown = true;
}
Finger[i][latest].id = (i * 2 + 1) * maxF + ie.value;
continue;
}
if (ie.code == ABS_MT_POSITION_X) {
Finger[i][latest].isDown = true;
Finger[i][latest].x = (int) (ie.value * S2TX);
continue;
}
if (ie.code == ABS_MT_POSITION_Y) {
Finger[i][latest].isDown = true;
Finger[i][latest].y = (int) (ie.value * S2TY);
continue;
}
if (ie.code == SYN_MT_REPORT) {
latest += 1;
continue;
}
if (ie.code == SYN_REPORT) {
Upload();
memset(&Finger[i][0], 0, sizeof(Finger) * 10);
latest = -1;
continue;
}
}
}
return nullptr;
}*/
static void *TypeA(void *arg) {
  int i = (int)(long)arg;
  Device &device = devices[i];
  int latest = 0;
  input_event inputEvent[64]{0};
  bool eacces_logged = false;  // ★ v2.48: SELinux 拒绝只打印一次, 避免刷屏
  while (initialized) {
    // ★ v2.48: 用 poll 带超时, 避免安卓16 SELinux/evdev 变化导致永久阻塞
    // 原代码: 阻塞式 read(), 设备不产生事件时永久卡死, io.MousePos 不更新
    struct pollfd pfd;
    pfd.fd = device.fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int pret = poll(&pfd, 1, 500);  // 500ms 超时, 与音量键线程 select 类似的健壮性
    if (pret <= 0) {
      if (!initialized) break;
      continue;  // 超时或被信号中断, 重新循环
    }
    if (!(pfd.revents & POLLIN)) continue;

    auto readSize = (int32_t)read(device.fd, inputEvent, sizeof(inputEvent));
    if (readSize <= 0 || (readSize % sizeof(input_event)) != 0) {
      if (readSize < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        continue;  // 非阻塞模式下正常, 无数据可读
      }
      if (readSize < 0 && errno == EACCES && !eacces_logged) {
        // SELinux 拒绝读取触摸设备
        printf("[Touch] 设备%d 读取被拒绝(EACCES), 可能SELinux限制 (安卓%d)\n",
               i, getAndroidVersion());
        eacces_logged = true;
        usleep(2000000);  // 2秒后再试, 避免刷屏
        continue;
      }
      continue;
    }
    size_t count = size_t(readSize) / sizeof(input_event);
    lock.lock();
    for (size_t j = 0; j < count; j++) {
      input_event &ie = inputEvent[j];
      if (ie.type == EV_ABS) {
        if (ie.code == ABS_MT_SLOT) {
          latest = ie.value;
          continue;
        }
        if (ie.code == ABS_MT_TRACKING_ID) {
          if (ie.value == -1) {
            device.Finger[latest].isDown = false;
          } else {
            device.Finger[latest].id = (i * 2 + 1) * maxF + latest;
            device.Finger[latest].isDown = true;
          }
          continue;
        }
        if (ie.code == ABS_MT_POSITION_X) {
          device.Finger[latest].id = (i * 2 + 1) * maxF + latest;
          device.Finger[latest].pos.x = (float)ie.value * device.S2TX;
          continue;
        }
        if (ie.code == ABS_MT_POSITION_Y) {
          device.Finger[latest].id = (i * 2 + 1) * maxF + latest;
          device.Finger[latest].pos.y = (float)ie.value * device.S2TY;
          continue;
        }
      }
      if (ie.code == SYN_REPORT) {
        // ★ v2.47: 实现 AutoAim.cpp 注释要求的 slot 过滤
        // 自瞄在 SLOT=1 注入触摸事件, 不应更新 ImGui 鼠标, 否则UI无法操作
        if (latest >= 1) {
          if (!readOnly) {
            if (callback) {
              callback(&devices);
            } else {
              Upload();
            }
          }
          continue;
        }
        if (ImGui::GetCurrentContext() != nullptr) {
          ImGuiIO &io = ImGui::GetIO();
          if (device.Finger[latest].isDown) {
            auto pos = Touch2Screen(device.Finger[latest].pos);
            io.MousePos = ImVec2(pos.x, pos.y);
            io.MouseDown[0] = true;
          } else {
            io.MouseDown[0] = false;
          }
        }
        if (!readOnly) {
          if (callback) {
            callback(&devices);
          } else {
            Upload();
          }
        }
        continue;
      }
    }
    lock.unlock();
  }
  return nullptr;
}
// ★ v2.48: 检查 EV_KEY 位图中是否包含指定按键
static bool checkKeyBit(int fd, int key) {
  uint8_t bits[KEY_MAX / 8 + 1] = {0};
  if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return false;
  return (bits[key / 8] & (1 << (key % 8))) != 0;
}
static bool checkDeviceIsTouch(int fd) {
  // ★ v2.48 综合修复: 多重检测, 兼容安卓16 evdev/InputFlinger 协议变化
  // 主检测: ABS_MT_POSITION_X + ABS_MT_POSITION_Y (原 v2.47 逻辑)
  // 回退1: 任意 ABS_MT_* + BTN_TOUCH 或 BTN_TOOL_FINGER
  // 回退2: 设备名包含 touch/ts/panel + 有 EV_ABS
  uint8_t *bits = NULL;
  ssize_t bits_size = 0;
  int res, j, k;
  bool itmp2 = false, itmp3 = false;
  bool has_any_abs_mt = false;   // 任意 ABS_MT_* 事件
  bool has_ev_abs = false;       // 有任意 EV_ABS 事件
  struct input_absinfo abs{};
  while (true) {
    res = ioctl(fd, EVIOCGBIT(EV_ABS, bits_size), bits);
    if (res < bits_size)
      break;
    bits_size = res + 16;
    bits = (uint8_t *)realloc(bits, bits_size * 2);
  }
  has_ev_abs = (res > 0);
  for (j = 0; j < res; j++) {
    for (k = 0; k < 8; k++)
      if (bits[j] & 1 << k) {
        int abs_code = j * 8 + k;
        if (ioctl(fd, EVIOCGABS(abs_code), &abs) == 0) {
          if (abs_code == ABS_MT_POSITION_X) {
            itmp2 = true;
            continue;
          }
          if (abs_code == ABS_MT_POSITION_Y) {
            itmp3 = true;
            continue;
          }
          // 任意 ABS_MT_* 事件 (ABS_MT_SLOT/TOUCH_MAJOR/PRESSURE/ORIENTATION等)
          // NDK 未定义 ABS_MT_FIRST/ABS_MT_LAST, 用 ABS_MT_SLOT~ABS_MT_TOOL_Y 范围替代
          if (abs_code >= ABS_MT_SLOT && abs_code <= ABS_MT_TOOL_Y) {
            has_any_abs_mt = true;
          }
        }
      }
  }
  free(bits);

  // 主检测通过
  if (itmp2 && itmp3) return true;

  // ★ v2.48 回退检测1: 有任意 ABS_MT_* + BTN_TOUCH 或 BTN_TOOL_FINGER
  // 适用于安卓16 EVIOCGBIT 不返回 POSITION_X/Y 但仍报告其他 ABS_MT 事件的情况
  bool has_btn_touch = checkKeyBit(fd, BTN_TOUCH);
  bool has_btn_tool_finger = checkKeyBit(fd, BTN_TOOL_FINGER);
  if (has_any_abs_mt && (has_btn_touch || has_btn_tool_finger)) {
    printf("[Touch] 回退检测1命中: ABS_MT + BTN (POS_X=%d POS_Y=%d btn_touch=%d btn_finger=%d)\n",
           itmp2, itmp3, has_btn_touch, has_btn_tool_finger);
    return true;
  }

  // ★ v2.48 回退检测2: 设备名匹配 + 有 EV_ABS
  // 适用于安卓16 evdev 协议大幅变化但设备名仍可识别的情况
  char name[256] = {0};
  if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && name[0]) {
    std::string nname(name);
    std::string lower = nname;
    for (auto &c : lower) c = (char)tolower((unsigned char)c);
    // ★ 排除键盘/笔/按键设备 (避免 touchpanel_kpd/touchpanel_pen 被误识别)
    if (lower.find("kpd") != std::string::npos ||
        lower.find("pen") != std::string::npos ||
        lower.find("key") != std::string::npos ||
        lower.find("button") != std::string::npos) {
      return false;
    }
    if (has_ev_abs && (lower.find("touch") != std::string::npos ||
                       lower.find("ts-") != std::string::npos ||
                       lower.find("tscreen") != std::string::npos ||
                       lower.find("focal") != std::string::npos ||
                       lower.find("goodix") != std::string::npos ||
                       lower.find("synaptics") != std::string::npos ||
                       lower.find("fts") != std::string::npos ||
                       lower.find("himax") != std::string::npos ||
                       lower.find("novatek") != std::string::npos ||
                       lower.find("ilitek") != std::string::npos)) {
      printf("[Touch] 回退检测2命中: 设备名='%s' + EV_ABS\n", name);
      return true;
    }
  }

  return false;
}
bool Init(const My_Vector2 &s, bool p_readOnly) {
  Close();
  devices.clear();
  My_Vector2 size = s;
  readOnly = p_readOnly;
  if (size.x > size.y) {
    screenSize = size;
  } else {
    screenSize = {size.y, size.x};
  }
  DIR *dir = opendir("/dev/input/");
  if (!dir) {
    return false;
  }
  dirent *ptr = NULL;
  int eventCount = 0;
  while ((ptr = readdir(dir)) != NULL) {
    if (strstr(ptr->d_name, "event"))
      eventCount++;
  }
  char temp[128];
  int scanned_count = 0;
  for (int i = 0; i <= eventCount; i++) {
    sprintf(temp, "/dev/input/event%d", i);
    // ★ v2.48: 添加 O_NONBLOCK, 配合 TypeA 线程的 poll() 超时机制
    // 原代码: 阻塞式 open, 导致 TypeA 线程 read() 永久阻塞 (安卓16卡死根因)
    int fd = open(temp, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
      fd = open(temp, O_RDONLY | O_NONBLOCK);
      if (fd < 0) continue;
    }
    scanned_count++;
    if (checkDeviceIsTouch(fd)) {
      Device device{};
      // ★ v2.48: 回退检测命中时 EVIOCGABS 可能失败, 用屏幕尺寸作默认值
      // 这样 TypeA 线程仍能启动, 若设备实际产生 POSITION 事件仍可处理
      if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &device.absX) != 0) {
        device.absX.maximum = (int)screenSize.x;
        device.absX.minimum = 0;
      }
      if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &device.absY) != 0) {
        device.absY.maximum = (int)screenSize.y;
        device.absY.minimum = 0;
      }
      device.fd = fd;
      if (!readOnly) {
        ioctl(fd, EVIOCGRAB, GRAB);
      }
      devices.push_back(device);
    } else {
      close(fd);
    }
  }
  if (devices.empty()) {
    printf("[Touch] 获取屏幕驱动失败 (扫描%d个设备, 安卓%d, SDK%d)\n",
           scanned_count, getAndroidVersion(), getAndroidSDKLevel());
    printf("[Touch] 诊断建议:\n");
    printf("[Touch]   1. 运行: getevent -p  (查看触摸设备是否报告 ABS_MT_POSITION_X/Y)\n");
    printf("[Touch]   2. 运行: ls -laZ /dev/input/event*  (查看SELinux标签)\n");
    printf("[Touch]   3. 运行: dmesg | grep denied  (查看SELinux拒绝日志)\n");
    fflush(stdout);
    return false;
  }
  printf("[Touch] 成功检测到 %zu 个触摸设备 (安卓%d)\n", devices.size(), getAndroidVersion());
  fflush(stdout);
  // LOGD("device count: %zu", devices.size());
  int screenX = devices[0].absX.maximum;
  int screenY = devices[0].absY.maximum;
  screenX_max = screenX;  // v2.42: 保存用于 Touch2Screen 方向判断
  screenY_max = screenY;
  screenX_min   = devices[0].absX.minimum;  // ★ v2.45: 处理非零最小值设备
  screenY_min   = devices[0].absY.minimum;
  screenX_range = screenX_max - screenX_min;
  screenY_range = screenY_max - screenY_min;
  if (!readOnly) {
    struct uinput_user_dev ui_dev;
    nowfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (nowfd <= 0) {
      return false;
    }
    int string_len = rand() % 10 + 5;
    char *string = new char[string_len + 1];
    memset(&ui_dev, 0, sizeof(ui_dev));
    genRandomString(string, string_len);
    strncpy(ui_dev.name, string, UINPUT_MAX_NAME_SIZE);
    ui_dev.id.bustype = 0;
    ui_dev.id.vendor = rand() % 10 + 5;
    ui_dev.id.product = rand() % 10 + 5;
    ui_dev.id.version = rand() % 10 + 5;
    ioctl(nowfd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    ioctl(nowfd, UI_SET_EVBIT, EV_ABS);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_X);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_Y);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(nowfd, UI_SET_EVBIT, EV_SYN);
    ioctl(nowfd, UI_SET_EVBIT, EV_KEY);
    ioctl(nowfd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
    ioctl(nowfd, UI_SET_KEYBIT, BTN_TOUCH);
    genRandomString(string, string_len);
    ioctl(nowfd, UI_SET_PHYS, string);
    delete[] string;
    int fd = devices[0].fd;
    {
      struct input_id id{};
      if (ioctl(fd, EVIOCGID, &id) == 0) {
        ui_dev.id.bustype = id.bustype;
        ui_dev.id.vendor = id.vendor;
        ui_dev.id.product = id.product;
        ui_dev.id.version = id.version;
      }
      uint8_t *bits = NULL;
      ssize_t bits_size = 0;
      int res, j, k;
      while (1) {
        res = ioctl(fd, EVIOCGBIT(EV_KEY, bits_size), bits);
        if (res < bits_size)
          break;
        bits_size = res + 16;
        bits = (uint8_t *)realloc(bits, bits_size * 2);
      }
      for (j = 0; j < res; j++) {
        for (k = 0; k < 8; k++)
          if (bits[j] & 1 << k) {
            if (j * 8 + k == BTN_TOUCH || j * 8 + k == BTN_TOOL_FINGER)
              continue;
            ioctl(nowfd, UI_SET_KEYBIT, j * 8 + k);
          }
      }
      free(bits);
    }
    ui_dev.absmin[ABS_MT_POSITION_X] = 0;
    ui_dev.absmax[ABS_MT_POSITION_X] = screenX;
    ui_dev.absmin[ABS_MT_POSITION_Y] = 0;
    ui_dev.absmax[ABS_MT_POSITION_Y] = screenY;
    ui_dev.absmin[ABS_X] = 0;
    ui_dev.absmax[ABS_X] = screenX;
    ui_dev.absmin[ABS_Y] = 0;
    ui_dev.absmax[ABS_Y] = screenY;
    ui_dev.absmin[ABS_MT_TRACKING_ID] = 0;
    ui_dev.absmax[ABS_MT_TRACKING_ID] = 65535;
    write(nowfd, &ui_dev, sizeof(ui_dev));
    if (ioctl(nowfd, UI_DEV_CREATE)) {
      return false;
    }
  }
  initialized = true;
  pthread_t t;
  for (int i = 0; i < devices.size(); i++) {
    devices[i].S2TX = (float)screenX / (float)devices[i].absX.maximum;
    devices[i].S2TY = (float)screenY / (float)devices[i].absY.maximum;
    pthread_create(&t, nullptr, TypeA, (void *)(long)i);
  }
  // ★ v2.45: touch_scale 根据 absX 对应长边/短边正确映射
  bool absX_is_long_ts = (screenX_max >= screenY_max);
  float ref_short = absX_is_long_ts ? (float)screenY_max : (float)screenX_max;
  float ref_long  = absX_is_long_ts ? (float)screenX_max : (float)screenY_max;
  if (size.x > size.y) std::swap(size.x, size.y);
  if (otherTouch) std::swap(size.x, size.y);
  touch_scale.x = ref_short / size.x;
  touch_scale.y = ref_long  / size.y;
  // system("chmod 000 -R /proc/bus/input/*");
  return true;
}
void Close() {
  if (initialized) {
    for (auto &device : devices) {
      if (!readOnly)
        ioctl(device.fd, EVIOCGRAB, UNGRAB);
      close(device.fd);
      device.fd = 0;
    }
    if (nowfd > 0) {
      ioctl(nowfd, UI_DEV_DESTROY);
      close(nowfd);
      nowfd = 0;
    }
    memset(input.event, 0, sizeof(input.event));
    initialized = false;
    devices.clear();
  }
}
void Down(float x, float y) {
  lock.lock();
  touchObj &touch = devices[0].Finger[9];
  touch.id = 19;
  touch.pos = My_Vector2(x, y) * touch_scale;
  touch.isDown = true;
  Upload();
  lock.unlock();
}
void Move(touchObj *touch, float x, float y) {
  lock.lock();
  touch->pos = My_Vector2(x, y) * touch_scale;
  Upload();
  lock.unlock();
}
void Move(float x, float y) { Down(x, y); }
void Up() {
  lock.lock();
  touchObj &touch = devices[0].Finger[9];
  touch.isDown = false;
  Upload();
  lock.unlock();
}
void SetCallBack(const std::function<void(std::vector<Device> *)> &cb) {
  callback = cb;
}
My_Vector2 Touch2Screen(const My_Vector2 &coord) {
  // ★ v2.45: 使用 (value-min)/range 归一化，处理非零最小值设备
  // coord.x ∈ [screenX_min, screenX_max], coord.y ∈ [screenY_min, screenY_max]
  float nx = (screenX_range > 0) ? ((coord.x - (float)screenX_min) / (float)screenX_range) : 0.0f;
  float ny = (screenY_range > 0) ? ((coord.y - (float)screenY_min) / (float)screenY_range) : 0.0f;

  // 判断 absX 对应物理长边还是短边
  bool absX_is_long = (screenX_max >= screenY_max);
  // 统一到物理坐标: xt=短边方向归一化, yt=长边方向归一化
  float xt_norm = absX_is_long ? ny : nx;
  float yt_norm = absX_is_long ? nx : ny;

  // screenSize = {长边, 短边}（始终）
  float longSide = screenSize.x;
  float shortSide = screenSize.y;
  // 还原原始范围
  float xt = xt_norm * shortSide;
  float yt = yt_norm * longSide;

  float x, y;
  if (otherTouch) {
    switch (orientation) {
    case 1:
      x = xt;
      y = yt;
      break;
    case 2:
      y = yt;
      x = shortSide - xt;
      break;
    case 3:
      x = shortSide - xt;
      y = longSide - yt;
      break;
    default:
      y = xt;
      x = shortSide - yt;
      break;
    }
  } else {
    switch (orientation) {
    case 1:
      x = yt;
      y = shortSide - xt;
      break;
    case 2:
      x = shortSide - xt;
      y = longSide - yt;
      break;
    case 3:
      y = xt;
      x = longSide - yt;
      break;
    default:
      x = xt;
      y = yt;
      break;
    }
  }
  return {x, y};
}
My_Vector2 GetScale() { return touch_scale; }
int GetFingerCount() {
    lock.lock();
    int count = 0;
    for (auto &dev : devices)
        for (auto &f : dev.Finger)
            if (f.isDown) count++;
    lock.unlock();
    return count;
}
bool GetFinger(int idx, float &outX, float &outY) {
    lock.lock();
    int n = 0;
    bool found = false;
    for (auto &dev : devices) {
        for (auto &f : dev.Finger) {
            if (f.isDown) {
                if (n == idx) {
                    outX = f.pos.x;
                    outY = f.pos.y;
                    found = true;
                    goto unlock;
                }
                n++;
            }
        }
    }
unlock:
    lock.unlock();
    return found;
}
void setOrientation(int o) { orientation = o; }
void setOtherTouch(bool p_otherTouch) { otherTouch = p_otherTouch; }
void UpdateScreenSize(const My_Vector2 &s) {
  lock.lock();
  My_Vector2 size = s;
  if (size.x > size.y) {
    screenSize = size;
  } else {
    screenSize = {size.y, size.x};
  }
  if (!devices.empty()) {
    bool absX_is_long_ts = (screenX_max >= screenY_max);
    float ref_short = absX_is_long_ts ? (float)screenY_max : (float)screenX_max;
    float ref_long  = absX_is_long_ts ? (float)screenX_max : (float)screenY_max;
    if (size.x > size.y) std::swap(size.x, size.y);
    if (otherTouch) std::swap(size.x, size.y);
    touch_scale.x = ref_short / size.x;
    touch_scale.y = ref_long  / size.y;
  }
  lock.unlock();
}
void Screen2Touch(float sx, float sy, int &out_raw_x, int &out_raw_y) {
  // ★ v2.43: Touch2Screen 的逆运算
  // 输入: 屏幕坐标 (sx, sy) — 当前方向，范围 [0, W] × [0, H]
  // 输出: 触摸驱动原始坐标 — 写入 /dev/input 的 ABS_MT_POSITION 值
  lock.lock();
  float longSide = screenSize.x;   // 物理长边
  float shortSide = screenSize.y;  // 物理短边
  float xt, yt;  // 物理统一坐标

  // 逆 orientation 变换
  if (otherTouch) {
    // Touch2Screen otherTouch=true 的逆
    switch (orientation) {
    case 1:
      xt = sx;  yt = sy;
      break;
    case 2:
      yt = sy;  xt = shortSide - sx;
      break;
    case 3:
      xt = shortSide - sx;  yt = longSide - sy;
      break;
    default:
      xt = sy;  yt = shortSide - sx;
      break;
    }
  } else {
    // Touch2Screen otherTouch=false 的逆
    switch (orientation) {
    case 1:
      yt = sx;  xt = shortSide - sy;
      break;
    case 2:
      xt = shortSide - sx;  yt = longSide - sy;
      break;
    case 3:
      xt = sy;  yt = longSide - sx;
      break;
    default:
      xt = sx;  yt = sy;
      break;
    }
  }

  // 归一化到 [0,1]
  float xt_norm = (shortSide > 0) ? (xt / shortSide) : 0.0f;
  float yt_norm = (longSide > 0) ? (yt / longSide) : 0.0f;

  // 逆物理统一: 判断 absX 对应长边还是短边
  bool absX_is_long = (screenX_max >= screenY_max);
  float nx, ny;
  if (absX_is_long) {
    nx = yt_norm;
    ny = xt_norm;
  } else {
    nx = xt_norm;
    ny = yt_norm;
  }

  // 还原到触摸驱动原始坐标 (★ v2.45: 使用 min+range)
  out_raw_x = (int)(nx * (float)screenX_range + (float)screenX_min);
  out_raw_y = (int)(ny * (float)screenY_range + (float)screenY_min);
  lock.unlock();
}
} // namespace Touch