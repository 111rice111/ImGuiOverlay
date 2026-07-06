#pragma once
#include "VectorStruct.h"
#include <functional>
#include <linux/input.h>
#include <vector>
namespace Touch {
struct touchObj {
  My_Vector2 pos{};
  int id = 0;
  bool isDown = false;
};
struct Device {
  int fd;
  float S2TX;
  float S2TY;
  input_absinfo absX, absY;
  touchObj Finger[10];
  Device() { memset((void *)this, 0, sizeof(*this)); }
};

// ★ Phase 1 底层重构: 触摸事件（由触摸线程产生，渲染线程消费）
// 解决旧代码中触摸线程直写 io.MousePos/io.MouseDown 导致的竞态:
//   - 快速点击在一帧内 down→up 被丢失 → 菜单点不动
//   - 多次翻转 MouseDown → IsMouseClicked 永不触发 → 拖拽启动失败
// 事件经无锁 SPSC 队列传递，渲染线程在 ImGui::NewFrame 前 drain，
// 调用 io.AddMousePosEvent / io.AddMouseButtonEvent 走官方事件队列。
struct FingerEvent {
  bool isDown;      // true=按下/移动, false=抬起
  float screenX;    // 已 Touch2Screen 转换的屏幕坐标
  float screenY;
};

bool Init(const My_Vector2 &s, bool p_readOnly);
void Close();
void Down(float x, float y);
void Move(float x, float y);
void Up();
void Move(touchObj *touch, float x, float y);
void Upload();
void SetCallBack(const std::function<void(std::vector<Device> *)> &cb);
My_Vector2 Touch2Screen(const My_Vector2 &coord);
My_Vector2 GetScale();
int GetFingerCount();
bool GetFinger(int idx, float &outX, float &outY);
void setOrientation(int orientation);
void setOtherTouch(bool p_otherTouch);
void UpdateScreenSize(const My_Vector2 &s);
// v2.43: 屏幕坐标→触摸驱动原始坐标（Touch2Screen 的逆运算）
// 用于 SimulateClick：把屏幕显示坐标转换为 /dev/input 的 ABS_MT_POSITION 值
void Screen2Touch(float sx, float sy, int &out_raw_x, int &out_raw_y);

// ★ Phase 1: 渲染线程在 ImGui::NewFrame 前调用
// 把触摸线程产生的 FingerEvent 灌入 ImGui 输入事件队列
// 调用点: main.cpp 主循环 drawBegin() 之后、graphics->NewFrame() 之前
void PumpEvents();

// ★ v2.50: 自瞄专用注入接口 — 直接写 uinput(nowfd), 绕过真实触摸屏
// 解决: 自瞄 write(/dev/input/eventX) + 真实手指事件混合在 TypeA 读取流中,
//       Upload() 的 SYN_MT_REPORT 顺序导致自瞄 pointer 被遗漏或 tracking_id 错乱,
//       真实手指落下时自瞄失效.
// 原理: nowfd 是 uinput 虚拟设备, Android InputReader 给它独立 deviceId,
//       自瞄在此注入 slot 2 与真实触摸屏(经 TypeA→Upload→nowfd)的事件
//       在 InputReader 层合并为合法多指 MotionEvent, 物理隔离不冲突.
// 参数: screen_x/y = 屏幕坐标, tid = tracking_id (自增), is_down = 按下/抬起
void InjectAimTouch(float screen_x, float screen_y, int tid, bool is_down);
} // namespace Touch
