// ============================================================
//  AutoAim.cpp — 自瞄辅助实现 (v2.54 完全重写: 直接 write eventX)
//  集成方式: 在 draw_Gui.cpp 末尾 #include "AutoAim.cpp"
//  可直接访问 draw_Gui.cpp 内的 static 函数与全局变量:
//    - optimizedWorldToScreen / getObjectCoordinates / isValidCoordinate
//    - SimulateClick / InitTouch / AddNotification / Touch::Screen2Touch
//    - matrix / data_buffers / displayInfo / Z / g_ui_density / GlobalMemory
//    - g_touch_path / g_touch_ready / g_touch_max_x / g_touch_max_y
// ============================================================
//
//  ★ v2.54 根本性重写 — 放弃 Touch 模块, 参照 SimulateClick 直接 write eventX
//
//    根本原因: main.cpp 中 Touch::Init(..., readOnly=true)
//      → 不创建 uinput(nowfd=0), 不 EVIOCGRAB
//      → TypeA 的 SYN_REPORT 处 `if (!readOnly)` 全部跳过
//      → SetCallBack 设置的 callback 永远不会被调用
//      → Upload() 永远不会被调用
//      → v2.52/v2.53 的 "在 TypeA 回调中执行" 方案完全无效
//
//    盖板 SimulateClick 有效的原因:
//      它完全独立于 Touch 模块, 直接 open(/dev/input/eventX) + write(EV_ABS/EV_SYN)
//      用 Type B 协议 (ABS_MT_SLOT + ABS_MT_TRACKING_ID + SYN_REPORT)
//      游戏直接 read eventX (因为没 EVIOCGRAB), 收到触摸事件
//
//    v2.54 方案: 完全模仿 SimulateClick 的写入方式
//      1. 用 g_touch_path + InitTouch() (draw_Gui.cpp 已有)
//      2. 用 Touch::Screen2Touch() 转换屏幕坐标 → 驱动原始坐标
//      3. 直接 write eventX, Type B 协议, SLOT=1 (避开真实摇杆 SLOT=0 和盖板 SLOT=0)
//      4. 三态: down(发完整事件) → move(只发位置) → up(只发 TRACKING_ID=-1)
//      5. 持久 fd (降低 open/close 开销), write 失败时重开
//      6. up 时不发 BTN_TOUCH=0 (避免误判真实手指抬起)
//
//    防干扰: 在 TouchHelperA.cpp TypeA 的 SYN_REPORT 处加过滤
//      `if (latest >= 1) continue;`  跳过自瞄 slot 的 ImGui 事件
//      (自瞄 SLOT=1 不应影响 ImGui 鼠标, 否则 UI 无法操作)
//
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
#include <atomic>
#include <chrono>

// ---------- 全局状态定义 ----------
bool g_aim_enabled = false;
float g_aim_smoothing = 0.7f;      // 平滑度: 越大越柔和(系数=1-smoothing)
float g_aim_deadzone  = 5.0f;      // 屏幕中心死区(像素), 仅防1-2px抖动
float g_aim_max_dist  = 50.0f;     // 最大追踪距离(米)
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

// 速度预测状态
static Vector3A g_aim_prev_world;          // 上一帧目标世界坐标
static Vector3A g_aim_velocity;            // 目标速度 (世界坐标/秒)
static double   g_aim_prev_time = 0;       // 上一帧时间戳

// ============================================================
//  ★ v2.56 核心: 持久 fd + 单次 write (原子性) + max_slots 诊断
//  - 持久 fd (降低 open/close 开销, write 失败时重开)
//  - 单次 write 提交完整事件序列 (内核不会拆散单个 write)
//  - 不发 BTN_TOUCH=0 (避免误判真实手指抬起)
//  - 诊断面板显示 max_slots (确认驱动是否支持多 slot)
// ============================================================
static int  g_aim_inject_fd   = -1;        // 持久 fd
static int  g_aim_slot        = 0;         // ★ v2.60: SLOT=0 (和 SimulateClick 一致, 游戏只认 slot 0)
static int  g_aim_tracking_id = 1000;      // 自瞄专用 tracking_id
static bool g_aim_finger_down = false;      // 自瞄虚拟手指是否按下
static int  g_aim_max_slots   = -1;        // 驱动 max_slots (-1=未检测)

// 检测驱动 max_slots (EVIOCGABS(ABS_MT_SLOT).maximum)
static int AimDetectMaxSlots() {
    if (!g_touch_ready) {
        if (!InitTouch()) return -1;
    }
    int fd = open(g_touch_path, O_RDWR);
    if (fd < 0) return -1;
    struct input_absinfo info;
    int max = -1;
    if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &info) == 0) {
        max = info.maximum;  // max_slots = absinfo.maximum + 1 (0..maximum)
    }
    close(fd);
    return max;
}

