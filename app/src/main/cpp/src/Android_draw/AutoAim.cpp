// ============================================================
//  AutoAim.cpp — 自瞄辅助实现
//  集成方式: 在 draw_Gui.cpp 末尾 #include "AutoAim.cpp"
//  这样可直接访问 draw_Gui.cpp 内的 static 函数与全局变量:
//    - optimizedWorldToScreen / getObjectCoordinates / isValidCoordinate
//    - SimulateClick / InitTouch / AddNotification
//    - matrix / data_buffers / displayInfo / Z / g_ui_density / GlobalMemory
//  参照 AutoWoodCheck 的实现路径
// ============================================================

#include "AutoAim.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "TouchHelperA.h"
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <cstring>
#include <algorithm>

// ---------- 全局状态定义 ----------
bool g_aim_enabled = false;
float g_aim_smoothing = 0.7f;      // ★ v2.49: 0.85→0.7 (系数0.3, 更激进的追踪, 减少延迟)
float g_aim_deadzone  = 5.0f;      // ★ v2.49: 15→5px (仅防1-2px抖动, 不再"稍偏就停")
float g_aim_max_dist  = 50.0f;     // ★ v2.49: 30→50米 (扩大追踪范围)
float g_aim_slide_pct_x = 0.8f;
float g_aim_slide_pct_y = 0.8f;
float g_aim_slide_x = 0.0f;
float g_aim_slide_y = 0.0f;
bool  g_show_aim_slide_point = true;
bool  g_show_aim_rect = false;
bool  g_show_aim_diag = false;
float g_aim_cooldown = 0.0f;
AimTarget g_aim_target;
SlideState g_aim_slide;

// 悬浮按钮内部状态
static ImVec2 g_aim_btn_pos{0, 0};
static bool   g_aim_btn_dragging = false;
static ImVec2 g_aim_btn_drag_offset;
static ImVec2 g_aim_btn_press_pos;

// ============================================================
//  内部辅助：滑动事件注入（直接写 /dev/input）
//  复用 SimulateClick 的校准与 Screen2Touch 逻辑，但拆为
//  Down/Move/Up 三阶段以支持跨帧持续滑动
// ============================================================

static int g_aim_tracking_id = -1;     // -1=已抬起, 1=已按下 (仅状态机标记)

// ★ v2.50: 注入路径改为 Touch::InjectAimTouch (直接写 nowfd/uinput)
//   旧路径: write(/dev/input/eventX) → TypeA 读到 → Upload() 提交
//   旧路径问题: 真实手指事件与自瞄事件混合在 TypeA 读取流, SYN_REPORT 时
//              Upload() 只提交当前 latest 对应 Finger, 自瞄 pointer 被遗漏.
//              真实手指落下时自瞄完全失效(用户反馈"手指放上去就不锁了").
//   新路径: 直接写 nowfd(uinput 虚拟设备), 绕过 TypeA/Upload.
//   uinput 已注册 ABS_MT_SLOT, 自瞄用 slot 2, 真实手指经 Upload 提交 slot 0,
//   在 Android InputReader 层合并为合法多指 MotionEvent, 物理隔离不冲突.
#define AIM_TID_BASE 1000
static int g_aim_kernel_tid = AIM_TID_BASE;

// 按下虚拟手指（开启滑动序列）
static bool aim_slide_down(float sx, float sy) {
    // tracking_id 自增: 每次 down 用新 id, 避免与真实手指低位 id 冲突
    g_aim_kernel_tid = (g_aim_kernel_tid < 100000) ? (g_aim_kernel_tid + 1) : AIM_TID_BASE;
    Touch::InjectAimTouch(sx, sy, g_aim_kernel_tid, true);
    g_aim_tracking_id = 1;   // 仅作"已按下"状态机标记
    g_aim_slide.cur_screen_x = sx;
    g_aim_slide.cur_screen_y = sy;
    return true;
}

// 移动虚拟手指（每帧增量）
static void aim_slide_move(float sx, float sy) {
    if (g_aim_tracking_id < 0) return;
    // move 时传当前 tid (不重新 down), is_down=true 保持按下并更新位置
    Touch::InjectAimTouch(sx, sy, g_aim_kernel_tid, true);
    g_aim_slide.cur_screen_x = sx;
    g_aim_slide.cur_screen_y = sy;
    g_aim_slide.last_move_time = ImGui::GetTime();
}

// 抬起虚拟手指（结束滑动序列）
static void aim_slide_up() {
    if (g_aim_tracking_id < 0) return;
    // is_down=false, tid 任意(内核会设 -1)
    Touch::InjectAimTouch(g_aim_slide.cur_screen_x, g_aim_slide.cur_screen_y, g_aim_kernel_tid, false);
    g_aim_tracking_id = -1;
}

