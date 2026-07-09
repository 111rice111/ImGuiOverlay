// draw_Touch.cpp - Part of draw_Gui split (v2.47)
// Extracted from draw_Gui.cpp: touch initialization, click simulation, auto wood check

#include "draw_Gui_internal.h"

bool InitTouch() {
    if (g_touch_ready) return true;
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDWR);
        if (fd < 0) continue;
        unsigned long abs_bits[ABS_MAX / (sizeof(unsigned long) * 8) + 1] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) {
            close(fd);
            continue;
        }
        auto check_bit = [&](int code) -> bool {
            return (abs_bits[code / (sizeof(unsigned long) * 8)] >> (code % (sizeof(unsigned long) * 8))) & 1;
        };
        if (!check_bit(ABS_MT_SLOT) || !check_bit(ABS_MT_TRACKING_ID) ||
            !check_bit(ABS_MT_POSITION_X) || !check_bit(ABS_MT_POSITION_Y)) {
            close(fd);
            continue;
        }
        struct input_absinfo abs_info;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_info) == 0) g_touch_max_x = abs_info.maximum;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_info) == 0) g_touch_max_y = abs_info.maximum;
        strncpy(g_touch_path, path, sizeof(g_touch_path));
        g_touch_ready = true;
        close(fd);
        return true;
    }
    return false;
}

void SimulateClick(int x, int y) {
    if (!g_touch_ready) {
        if (!InitTouch()) return;
    }
    // 应用校准
    float sx = x + wood_offset_x;
    float sy = y + wood_offset_y;
    int tx, ty;
    if (g_calib_done) {
        float dx = sx - g_calib_C;
        float dy = sy - g_calib_F;
        float det = g_calib_A * g_calib_E - g_calib_B * g_calib_D;
        if (fabsf(det) < 0.01f) { tx = (int)sx; ty = (int)sy; }
        else {
            tx = (int)(( g_calib_E * dx - g_calib_B * dy) / det);
            ty = (int)((-g_calib_D * dx + g_calib_A * dy) / det);
        }
    } else {
        tx = (int)sx; ty = (int)sy;
    }
    g_last_touch_x = sx;
    g_last_touch_y = sy;
    g_last_touch_time = ImGui::GetTime();
    int fd = open(g_touch_path, O_RDWR);
    if (fd < 0) return;
    // ★ v2.43: 用 Screen2Touch 替代手动转换
    // 原代码 raw_x = tx / displayInfo.width * g_touch_max_x 假设 absX=短边
    // 当 absX=长边或屏幕旋转时坐标轴错位 → 触摸点偏移
    int raw_x, raw_y;
    Touch::Screen2Touch((float)tx, (float)ty, raw_x, raw_y);
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS; ev.code = ABS_MT_SLOT; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_TRACKING_ID; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.type = EV_KEY; ev.code = BTN_TOOL_FINGER; ev.value = 1; write(fd, &ev, sizeof(ev));
    ev.code = BTN_TOUCH; ev.value = 1; write(fd, &ev, sizeof(ev));
    ev.type = EV_ABS; ev.code = ABS_MT_POSITION_X; ev.value = raw_x; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_POSITION_Y; ev.value = raw_y; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_TOUCH_MAJOR; ev.value = 10; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_PRESSURE; ev.value = 50; write(fd, &ev, sizeof(ev));
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0; write(fd, &ev, sizeof(ev));
    usleep(50000);
    ev.type = EV_ABS; ev.code = ABS_MT_SLOT; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_TRACKING_ID; ev.value = -1; write(fd, &ev, sizeof(ev));
    ev.type = EV_KEY; ev.code = BTN_TOOL_FINGER; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.code = BTN_TOUCH; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0; write(fd, &ev, sizeof(ev));
    close(fd);
}