// 确保注入 fd 可用 (失败返回 false)
static bool AimEnsureFd() {
    if (g_aim_inject_fd >= 0) return true;
    if (!g_touch_ready) {
        if (!InitTouch()) return false;
    }
    g_aim_inject_fd = open(g_touch_path, O_RDWR);
    return g_aim_inject_fd >= 0;
}

// 单次 write 提交完整事件序列 (原子性)
static inline bool AimWriteBatch(const struct input_event* ev, int count) {
    if (g_aim_inject_fd < 0) return false;
    ssize_t expect = (ssize_t)(sizeof(struct input_event) * count);
    ssize_t r = write(g_aim_inject_fd, ev, expect);
    if (r != expect) {
        close(g_aim_inject_fd);
        g_aim_inject_fd = -1;
        return false;
    }
    return true;
}

// ============================================================
//  ★ v2.58: 持续按下方案 (回退到 v2.54 验证有效的方案)
//  v2.57 闪现式无效 (down→up 太快, 游戏当作 tap 不当作 drag)
//  回到持续按下: down 一次, 之后每帧 move 更新位置
//  真实手指落下时仍维持 slot 1 按下 (不停止)
// ============================================================

// 按下自瞄手指 (单次 write 完整事件序列)
static void AimInjectDown(float screen_x, float screen_y) {
    if (!AimEnsureFd()) return;
    int raw_x, raw_y;
    Touch::Screen2Touch(screen_x, screen_y, raw_x, raw_y);

    struct input_event ev[9];
    memset(ev, 0, sizeof(ev));
    int i = 0;
    ev[i].type = EV_ABS; ev[i].code = ABS_MT_SLOT;            ev[i].value = g_aim_slot;        i++;
    ev[i].type = EV_ABS; ev[i].code = ABS_MT_TRACKING_ID;      ev[i].value = g_aim_tracking_id; i++;
    ev[i].type = EV_ABS; ev[i].code = ABS_MT_POSITION_X;       ev[i].value = raw_x;             i++;
    ev[i].type = EV_ABS; ev[i].code = ABS_MT_POSITION_Y;       ev[i].value = raw_y;             i++;
    ev[i].type = EV_ABS; ev[i].code = ABS_MT_TOUCH_MAJOR;      ev[i].value = 10;                i++;
    ev[i].type = EV_ABS; ev[i].code = ABS_MT_PRESSURE;         ev[i].value = 50;                i++;
    ev[i].type = EV_KEY; ev[i].code = BTN_TOOL_FINGER;        ev[i].value = 1;                 i++;
    ev[i].type = EV_KEY; ev[i].code = BTN_TOUCH;               ev[i].value = 1;                 i++;
    ev[i].type = EV_SYN; ev[i].code = SYN_REPORT;              ev[i].value = 0;                 i++;
    AimWriteBatch(ev, i);

    g_aim_finger_down = true;
    g_aim_tracking_id++;
}

// 移动自瞄手指 (单次 write, SLOT+POSITION+SYN)
static void AimInjectMove(float screen_x, float screen_y) {
    if (!g_aim_finger_down || !AimEnsureFd()) return;
    int raw_x, raw_y;
    Touch::Screen2Touch(screen_x, screen_y, raw_x, raw_y);

    struct input_event ev[4];
    memset(ev, 0, sizeof(ev));
    ev[0].type = EV_ABS; ev[0].code = ABS_MT_SLOT;        ev[0].value = g_aim_slot;
    ev[1].type = EV_ABS; ev[1].code = ABS_MT_POSITION_X;  ev[1].value = raw_x;
    ev[2].type = EV_ABS; ev[2].code = ABS_MT_POSITION_Y;  ev[2].value = raw_y;
    ev[3].type = EV_SYN; ev[3].code = SYN_REPORT;          ev[3].value = 0;
    AimWriteBatch(ev, 4);
}

