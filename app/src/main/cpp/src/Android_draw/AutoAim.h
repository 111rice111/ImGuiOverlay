#pragma once

// ============================================================
//  AutoAim.h — 自瞄辅助模块（独立模块，最小侵入）
//  实现路径: 内核只读内存 → 监管者坐标+矩阵 → W2S → 触摸滑动注入
//  参照 AutoWoodCheck 的实现风格
// ============================================================

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include "Structs.h"  // Vector3A 定义在此

// ---------- 目标状态 ----------
struct AimTarget {
    uintptr_t obj = 0;          // 目标对象地址（粘滞判据）
    Vector3A  worldPos;         // 世界坐标
    float     distance = 0;     // 与自身距离（米）
    bool      valid = false;    // 本帧是否有效
};

// ---------- 滑动注入状态机 ----------
struct SlideState {
    int   tracking_id = -1;     // 当前手指 tracking_id（-1=已抬起）
    float cur_screen_x = 0;     // 虚拟手指当前屏幕坐标
    float cur_screen_y = 0;
    int   w2s_fail_count = 0;   // W2S 连续失败帧计数
    float last_move_time = 0;   // 上次滑动时间（用于动画）
};

// ---------- 全局状态（供 UI 与主循环共享） ----------
extern bool g_aim_enabled;                       // 悬浮按钮开关（UI与主循环同渲染线程，无需 atomic）
extern float g_aim_smoothing;                   // 平滑度 0.05~1.0, 默认 0.15
extern float g_aim_deadzone;                    // 屏幕中心死区(像素) 5~20, 默认 10
extern float g_aim_max_dist;                    // 最大追踪距离(米), 默认 30
extern float g_aim_slide_pct_x;                 // 起手位置百分比 X (默认 0.8)
extern float g_aim_slide_pct_y;                 // 起手位置百分比 Y (默认 0.8)
extern float g_aim_slide_x;                     // 起手位置绝对坐标 X（每帧按百分比反算）
extern float g_aim_slide_y;                     // 起手位置绝对坐标 Y
extern bool  g_show_aim_slide_point;            // 显示起手点指示器
extern bool  g_show_aim_rect;                   // 显示判定范围（屏幕中心死区圆）
extern bool  g_show_aim_diag;                   // 显示诊断面板
extern float g_aim_cooldown;                    // 当前冷却倒计时（显示用）
extern AimTarget g_aim_target;                  // 当前锁定目标
extern SlideState g_aim_slide;                  // 滑动状态机

// ---------- 接口 ----------
void AutoAimCheck();                            // 主循环入口（每帧由渲染线程调用）
void DrawAimFloatingButton();                   // 悬浮按钮渲染（48x48 红/灰，可拖动）
void DrawAimSlidePointIndicator();              // 起手点可视化指示器（暖金圆环）
void DrawAimDiagPanel();                        // 诊断面板
void AimSlideTestClick();                       // 测试起手位置（按下→抬起一次）
// 配置加载/保存：由 draw_Gui.cpp 的 LoadConfig/SaveConfig 内联调用
// LoadAimConfig 接收已解析的 key→value map，内部用自带的 getBool/getFloat lambda 风格
void LoadAimConfig(const std::unordered_map<std::string,std::string>& map);
void SaveAimConfig(std::ofstream& file);
void OnAimScreenSizeChanged();                  // 分辨率变化时按百分比重算起手坐标