static void aim_end_slide() {
    aim_slide_up();
    g_aim_slide.tracking_id = -1;
}

// ============================================================
//  目标选择：找最近监管者（阵营==1，非幽灵）
// ============================================================
static AimTarget aim_select_nearest_hunter(const std::vector<DataStruct>& data) {
    AimTarget best;
    best.distance = 9999.0f;
    for (const auto& it : data) {
        if (it.阵营 != 1 || it.is_ghost) continue;
        Vector3A pos = getObjectCoordinates(it.objcoor, false);
        if (!isValidCoordinate(pos)) continue;
        float distSq = FastMath::fastDistanceSquared(Z, pos);
        float distM = sqrtf(distSq) / 11.886f;
        if (distM > g_aim_max_dist) continue;
        if (distM < best.distance) {
            best.obj = it.obj;
            best.worldPos = pos;
            best.distance = distM;
            best.valid = true;
        }
    }
    return best;
}

// 粘滞判据：当前锁定目标是否仍有效
static bool aim_target_still_valid(const std::vector<DataStruct>& data) {
    if (g_aim_target.obj == 0) return false;
    for (const auto& it : data) {
        if (it.obj != g_aim_target.obj) continue;
        if (it.阵营 != 1 || it.is_ghost) return false;
        Vector3A pos = getObjectCoordinates(it.objcoor, false);
        if (!isValidCoordinate(pos)) return false;
        float dist = sqrtf(FastMath::fastDistanceSquared(Z, pos)) / 11.886f;
        if (dist > g_aim_max_dist) return false;
        g_aim_target.worldPos = pos;
        g_aim_target.distance = dist;
        return true;
    }
    return false;
}

// ============================================================
//  主循环入口
// ============================================================