// 抬起自瞄手指 (单次 write, SLOT+TRACKING_ID=-1+SYN)
static void AimInjectUp() {
    if (!g_aim_finger_down) return;
    if (!AimEnsureFd()) { g_aim_finger_down = false; return; }

    struct input_event ev[3];
    memset(ev, 0, sizeof(ev));
    ev[0].type = EV_ABS; ev[0].code = ABS_MT_SLOT;         ev[0].value = g_aim_slot;
    ev[1].type = EV_ABS; ev[1].code = ABS_MT_TRACKING_ID;  ev[1].value = -1;
    ev[2].type = EV_SYN; ev[2].code = SYN_REPORT;          ev[2].value = 0;
    AimWriteBatch(ev, 3);

    g_aim_finger_down = false;
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

// 前向声明
void InitAimMaxSlots();

// ============================================================
//  渲染线程入口 (每帧由 draw_Gui.cpp 调用)
//  在渲染线程执行: 读目标 → W2S → 速度预测 → 增量滑动 → write eventX
//  ★ 不依赖 Touch 模块的 callback/Upload (因为 readOnly=true 它们不触发)
//  ★ 完全独立 write eventX, 参照 SimulateClick
// ============================================================
void AutoAimCheck() {
    // 启动时检测一次 max_slots (诊断用)
    InitAimMaxSlots();

    // 首次运行: 把 cur_screen 初始化为起手点
    static bool aim_slide_initialized = false;
    if (!aim_slide_initialized) {
        g_aim_slide.cur_screen_x = g_aim_slide_x;
        g_aim_slide.cur_screen_y = g_aim_slide_y;
        aim_slide_initialized = true;
    }

    if (!g_aim_enabled) {
        // 自瞄关闭: 抬起自瞄手指(如果按下)
        if (g_aim_finger_down) {
            AimInjectUp();
        }
        return;
    }

    // 自瞄开启
    if (GlobalMemory::自身 == 0) {
        if (g_aim_finger_down) AimInjectUp();
        return;
    }

    // matrix 有效性检查 (渲染线程通过 vm_readv 读取 matrix, 首次可能未读到)
    if (std::abs(matrix[0]) < 0.0001f && std::abs(matrix[1]) < 0.0001f) {
        if (g_aim_finger_down) AimInjectUp();
        return;
    }

    // ★ v2.60: 真实手指落下时让位 (slot 0 冲突避免)
    //   问题: 游戏只认 slot 0 的视角控制, 真实手指落在左下摇杆(也在 slot 0)
    //         自瞄和真实手指共用 slot 0 会冲突
    //   方案: 真实手指落下时, 自瞄抬起让位 (不干扰摇杆)
    //         真实手指抬起后, 自瞄立即重新 down (抢占 slot 0 视角控制)
    //   限制: 走路时(摇杆按下)自瞄暂停滑动, 摇杆抬起后恢复
    //         但摇杆抬起时角色停止移动 → 自瞄立即滑动视角
    //         用户需求是"持续瞄准", 但物理上 slot 0 冲突无法避免
    //         这是 v2.60 的折中方案, 先验证游戏是否认 slot 0
    static int prev_real_finger_count = 0;
    int real_finger_count = Touch::GetFingerCount();
    if (real_finger_count > 0) {
        // 有真实手指 → 让位 (抬起自瞄)
        if (g_aim_finger_down) {
            AimInjectUp();
        }
        prev_real_finger_count = real_finger_count;
        return;  // 真实手指在时不注入
    }
    // 真实手指全部抬起 → 如果之前有, 重新 down (抢占 slot 0)
    if (prev_real_finger_count > 0 && !g_aim_finger_down) {
        // 下面的逻辑会 AimInjectDown
    }
    prev_real_finger_count = real_finger_count;

    // 1. 目标选择 + 粘滞 (data_buffers 由数据线程更新, 原子读取)
    const auto& data = data_buffers[front_buffer_idx.load(std::memory_order_acquire)];
    if (!aim_target_still_valid(data)) {
        g_aim_target = aim_select_nearest_hunter(data);
        if (!g_aim_target.valid) {
            if (g_aim_finger_down) AimInjectUp();
            return;
        }
        // 新目标: 重置速度预测
        g_aim_prev_world = g_aim_target.worldPos;
        g_aim_velocity = {0, 0, 0};
        g_aim_prev_time = ImGui::GetTime();
    }

    // 2. 速度预测 (基于世界坐标变化)
    double now = ImGui::GetTime();
    float dt = (float)(now - g_aim_prev_time);
    if (dt > 0.001f && dt < 0.5f) {
        float dvx = g_aim_target.worldPos.X - g_aim_prev_world.X;
        float dvy = g_aim_target.worldPos.Y - g_aim_prev_world.Y;
        float dvz = g_aim_target.worldPos.Z - g_aim_prev_world.Z;
        // 速度 = 位移/时间, 限幅防异常跳变
        g_aim_velocity.X = std::clamp(dvx / dt, -500.0f, 500.0f);
        g_aim_velocity.Y = std::clamp(dvy / dt, -500.0f, 500.0f);
        g_aim_velocity.Z = std::clamp(dvz / dt, -500.0f, 500.0f);
    }
    g_aim_prev_world = g_aim_target.worldPos;
    g_aim_prev_time = now;

    // 3. 预测下一帧目标位置 (当前坐标 + 速度 * 预测时间)
    float predict_time = 0.016f;
    Vector3A predicted_pos;
    predicted_pos.X = g_aim_target.worldPos.X + g_aim_velocity.X * predict_time;
    predicted_pos.Y = g_aim_target.worldPos.Y + g_aim_velocity.Y * predict_time;
    predicted_pos.Z = g_aim_target.worldPos.Z + g_aim_velocity.Z * predict_time;

    // 4. W2S 换算 (用预测后的位置)
    float sx, sy, sw;
    bool ok = optimizedWorldToScreen(predicted_pos, matrix,
                                     displayInfo.width * 0.5f, displayInfo.height * 0.5f,
                                     sx, sy, sw);
    if (!ok) {
        // W2S 失败: 目标出屏幕, 连续失败30帧则抬起自瞄手指
        g_aim_slide.w2s_fail_count++;
        if (g_aim_slide.w2s_fail_count >= 30 && g_aim_finger_down) {
            AimInjectUp();
        }
        return;
    }
    g_aim_slide.w2s_fail_count = 0;

    // 5. 计算屏幕中心差值
    float cx = displayInfo.width * 0.5f;
    float cy = displayInfo.height * 0.5f;
    float dx = sx - cx;
    float dy = sy - cy;
    float dist = sqrtf(dx * dx + dy * dy);

    // 6. 死区衰减 (死区内不停止, 衰减系数使中心微调更柔和)
    float deadzone_factor = 1.0f;
    if (dist < g_aim_deadzone) {
        deadzone_factor = dist / g_aim_deadzone;
    }

    // 7. 计算滑动增量 (平滑 + 死区衰减)
    float step_x = dx * (1.0f - g_aim_smoothing) * deadzone_factor;
    float step_y = dy * (1.0f - g_aim_smoothing) * deadzone_factor;

    // 8. 软回中 (防漂移, 不抬起)
    if (g_aim_finger_down) {
        float home_dx = g_aim_slide_x - g_aim_slide.cur_screen_x;
        float home_dy = g_aim_slide_y - g_aim_slide.cur_screen_y;
        float drift = sqrtf(home_dx * home_dx + home_dy * home_dy);
        if (drift > 30.0f) {
            float pull_strength = std::min(0.15f, drift / 200.0f);
            step_x += home_dx * pull_strength;
            step_y += home_dy * pull_strength;
        }
    }

    // 9. 单帧限幅 ±12px (60fps下最大720px/秒, 避免overshoot)
    step_x = std::clamp(step_x, -12.0f, 12.0f);
    step_y = std::clamp(step_y, -12.0f, 12.0f);

    // 10. 更新虚拟手指位置
    g_aim_slide.cur_screen_x = std::clamp(g_aim_slide.cur_screen_x + step_x,
                                          0.0f, (float)displayInfo.width);
    g_aim_slide.cur_screen_y = std::clamp(g_aim_slide.cur_screen_y + step_y,
                                          0.0f, (float)displayInfo.height);
    g_aim_slide.last_move_time = now;

    // 11. ★ 持续按下注入: down 一次, 之后每帧 move 更新位置
    //    真实手指落下时仍维持 slot 1 按下 (不停止)
    if (!g_aim_finger_down) {
        AimInjectDown(g_aim_slide.cur_screen_x, g_aim_slide.cur_screen_y);
    } else {
        AimInjectMove(g_aim_slide.cur_screen_x, g_aim_slide.cur_screen_y);
    }
}

// 初始化: 检测 max_slots (启动时调用一次)
void InitAimMaxSlots() {
    if (g_aim_max_slots < 0) {
        g_aim_max_slots = AimDetectMaxSlots();
    }
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
                // 关闭自瞄时立即抬起手指
                if (!new_state && g_aim_finger_down) {
                    AimInjectUp();
                }
                AddNotification(new_state ? "自瞄已开启" : "自瞄已关闭",
                                1.2f, new_state ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                                : ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
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
    bool moving = g_aim_finger_down;
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
                g_aim_finger_down ? "ON" : "OFF",
                g_aim_slide.w2s_fail_count,
                g_aim_slide.cur_screen_x, g_aim_slide.cur_screen_y);
    ImGui::Text("速度:(%.0f,%.0f,%.0f) 平滑:%.2f",
                g_aim_velocity.X, g_aim_velocity.Y, g_aim_velocity.Z,
                g_aim_smoothing);
    ImGui::Text("注入fd:%d SLOT:%d tid:%d max_slots:%d", g_aim_inject_fd, g_aim_slot, g_aim_tracking_id, g_aim_max_slots);
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
    file << "show_aim_slide_point=" << (g_show_aim_slide_point ? 1 : 0) << "\n";
    file << "show_aim_rect=" << (g_show_aim_rect ? 1 : 0) << "\n";
    file << "show_aim_diag=" << (g_show_aim_diag ? 1 : 0) << "\n";
}

void OnAimScreenSizeChanged() {
    if (g_aim_slide_pct_x > 0.0f && g_aim_slide_pct_y > 0.0f) {
        g_aim_slide_x = g_aim_slide_pct_x * (float)displayInfo.width;
        g_aim_slide_y = g_aim_slide_pct_y * (float)displayInfo.height;
    }
}
