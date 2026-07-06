#include "TouchHelperA.h"
#include "Utils.h"
#include "imgui.h"
#include "spinlock.h"
#include <atomic>
#include <cmath>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
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

// ★ Phase 1: 无锁单生产者单消费者环形缓冲区
// 生产者 = TypeA 触摸读取线程, 消费者 = 渲染线程 (PumpEvents)
// 容量 256 = power-of-2, 240Hz 触摸面板约 1 秒容量, 足够吸收渲染抖动
// Push 失败(满)时丢弃新事件 —— 渲染恢复后靠下一次 SYN_REPORT 修正状态
template <typename T, size_t Cap>
class SPSCRingBuffer {
  static_assert((Cap & (Cap - 1)) == 0, "Cap must be power of 2");
  T buf_[Cap];
  std::atomic<size_t> head_{0};  // 写入位（生产者）
  std::atomic<size_t> tail_{0};  // 读取位（消费者）
public:
  bool Push(const T &item) noexcept {
    size_t h = head_.load(std::memory_order_relaxed);
    size_t next = (h + 1) & (Cap - 1);
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;  // 满，丢弃新事件
    }
    buf_[h] = item;
    head_.store(next, std::memory_order_release);
    return true;
  }
  bool Pop(T &out) noexcept {
    size_t t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) {
      return false;  // 空
    }
    out = buf_[t];
    tail_.store((t + 1) & (Cap - 1), std::memory_order_release);
    return true;
  }
};

// 触摸事件队列（TypeA 线程 push, 渲染线程 drain）
static SPSCRingBuffer<FingerEvent, 256> g_eventQueue;
static struct {
  input_event downEvent[2]{{{}, EV_KEY, BTN_TOUCH, 1},
                           {{}, EV_KEY, BTN_TOOL_FINGER, 1}};
  input_event event[512]{0};
} input;
static My_Vector2 touch_scale;
static My_Vector2 screenSize;
static int screenX_max = 0;  // v2.42: 触摸驱动 absX.maximum（用于判断 absX 对应长/短边）
static int screenY_max = 0;  // v2.42: 触摸驱动 absY.maximum
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
  while (initialized) {
    auto readSize = (int32_t)read(device.fd, inputEvent, sizeof(inputEvent));
    if (readSize <= 0 || (readSize % sizeof(input_event)) != 0) {
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
        // ★ Phase 1: 不再直写 io.MousePos/io.MouseDown (竞态根因)
        // 改为 push FingerEvent 到无锁队列，由渲染线程 PumpEvents 消费
        // Touch2Screen 在 lock 内调用，安全读取 screenSize/orientation 等共享变量
        touchObj &f = device.Finger[latest];
        if (f.isDown) {
          auto pos = Touch2Screen(f.pos);
          g_eventQueue.Push({true, pos.x, pos.y});
        } else {
          g_eventQueue.Push({false, 0.0f, 0.0f});
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
static bool checkDeviceIsTouch(int fd) {
  uint8_t *bits = NULL;
  ssize_t bits_size = 0;
  int res, j, k;
  bool itmp = false, itmp2 = false, itmp3 = false;
  struct input_absinfo abs{};
  while (true) {
    res = ioctl(fd, EVIOCGBIT(EV_ABS, bits_size), bits);
    if (res < bits_size)
      break;
    bits_size = res + 16;
    bits = (uint8_t *)realloc(bits, bits_size * 2);
  }
  for (j = 0; j < res; j++) {
    for (k = 0; k < 8; k++)
      if (bits[j] & 1 << k && ioctl(fd, EVIOCGABS(j * 8 + k), &abs) == 0) {
        if (j * 8 + k == ABS_MT_SLOT) {
          itmp = true;
          continue;
        }
        if (j * 8 + k == ABS_MT_POSITION_X) {
          itmp2 = true;
          continue;
        }
        if (j * 8 + k == ABS_MT_POSITION_Y) {
          itmp3 = true;
          continue;
        }
      }
  }
  free(bits);
  return itmp && itmp2 && itmp3;
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
  for (int i = 0; i <= eventCount; i++) {
    sprintf(temp, "/dev/input/event%d", i);
    int fd = open(temp, O_RDWR);
    if (fd < 0) {
      continue;
    }
    if (checkDeviceIsTouch(fd)) {
      Device device{};
      if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &device.absX) == 0 &&
          ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &device.absY) == 0) {
        device.fd = fd;
        if (!readOnly) {
          ioctl(fd, EVIOCGRAB, GRAB);
        }
        devices.push_back(device);
      }
    } else {
      close(fd);
    }
  }
  if (devices.empty()) {
    puts("获取屏幕驱动失败");
    return false;
  }
  // LOGD("device count: %zu", devices.size());
  int screenX = devices[0].absX.maximum;
  int screenY = devices[0].absY.maximum;
  screenX_max = screenX;  // v2.42: 保存用于 Touch2Screen 方向判断
  screenY_max = screenY;
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
  if (size.x > size.y) {
    std::swap(size.x, size.y);
  }
  if (otherTouch) {
    std::swap(size.x, size.y);
  }
  touch_scale.x = (float)screenX / size.x;
  touch_scale.y = (float)screenY / size.y;
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
  // ★ v2.42: 使用归一化坐标，自动适配 absX/absY 对应物理长边还是短边
  // 原代码假设 absX=短边，当设备 absX=长边时坐标轴互换导致触摸点偏移
  // coord.x ∈ [0, screenX_max], coord.y ∈ [0, screenY_max]（经 S2TX/S2TY 统一）
  float nx = (screenX_max > 0) ? (coord.x / (float)screenX_max) : 0.0f;
  float ny = (screenY_max > 0) ? (coord.y / (float)screenY_max) : 0.0f;

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
    int screenX = devices[0].absX.maximum;
    int screenY = devices[0].absY.maximum;
    // ★ 修复根因 E: 旋转后部分设备 absX/absY.maximum 会变化
    // 旧代码只在 Init() 设置一次 screenX_max/screenY_max，旋转后 Touch2Screen
    // 归一化基准错误 → 触摸偏移。此处同步更新。
    screenX_max = screenX;
    screenY_max = screenY;
    if (size.x > size.y) std::swap(size.x, size.y);
    if (otherTouch) std::swap(size.x, size.y);
    touch_scale.x = (float)screenX / size.x;
    touch_scale.y = (float)screenY / size.y;
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

  // 还原到触摸驱动原始坐标
  out_raw_x = (int)(nx * (float)screenX_max);
  out_raw_y = (int)(ny * (float)screenY_max);
  lock.unlock();
}