void AutoAimCheck() {
    if (!g_aim_enabled) {
        aim_end_slide();
        g_aim_target.valid = false;
        g_aim_target.obj = 0;
        return;
    }

    // ★ v2.49: 移除"真实手指让权"机制
    //   旧逻辑: 检测到真实手指(摇杆/技能) → aim_slide_up 让权 → 自瞄停止
    //   这正是"走路时不瞄"的根因, 与用户需求"走路也要一直瞄"冲突.
    //   slot 2 已与真实摇杆 slot 0 物理隔离(TypeA 用 slot 值作 Finger[] 索引,
    //   Upload 把 Finger[0]+Finger[2] 通过 SYN_MT_REPORT 分隔同时提交给游戏),
    //   自瞄与摇杆可在不同 slot 并行工作, 无需让权.
    //   真实手指仅用于诊断显示, 不再干预自瞄.

    const auto& data = data_buffers[front_buffer_idx.load(std::memory_order_acquire)];
    if (GlobalMemory::自身 == 0) { aim_end_slide(); return; }

    // 目标选择 + 粘滞
    if (!aim_target_still_valid(data)) {
        g_aim_target = aim_select_nearest_hunter(data);
        if (!g_aim_target.valid) {
            aim_end_slide();
            return;
        }
    }

    // W2S 换算
    float sx, sy, sw;
    bool ok = optimizedWorldToScreen(g_aim_target.worldPos, matrix,
                                     displayInfo.width * 0.5f, displayInfo.height * 0.5f,
                                     sx, sy, sw);
    if (!ok) {
        g_aim_slide.w2s_fail_count++;
        // ★ v2.49: W2S 失败/目标出屏幕 → 不抬起, 不施加位移, 等待目标回屏
        //   旧逻辑 aim_slide_up 会导致目标回屏后需重新 down, 期间完全不修正.
        //   新逻辑: 手指保持在当前位置(不再 move), 视角不动, 目标回到屏幕内
        //   立即从当前位置继续追踪, 无重按间隙.
        //   连续失败 30 帧(~0.5秒)才彻底结束本次滑动(防永久占 slot).
        if (g_aim_slide.w2s_fail_count >= 30) aim_end_slide();
        return;
    }
    g_aim_slide.w2s_fail_count = 0;

    // 屏幕中心死区
    float cx = displayInfo.width * 0.5f;
    float cy = displayInfo.height * 0.5f;
    float dx = sx - cx;
    float dy = sy - cy;
    float dist = sqrtf(dx * dx + dy * dy);

    // ★ v2.49: 死区内不停止, 改为按距离衰减系数微调
    //   旧逻辑: dist < deadzone 直接 return → 目标稍偏中心就完全不修正
    //   用户反馈"稍微离开一点距离就不瞄了" 即此根因.
    //   新逻辑: 死区内 step 乘 (dist/deadzone) 衰减, 越接近中心修正越弱,
    //   既不停止追踪(持续锁敌), 又避免中心微抖(死区防抖本意).
    //   死区半径默认缩小到 5px(原15px), 仅防止 1-2 像素抖动.
    float deadzone_factor = 1.0f;
    if (dist < g_aim_deadzone) {
        deadzone_factor = dist / g_aim_deadzone;   // 0~1 线性衰减
    }

    // ★ v2.49: 漂移控制 - 改为软回中(本帧 step 叠加回中分量), 不再抬起
    //   旧逻辑: 漂移>80px 直接 aim_slide_up → 下一帧重按 → 持续追踪会反复抬起
    //   用户反馈"持续几秒后就不瞄了" 即此根因(频繁抬起期间不修正).
    //   新逻辑: 偏离起手点越远, 本帧 step 叠加越大的回中分量(向起手点拉),
    //   既持续追踪目标, 又缓慢回中防漂移, 无抬起间隙.
    //   回中分量方向 = 起手点 - 当前位置, 与追踪 step 叠加后限幅.
    float home_pull_x = 0.0f, home_pull_y = 0.0f;
    if (g_aim_tracking_id >= 0) {
        float home_dx = g_aim_slide_x - g_aim_slide.cur_screen_x;
        float home_dy = g_aim_slide_y - g_aim_slide.cur_screen_y;
        float drift = sqrtf(home_dx * home_dx + home_dy * home_dy);
        if (drift > 30.0f) {   // 偏离>30px 才开始软回中, 避免小幅修正被抵消
            // 回中强度 = 偏离量的 15%, 与追踪 step 同量级, 平滑叠加
            float pull_strength = std::min(0.15f, drift / 200.0f);
            home_pull_x = home_dx * pull_strength;
            home_pull_y = home_dy * pull_strength;
        }
    }

    // 增量位移：平滑度越大移动越慢（系数 = 1 - smoothing）
    // 叠加死区衰减系数(死区内弱化) + 软回中分量(防漂移)
    float step_x = dx * (1.0f - g_aim_smoothing) * deadzone_factor + home_pull_x;
    float step_y = dy * (1.0f - g_aim_smoothing) * deadzone_factor + home_pull_y;
    // 单帧限幅 ±12px（60fps下最大720px/秒，避免overshoot震荡）
    step_x = std::clamp(step_x, -12.0f, 12.0f);
    step_y = std::clamp(step_y, -12.0f, 12.0f);

    // 起手或增量移动
    if (g_aim_tracking_id < 0) {
        if (!aim_slide_down(g_aim_slide_x, g_aim_slide_y)) return;
        g_aim_slide.tracking_id = g_aim_tracking_id;
    }
    float new_x = std::clamp(g_aim_slide.cur_screen_x + step_x, 0.0f, (float)displayInfo.width);
    float new_y = std::clamp(g_aim_slide.cur_screen_y + step_y, 0.0f, (float)displayInfo.height);
    aim_slide_move(new_x, new_y);
}

