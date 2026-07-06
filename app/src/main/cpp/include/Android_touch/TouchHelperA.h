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
} // namespace Touch