// ★ Phase 1: 渲染线程在 ImGui::NewFrame 前调用
// drain 触摸事件队列，走 ImGui 官方事件 API
// ImGui 内部会把事件 push 到 g.InputEventsQueue，NewFrame() 统一消费
//   - AddMousePosEvent: 最后一个事件的位置生效
//   - AddMouseButtonEvent: 按状态翻转更新 MouseDown[]
// 即使一帧内 push 多个事件，ImGui 也能正确处理 down/up 时序
void PumpEvents() {
  if (!initialized) return;
  if (ImGui::GetCurrentContext() == nullptr) return;
  ImGuiIO &io = ImGui::GetIO();
  FingerEvent e;
  while (g_eventQueue.Pop(e)) {
    if (e.isDown) {
      io.AddMousePosEvent(e.screenX, e.screenY);
      io.AddMouseButtonEvent(0, true);
    } else {
      io.AddMouseButtonEvent(0, false);
    }
  }
}

// ★ v2.50: 自瞄专用注入 — 直接更新 devices[0].Finger[2] + 调用 Upload()
//   必须走盖板路径(Upload→nowfd), 直写 uinput 会被游戏反作弊过滤.
//   旧方案 write(/dev/input/eventX) 的缺陷:
//     自瞄事件与真实手指事件混合在真实触摸屏 evdev 缓冲,
//     真实手指落下时事件流密集, 自瞄事件被挤压/延迟读取,
//     Finger[2].pos 不更新 → Upload 提交旧位置 → 自瞄失效.
//   新方案: 直接在 lock 下更新 devices[0].Finger[2].pos/isDown/id,
//     然后调用 Upload() 提交到 nowfd (盖板路径, 不被过滤).
//     不依赖 evdev 缓冲, 真实手指落下时自瞄仍持续更新 Finger[2] 最新位置.
//     Upload 遍历所有 isDown 的 Finger, 同时提交 Finger[0](真实摇杆 slot0) +
//     Finger[2](自瞄 slot2), 游戏收到合法两指 MotionEvent, 互不冲突.
void InjectAimTouch(float screen_x, float screen_y, int tid, bool is_down) {
  if (!initialized || nowfd <= 0) return;
  if (devices.empty()) return;
  lock.lock();
  Device &dev = devices[0];
  touchObj &f = dev.Finger[2];   // ★ slot 2: 与真实手指 slot 0 隔离
  if (is_down) {
    f.isDown = true;
    f.pos.x = screen_x;
    f.pos.y = screen_y;
    f.id = tid;            // 自瞄 tid 从 1000 起, 与真实手指低位 id 隔离
  } else {
    f.isDown = false;
  }
  // ★ 走盖板路径: Upload 把所有 isDown 的 Finger (含真实手指+自瞄)
  //   通过 SYN_MT_REPORT 分隔, 一次 write(nowfd) 提交. 不被反作弊过滤.
  Upload();
  lock.unlock();
}
} // namespace Touch