// ============================================================
//  悬浮按钮（48x48 红/灰，可拖动，点击切换）
// ============================================================
void DrawAimFloatingButton() {
    const float size = 48.0f * g_ui_density;
    static bool initialized = false;
    if (!initialized) {
        g_aim_btn_pos = ImVec2(displayInfo.width * 0.1f, displayInfo.height * 0.4f);
        initialized = true;
    }

    ImGui::SetNextWindowSize(ImVec2(size, size));
    ImGui::SetNextWindowPos(g_aim_btn_pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));

    char name[32];
    snprintf(name, sizeof(name), "##AimBtn_%p", (void*)&g_aim_btn_pos);
    ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing);
    ImVec2 p0 = ImGui::GetWindowPos();
    ImVec2 p1(p0.x + size, p0.y + size);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 状态颜色：红=开启 / 灰=关闭
    ImU32 col = g_aim_enabled ? IM_COL32(220, 60, 60, 230)
                              : IM_COL32(120, 120, 120, 200);
    ImU32 ring = g_aim_enabled ? IM_COL32(255, 200, 200, 255)
                               : IM_COL32(200, 200, 200, 200);
    ImVec2 ctr(p0.x + size * 0.5f, p0.y + size * 0.5f);
    dl->AddCircleFilled(ctr, size * 0.45f, col, 24);
    dl->AddCircle(ctr, size * 0.45f, ring, 24, 2.0f * g_ui_density);
    // 文字
    const char* txt = "瞄";
    dl->AddText(ImVec2(p0.x + size * 0.35f, p0.y + size * 0.32f),
                IM_COL32(255, 255, 255, 255), txt);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // 交互：通过 ImGui io 判断按下/拖动/抬起
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse(io.MousePos.x, io.MousePos.y);
    bool in_btn = mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y;

    if (io.MouseDown[0]) {
        if (!g_aim_btn_dragging && in_btn) {
            g_aim_btn_dragging = true;
            g_aim_btn_press_pos = mouse;
            g_aim_btn_drag_offset = ImVec2(mouse.x - p0.x, mouse.y - p0.y);
        }
        if (g_aim_btn_dragging) {
            ImVec2 new_pos(mouse.x - g_aim_btn_drag_offset.x,
                           mouse.y - g_aim_btn_drag_offset.y);
            // 边界约束
            new_pos.x = std::clamp(new_pos.x, 0.0f, (float)displayInfo.width - size);
            new_pos.y = std::clamp(new_pos.y, 0.0f, (float)displayInfo.height - size);
            g_aim_btn_pos = new_pos;
        }
    } else {
        if (g_aim_btn_dragging) {
            // 抬起：位移 < 5px 视为点击切换
            float move_dist = sqrtf((mouse.x - g_aim_btn_press_pos.x) * (mouse.x - g_aim_btn_press_pos.x) +
                                    (mouse.y - g_aim_btn_press_pos.y) * (mouse.y - g_aim_btn_press_pos.y));
            if (move_dist < 5.0f) {
                bool new_state = !g_aim_enabled;
                g_aim_enabled = new_state;
                AddNotification(new_state ? "自瞄已开启" : "自瞄已关闭",
                                1.2f, new_state ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                                : ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
                if (!new_state) aim_end_slide();
            }
            g_aim_btn_dragging = false;
        }
    }
}

// ============================================================
//  起手点可视化指示器（暖金圆环，参照自动盖板 show_touch_point）
// ============================================================
void DrawAimSlidePointIndicator() {
    if (!g_show_aim_slide_point) return;
    ImVec2 center;
    bool moving = (g_aim_tracking_id >= 0);
    if (moving) {
        center = ImVec2(g_aim_slide.cur_screen_x, g_aim_slide.cur_screen_y);
    } else {
        center = ImVec2(g_aim_slide_x, g_aim_slide_y);
    }
    float now = ImGui::GetTime();
    float elapsed = now - g_aim_slide.last_move_time;
    bool animating = (moving && elapsed < 0.4f && g_aim_slide.last_move_time > 0);

    const ImU32 gold_fill   = IM_COL32(242, 199, 56, 35);
    const ImU32 gold_ring   = IM_COL32(242, 199, 56, 110);
    const ImU32 gold_center = IM_COL32(242, 199, 56, 210);
    const ImU32 gold_cross  = IM_COL32(242, 199, 56, 155);
    float dpi = g_ui_density;
    float base_r = 30.0f * dpi;

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddCircleFilled(center, base_r, gold_fill);
    fg->AddCircle(center, base_r, gold_ring, 32, 2.5f * dpi);

    if (animating) {
        float t = elapsed / 0.4f;
        float wave_r = base_r + t * 35.0f * dpi;
        int wave_a = (int)(180 * (1.0f - t * t));
        fg->AddCircle(center, wave_r, IM_COL32(242, 199, 56, wave_a), 32, 2.0f * dpi);
    }

    fg->AddCircleFilled(center, 4.0f * dpi, gold_center);
    // 十字
    fg->AddLine(ImVec2(center.x - base_r, center.y), ImVec2(center.x + base_r, center.y), gold_cross, 1.0f);
    fg->AddLine(ImVec2(center.x, center.y - base_r), ImVec2(center.x, center.y + base_r), gold_cross, 1.0f);

    // 死区可视化（屏幕中心圆，青色虚线圈）
    if (g_show_aim_rect) {
        ImVec2 screen_ctr(displayInfo.width * 0.5f, displayInfo.height * 0.5f);
        ImU32 dz_col = IM_COL32(0, 220, 255, 120);
        fg->AddCircle(screen_ctr, g_aim_deadzone, dz_col, 32, 1.5f * dpi);
        fg->AddCircleFilled(screen_ctr, 2.0f * dpi, IM_COL32(0, 220, 255, 180));
    }
}