void AutoWoodCheck() {
    if (!wood_enabled) return;
    const auto& current_data = data_buffers[front_buffer_idx.load(std::memory_order_acquire)];

    bool self_is_survivor = false;
    for (const auto& item : current_data) {
        if (item.obj == GlobalMemory::自身) {
            if (item.阵营 == 2) self_is_survivor = true;
            break;
        }
    }
    // 跳过 self_is_survivor 检查: GlobalMemory::自身 可能指向错误对象
    // if (!self_is_survivor) return;
    if (GlobalMemory::自身 == 0) { g_wood_viz_valid = false; return; }

    float minHunterDist = 9999.0f;
    const DataStruct* nearest_hunter = nullptr;
    Vector3A hunterPos;
    for (const auto& item : current_data) {
        if (item.阵营 != 1 || item.is_ghost) continue;
        if (IsFakeHunter_cached(item.类名)) continue;
        Vector3A pos = getObjectCoordinates(item.objcoor, false);
        if (!isValidCoordinate(pos)) continue;
        float dist = FastMath::fastDistanceSquared(Z, pos);
        if (dist < minHunterDist) {
            minHunterDist = dist;
            nearest_hunter = &item;
            hunterPos = pos;
        }
    }
    if (!nearest_hunter) { g_wood_viz_valid = false; return; }

    float minWoodDist = 9999.0f;
    const DataStruct* nearest_wood = nullptr;
    Vector3A woodPos;
    for (const auto& item : current_data) {
        if (item.sub_type != ObjSubClass::Pallet) continue;
        if (item.action == 131088 || item.action == 196624) continue;
        Vector3A pos = getObjectCoordinates(item.objcoor, false);
        if (!isValidCoordinate(pos)) continue;
        float dist = FastMath::fastDistanceSquared(Z, pos);
        if (dist < minWoodDist) {
            minWoodDist = dist;
            nearest_wood = &item;
            woodPos = pos;
        }
    }
    if (!nearest_wood) { g_wood_viz_valid = false; return; }

    float dx = getFloat(nearest_wood->objcoor + GAME_OFFSET(coord_yaw_cos, 0xB8));
    float dy = getFloat(nearest_wood->objcoor + GAME_OFFSET(coord_yaw_sin, 0xC0));
    float angle = atan2f(dy, dx);

    // ★ 帧间距离验证: 新位置与上一帧差距>500单位 → 拒绝(防悬空指针抽风)
    if (g_wood_viz_valid) {
        float jump = sqrtf((woodPos.X - g_wood_viz_pos_prev.X) * (woodPos.X - g_wood_viz_pos_prev.X) +
                          (woodPos.Y - g_wood_viz_pos_prev.Y) * (woodPos.Y - g_wood_viz_pos_prev.Y));
        if (jump > 500.0f) return;  // 正常木板不会瞬移, 跳过此帧
    }

    g_wood_viz_pos = woodPos;
    g_wood_viz_angle = angle;
    g_wood_viz_valid = true;
    g_wood_viz_pos_prev = woodPos;

    float x[4], y[4];
    x[0] = woodPos.X + (wood_length / 2) * cosf(angle) + (wood_width / 2) * cosf(angle + M_PI_2);
    y[0] = woodPos.Y + (wood_length / 2) * sinf(angle) + (wood_width / 2) * sinf(angle + M_PI_2);
    x[1] = woodPos.X + (wood_length / 2) * cosf(angle) - (wood_width / 2) * cosf(angle + M_PI_2);
    y[1] = woodPos.Y + (wood_length / 2) * sinf(angle) - (wood_width / 2) * sinf(angle + M_PI_2);
    x[2] = woodPos.X - (wood_length / 2) * cosf(angle) - (wood_width / 2) * cosf(angle + M_PI_2);
    y[2] = woodPos.Y - (wood_length / 2) * sinf(angle) - (wood_width / 2) * sinf(angle + M_PI_2);
    x[3] = woodPos.X - (wood_length / 2) * cosf(angle) + (wood_width / 2) * cosf(angle + M_PI_2);
    y[3] = woodPos.Y - (wood_length / 2) * sinf(angle) + (wood_width / 2) * sinf(angle + M_PI_2);

    float xmax = x[0], xmin = x[0], ymax = y[0], ymin = y[0];
    for (int i = 1; i < 4; i++) {
        if (x[i] > xmax) xmax = x[i];
        if (x[i] < xmin) xmin = x[i];
        if (y[i] > ymax) ymax = y[i];
        if (y[i] < ymin) ymin = y[i];
    }

    bool hunterInside = (hunterPos.X <= xmax && hunterPos.X >= xmin &&
                         hunterPos.Y <= ymax && hunterPos.Y >= ymin);
    float distToWood = sqrtf(FastMath::fastDistanceSquared(Z, woodPos)) / 11.886f;

    if (hunterInside && distToWood <= wood_trigger_dist) {
        // ★ 冷却检查: 距上次触发不足 cooldown 秒则跳过
        float now = ImGui::GetTime();
        if (now - g_last_wood_trigger_time < wood_cooldown_dur) {
            g_wood_cooldown = wood_cooldown_dur - (now - g_last_wood_trigger_time);
            return;
        }
        g_last_wood_trigger_time = now;
        g_wood_cooldown = 0.0f;
        g_wood_popup_time = now;  // ★ 触发弹窗计时
        // ★ DPI 缩放: 抖动幅度按屏幕密度缩放
        int jitter_range = (int)(50.0f * g_ui_density);
        int tx = (int)wood_touch_x + (rand() % (jitter_range * 2) - jitter_range);
        int ty = (int)wood_touch_y + (rand() % (jitter_range * 2) - jitter_range);
        SimulateClick(tx, ty);
        // 弹窗提醒（1.5秒冷却，不刷屏）
        if (now - g_last_touch_time > 1.5f) {
            g_last_touch_time = now;
            AddNotification("已触发盖板", 1.2f, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        }
        usleep(50000);
    } else {
        // 未触发时实时更新冷却倒计时显示
        g_wood_cooldown = fmaxf(0.0f, wood_cooldown_dur - (ImGui::GetTime() - g_last_wood_trigger_time));
    }
}