// ============================================================
//  诊断面板
// ============================================================
void DrawAimDiagPanel() {
    if (!g_show_aim_diag) return;
    const auto& cd = data_buffers[front_buffer_idx.load(std::memory_order_acquire)];
    int hunter_cnt = 0;
    for (const auto& it : cd) {
        if (it.阵营 == 1 && !it.is_ghost) hunter_cnt++;
    }
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("自瞄诊断", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
    ImGui::Text("监管者:%d 目标:%s 距离:%.1fm", hunter_cnt,
                g_aim_target.valid ? "OK" : "无",
                g_aim_target.distance);
    ImGui::Text("滑动:%s W2S失败:%d 手指:(%.0f,%.0f)",
                g_aim_tracking_id >= 0 ? "ON" : "OFF",
                g_aim_slide.w2s_fail_count,
                g_aim_slide.cur_screen_x, g_aim_slide.cur_screen_y);
    ImGui::Text("平滑:%.2f 死区:%.0f 起手:(%.0f,%.0f)",
                g_aim_smoothing, g_aim_deadzone, g_aim_slide_x, g_aim_slide_y);
    ImGui::End();
}

// ============================================================
//  测试起手位置（按下→50ms→抬起，验证位置正确）
// ============================================================
void AimSlideTestClick() {
    if (!g_touch_ready) { if (!InitTouch()) return; }
    int tx = (int)g_aim_slide_x, ty = (int)g_aim_slide_y;
    SimulateClick(tx, ty);
    AddNotification("已测试起手位置", 1.0f, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
}

// ============================================================
//  配置加载/保存
// ============================================================
void LoadAimConfig(const std::unordered_map<std::string, std::string>& map) {
    auto getBool = [&](const std::string& key, bool& var) {
        auto it = map.find(key);
        if (it != map.end()) var = (it->second == "1");
    };
    auto getFloat = [&](const std::string& key, float& var) {
        auto it = map.find(key);
        if (it != map.end()) {
            try { var = std::stof(it->second); } catch (...) {}
        }
    };
    getBool("aim_enabled", g_aim_enabled);
    getFloat("aim_smoothing", g_aim_smoothing);
    getFloat("aim_deadzone", g_aim_deadzone);
    getFloat("aim_max_dist", g_aim_max_dist);
    getFloat("aim_slide_pct_x", g_aim_slide_pct_x);
    getFloat("aim_slide_pct_y", g_aim_slide_pct_y);
    getFloat("aim_slide_x", g_aim_slide_x);
    getFloat("aim_slide_y", g_aim_slide_y);
    getBool("show_aim_slide_point", g_show_aim_slide_point);
    getBool("show_aim_rect", g_show_aim_rect);
    getBool("show_aim_diag", g_show_aim_diag);

    // 启动时按百分比反算绝对坐标
    if (g_aim_slide_pct_x > 0.0f && g_aim_slide_pct_y > 0.0f) {
        g_aim_slide_x = g_aim_slide_pct_x * (float)displayInfo.width;
        g_aim_slide_y = g_aim_slide_pct_y * (float)displayInfo.height;
    } else if (g_aim_slide_x > 0.0f && g_aim_slide_y > 0.0f) {
        g_aim_slide_pct_x = g_aim_slide_x / (float)displayInfo.width;
        g_aim_slide_pct_y = g_aim_slide_y / (float)displayInfo.height;
    } else {
        g_aim_slide_pct_x = 0.8f;
        g_aim_slide_pct_y = 0.8f;
        g_aim_slide_x = g_aim_slide_pct_x * (float)displayInfo.width;
        g_aim_slide_y = g_aim_slide_pct_y * (float)displayInfo.height;
    }
}

void SaveAimConfig(std::ofstream& file) {
    file << "aim_enabled=" << (g_aim_enabled ? 1 : 0) << "\n";
    file << "aim_smoothing=" << g_aim_smoothing << "\n";
    file << "aim_deadzone=" << g_aim_deadzone << "\n";
    file << "aim_max_dist=" << g_aim_max_dist << "\n";
    file << "aim_slide_pct_x=" << g_aim_slide_pct_x << "\n";
    file << "aim_slide_pct_y=" << g_aim_slide_pct_y << "\n";
    file << "aim_slide_x=" << g_aim_slide_x << "\n";
    file << "aim_slide_y=" << g_aim_slide_y << "\n";
    file << "show_aim_slide_point=" << g_show_aim_slide_point << "\n";
    file << "show_aim_rect=" << g_show_aim_rect << "\n";
    file << "show_aim_diag=" << g_show_aim_diag << "\n";
}

void OnAimScreenSizeChanged() {
    if (g_aim_slide_pct_x > 0.0f && g_aim_slide_pct_y > 0.0f) {
        g_aim_slide_x = g_aim_slide_pct_x * (float)displayInfo.width;
        g_aim_slide_y = g_aim_slide_pct_y * (float)displayInfo.height;
    }
}
