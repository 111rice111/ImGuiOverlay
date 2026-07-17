// ============================================================
// draw_LayoutUI.cpp — draw_Gui 拆分文件 (v2.47)
// 本文件从原 draw_Gui.cpp 中拆分而来，仅包含 Layout_tick_UI 函数。
// 所有共享声明见 draw_Gui_internal.h。
// ============================================================

#include "draw_Gui_internal.h"

void Layout_tick_UI(bool *main_thread_flag) {
    if (!ImGui::GetCurrentContext() || !g_font_ui || !g_font_ui->IsLoaded()) return;
    px = static_cast<float>(displayInfo.width) * 0.5f;
    py = static_cast<float>(displayInfo.height) * 0.5f;
    screen_config();
    drawBegin();

    // ★ 隐藏 ImGui 内部的隐式 Debug fallback 窗口（Debug##Default）
    // 该窗口由 ImGui::NewFrame() 自动创建，正常应自动隐藏。
    // 若某渲染路径意外触发 WriteAccessed 则会保持可见，在此强制关闭。
    {
        ImGuiContext& g = *GImGui;
        for (int i = 0; i < g.Windows.Size; i++) {
            ImGuiWindow* win = g.Windows[i];
            if (win && win->IsFallbackWindow) {
                win->Active = false;
                break;
            }
        }
    }

    SoHook::Update(g_current_game_pid);
    Draw_Main_Optimized(ImGui::GetForegroundDrawList());
    AutoWoodCheck();

    // 诊断: 显示板子/监管者检测状态
    if (g_show_wood_diag) {
        static int dbg_hunter_cnt = 0, dbg_wood_cnt = 0;
        const auto& cd = data_buffers[front_buffer_idx.load(std::memory_order_acquire)];
        dbg_hunter_cnt = 0; dbg_wood_cnt = 0;
        for (const auto& it : cd) {
            if (it.阵营 == 1 && !it.is_ghost) dbg_hunter_cnt++;
            if (it.sub_type == ObjSubClass::Pallet) dbg_wood_cnt++;
        }
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("木诊断", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        ImGui::Text("监管者:%d 板子:%d 判区:%s 冷却:%.1f", dbg_hunter_cnt, dbg_wood_cnt,
            g_wood_viz_valid ? "OK" : "无", g_wood_cooldown);
        ImGui::End();
    }

    // 判定范围可视化 — 赛博科技风格
    if (show_wood_rect && g_wood_viz_valid && wood_enabled) {
        // ★ 防数据竞态: 本地快照, 一次性读取全部变量
        Vector3A wvp = g_wood_viz_pos;
        float wva = g_wood_viz_angle;
        float ca = cosf(wva), sa = sinf(wva);
        float hw = wood_width * 0.5f, hl = wood_length * 0.5f;
        Vector3A wc[4] = {
            {wvp.X + hl*ca + hw*(-sa), wvp.Y + hl*sa + hw*ca, wvp.Z},
            {wvp.X + hl*ca - hw*(-sa), wvp.Y + hl*sa - hw*ca, wvp.Z},
            {wvp.X - hl*ca - hw*(-sa), wvp.Y - hl*sa - hw*ca, wvp.Z},
            {wvp.X - hl*ca + hw*(-sa), wvp.Y - hl*sa + hw*ca, wvp.Z}
        };
        ImVec2 sc[4]; int valid = 0; bool valid_proj[4] = {false,false,false,false};
        const float oob_limit = std::max(displayInfo.width, displayInfo.height) * 2.0f;
        bool any_oob = false;
        for (int k = 0; k < 4; k++) {
            float sx, sy, sw;
            if (optimizedWorldToScreen(wc[k], matrix, px, py, sx, sy, sw)) {
                // ★ 屏幕空间合法性检查: 拒绝超出屏幕 2 倍外的奇点投影
                if (fabsf(sx - px) > oob_limit || fabsf(sy - py) > oob_limit) { any_oob = true; continue; }
                sc[k] = ImVec2(sx, sy); valid_proj[k] = true; valid++;
            }
        }
        if (valid >= 3 && !any_oob) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 ctr = {(sc[0].x+sc[1].x+sc[2].x+sc[3].x)*0.25f, (sc[0].y+sc[1].y+sc[2].y+sc[3].y)*0.25f};

            // —— 内填: 深蓝紫半透明底 ——
            fg->AddQuadFilled(sc[0], sc[1], sc[2], sc[3], IM_COL32(20, 80, 180, 12));

            // —— 双层扫描线 (等距横线) ——
            float minY = sc[0].y, maxY = sc[0].y;
            for (int k = 1; k < 4; k++) { if (sc[k].y < minY) minY = sc[k].y; if (sc[k].y > maxY) maxY = sc[k].y; }
            static float scan_phase = 0; scan_phase += 0.04f; if (scan_phase > 1.0f) scan_phase -= 1.0f;
            float scan_spacing = 4.5f * g_ui_density;
            for (float sy = minY - scan_spacing + scan_phase * scan_spacing; sy < maxY + scan_spacing; sy += scan_spacing) {
                // 裁剪线: 在四个角之间插值
                // 简化: 用 AddLine 画横线穿过四边形, ImGui 不支持裁剪到四边形, 用粗线+淡alpha 近似
                fg->AddLine(ImVec2(sc[0].x - 5, sy), ImVec2(sc[2].x + 5, sy), IM_COL32(0, 200, 255, 12), 1.0f);
            }

            // —— 两层青蓝光晕 ——
            float glow_r[2] = {1.03f, 1.06f};
            int   glow_a[2] = {10, 4};
            for (int g = 0; g < 2; g++) {
                ImVec2 gl[4];
                for (int k = 0; k < 4; k++) { gl[k].x = ctr.x + (sc[k].x - ctr.x)*glow_r[g]; gl[k].y = ctr.y + (sc[k].y - ctr.y)*glow_r[g]; }
                fg->AddQuadFilled(gl[0], gl[1], gl[2], gl[3], IM_COL32(0, 180, 240, glow_a[g]));
            }

            // —— 四角 bracket (L 形角标) ——
            const ImU32 bracket_c = IM_COL32(0, 220, 255, 180);
            const float bk_len = std::min(std::min(fabsf(sc[1].x-sc[0].x), fabsf(sc[2].x-sc[3].x)),
                                          std::min(fabsf(sc[3].x-sc[0].x), fabsf(sc[2].x-sc[1].x))) * 0.22f;
            for (int k = 0; k < 4; k++) {
                int prev = (k+3)%4, next = (k+1)%4;
                float dx_n = (sc[next].x - sc[k].x), dy_n = (sc[next].y - sc[k].y);
                float dx_p = (sc[prev].x - sc[k].x), dy_p = (sc[prev].y - sc[k].y);
                float ln = sqrtf(dx_n*dx_n + dy_n*dy_n), lp = sqrtf(dx_p*dx_p + dy_p*dy_p);
                if (ln < 0.1f || lp < 0.1f) continue;
                dx_n /= ln; dy_n /= ln; dx_p /= lp; dy_p /= lp;
                float bl = std::min(bk_len, std::min(ln, lp) * 0.35f);
                ImVec2 cn(sc[k].x + dx_n*bl, sc[k].y + dy_n*bl);
                ImVec2 cp(sc[k].x + dx_p*bl, sc[k].y + dy_p*bl);
                fg->AddLine(sc[k], cn, bracket_c, 2.0f);
                fg->AddLine(sc[k], cp, bracket_c, 2.0f);
            }

            // —— 边框: 青蓝 1.2px ——
            fg->AddQuad(sc[0], sc[1], sc[2], sc[3], IM_COL32(0, 210, 255, 65), 1.2f);

            // —— 四角菱形亮点 ——
            for (int k = 0; k < 4; k++) {
                const float ds = 3.0f;
                fg->AddLine(ImVec2(sc[k].x-ds, sc[k].y), ImVec2(sc[k].x+ds, sc[k].y), IM_COL32(0, 255, 255, 100), 1.5f);
                fg->AddLine(ImVec2(sc[k].x, sc[k].y-ds), ImVec2(sc[k].x, sc[k].y+ds), IM_COL32(0, 255, 255, 100), 1.5f);
            }

            // —— 中心十字准星 ——
            const float cs = std::min(bk_len * 0.6f, 12.0f);
            fg->AddLine(ImVec2(ctr.x-cs, ctr.y), ImVec2(ctr.x+cs, ctr.y), IM_COL32(0, 255, 255, 50), 1.0f);
            fg->AddLine(ImVec2(ctr.x, ctr.y-cs), ImVec2(ctr.x, ctr.y+cs), IM_COL32(0, 255, 255, 50), 1.0f);
            fg->AddCircle(ctr, cs*0.5f, IM_COL32(0, 220, 255, 40), 16, 0.8f);
        }
    }

    // 延迟写 JSON：脏标记倒计时刷写
    if (g_dirty_flush_counter > 0) {
        g_dirty_flush_counter--;
        if (g_dirty_flush_counter <= 0) {
            FlushDirtyData();
        }
    }

    const float base = static_cast<float>(std::min(displayInfo.width, displayInfo.height));
    const float g_density = std::clamp(std::sqrt(base) * 0.0045f, 0.8f, 2.0f);
    g_ui_density = g_density;

    // ★ 分辨率变化检测: 折叠/旋转时按百分比重新计算触摸坐标
    if (g_last_display_w > 0 && (g_last_display_w != displayInfo.width || g_last_display_h != displayInfo.height)) {
        wood_touch_x = wood_touch_pct_x * (float)displayInfo.width;
        wood_touch_y = wood_touch_pct_y * (float)displayInfo.height;
    }
    g_last_display_w = displayInfo.width;
    g_last_display_h = displayInfo.height;

    static bool was_in_talent_view = false;
    if (g_talent_view) {
        if (!was_in_talent_view) g_talent_need_refresh = true;
        was_in_talent_view = true;
        show_talent_viewer();
    } else {
        was_in_talent_view = false;
    }

    static bool synced_threshold = false;
    if (!synced_threshold) {
        MjSubsystem::high_value_threshold = g_treasure_threshold;
        synced_threshold = true;
    }

    static char new_name[64] = "";
    static int new_map_number = 0;
    static float new_music_x = 0.0f, new_music_y = 0.0f, new_music_z = 0.0f;
    static float new_piano_x = 0.0f, new_piano_y = 0.0f, new_piano_z = 0.0f;
    static char new_texture[128] = MAPS_ROOT "";

    static float ui_anim_scale = 0.0f;
    const float anim_speed_fast = 0.15f;
    const float anim_speed_slow = 0.17f;
    float target_scale = MemuSwitch ? 1.0f : 0.0f;
    float animSpeed = (ui_anim_scale < target_scale) ? anim_speed_fast : anim_speed_slow;
    float deltaTimeScaled = ImGui::GetIO().DeltaTime / animSpeed;
    ui_anim_scale = ImLerp(ui_anim_scale, target_scale, deltaTimeScaled);

    // ★ 最小化横条动画 — 与主窗口反向
    float bar_target = MemuSwitch ? 0.0f : 1.0f;
    float bar_anim_speed = (g_minimized_bar_anim < bar_target) ? anim_speed_fast : anim_speed_slow;
    float bar_dt = ImGui::GetIO().DeltaTime / bar_anim_speed;
    g_minimized_bar_anim = ImLerp(g_minimized_bar_anim, bar_target, bar_dt);
    static bool theme_initialized = false;
    if (!theme_initialized) { InitModernUITheme(); theme_initialized = true; }
    LoadUITextures();
    StyleBackup style_bak = BackupImGuiStyle();
    ApplyModernUIStyle(g_density);

    if (ui_anim_scale > 0.01f) {
        ImDrawList *fg_draw_list = ImGui::GetForegroundDrawList();
        const char *watermark_text = "大米饭先生";
        float watermark_font_size = 45.0f * g_density;
        ImVec2 watermark_pos(80.0f, displayInfo.height - (watermark_font_size + 40.0f));
        fg_draw_list->AddText(g_font_ui, watermark_font_size, watermark_pos, IM_COL32(255, 50, 50, 100), watermark_text);

        const float win_w_final = std::min(base * 0.51f * 1.618f, displayInfo.width * 0.9f);
        const float win_h_final = std::min(win_w_final / 1.618f, displayInfo.height * 0.9f);
        float anim_win_w = win_w_final * ui_anim_scale;
        float anim_win_h = win_h_final * ui_anim_scale;
        ImVec2 anim_win_pos((displayInfo.width - anim_win_w) * 0.5f, (displayInfo.height - anim_win_h) * 0.5f);

        // 自定义窗口位置（支持拖动后保持）
        static ImVec2 g_custom_win_pos(0, 0);
        static bool g_custom_pos_set = false;
        ImVec2 final_win_pos = g_custom_pos_set ? g_custom_win_pos : anim_win_pos;

        ImGui::SetNextWindowPos(final_win_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(anim_win_w, anim_win_h), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(ui_anim_scale * 0.45f);  // ★ 更透明 — 参考图效果
        ImGui::Begin("大米饭先生", main_thread_flag, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

        // ★ 驱动状态: 显示当前驱动 + 已适配驱动列表
        if (g_drv) {
            ImGui::TextColored(g_theme.success, "[%s 驱动] fd=%d", g_drv->name(), g_drv->get_fd());
        } else {
            ImGui::TextColored(g_theme.danger, "[!] 驱动未加载");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        auto drv_list = list_available_drivers();
        for (size_t i = 0; i < drv_list.size(); i++) {
            if (i > 0) { ImGui::SameLine(); ImGui::TextDisabled(","); ImGui::SameLine(); }
            ImVec4 dim = {0.5f, 0.5f, 0.5f, 0.5f};
            ImGui::TextColored(drv_list[i].available ? g_theme.success : dim,
                "%s", drv_list[i].name.c_str());
        }
        ImGui::TextDisabled("(已适配驱动)");
        const ImVec2 window_pos2 = ImGui::GetWindowPos();
        const ImVec2 window_size = ImGui::GetWindowSize();
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const float titlebar_height = ImGui::GetTextLineHeight() * 2.4f;
        const float window_rounding = 48.0f * g_density;

        // === v2.46 窗口圆角背景: 暖金色宣纸主题 ===
        draw_list->AddRectFilled(window_pos2, ImVec2(window_pos2.x + window_size.x, window_pos2.y + window_size.y),
                                 IM_COL32(252, 243, 220, 238), window_rounding);
        draw_list->AddRect(
            window_pos2, ImVec2(window_pos2.x + window_size.x, window_pos2.y + window_size.y),
            IM_COL32(220, 185, 110, 100), window_rounding, 0, 1.5f * g_density);

        // === v2.46 UI底图: contain模式完整显示人物，底部对齐，顶部羽化融合暖金色 ===
        if (g_tex_ui_bg != 0 && g_tex_ui_bg_w > 0 && g_tex_ui_bg_h > 0) {
            float win_w = window_size.x;
            float win_h = window_size.y;
            float img_w = (float)g_tex_ui_bg_w;
            float img_h = (float)g_tex_ui_bg_h;
            float scale = fminf(win_w / img_w, win_h / img_h);
            float draw_w = img_w * scale;
            float draw_h = img_h * scale;
            float x0 = window_pos2.x + (win_w - draw_w) * 0.5f;
            float y0 = window_pos2.y + win_h - draw_h;
            float x1 = x0 + draw_w;
            float y1 = y0 + draw_h;

            draw_list->AddImageRounded(g_tex_ui_bg, ImVec2(x0, y0), ImVec2(x1, y1),
                                       ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                                       IM_COL32(255, 243, 218, 165), window_rounding,
                                       ImDrawFlags_RoundCornersBottom);

            float feather_h = win_h * 0.35f;
            int steps = (int)(feather_h / 2.0f);
            for (int i = 0; i < steps; i++) {
                float t = (float)i / (float)steps;
                float yy0 = y0 - feather_h * (1.0f - t);
                float yy1 = y0 - feather_h * (1.0f - (t + 1.0f / (float)steps));
                if (yy0 < window_pos2.y) yy0 = window_pos2.y;
                if (yy1 > y0 + 2.0f) break;
                ImU8 alpha = (ImU8)(238 * (1.0f - t) * (1.0f - t));
                draw_list->AddRectFilled(ImVec2(window_pos2.x, yy0), ImVec2(window_pos2.x + window_size.x, yy1),
                                         IM_COL32(252, 243, 220, alpha));
            }
        }

        // === v2.46 左上角圆形头像 ===
        float avatar_size = titlebar_height * 0.82f;
        float avatar_pad = ImGui::GetStyle().WindowPadding.x * 0.6f;
        float avatar_right_x = window_pos2.x;
        if (g_tex_ui_avatar != 0 && g_tex_ui_avatar_w > 0 && g_tex_ui_avatar_h > 0) {
            ImVec2 avatar_center(window_pos2.x + avatar_pad + avatar_size * 0.5f,
                                 window_pos2.y + titlebar_height * 0.5f);
            float avatar_r = avatar_size * 0.5f;
            ImVec2 av_p0(avatar_center.x - avatar_r, avatar_center.y - avatar_r);
            ImVec2 av_p1(avatar_center.x + avatar_r, avatar_center.y + avatar_r);
            draw_list->AddImageRounded(g_tex_ui_avatar, av_p0, av_p1, ImVec2(0,0), ImVec2(1,1),
                                       IM_COL32(255,255,255,255), avatar_r, ImDrawFlags_RoundCornersAll);
            draw_list->AddCircle(avatar_center, avatar_r + 2.0f * g_density, IM_COL32(210, 170, 60, 180), 32, 2.0f * g_density);
            avatar_right_x = window_pos2.x + avatar_pad + avatar_size + 12.0f * g_density;
        }

        // 现代标题栏：暖金宣纸半透明 — 参考图风格
        draw_list->AddRectFilledMultiColor(window_pos2, ImVec2(window_pos2.x + window_size.x, window_pos2.y + titlebar_height),
                                           IM_COL32(255, 250, 235, 140), IM_COL32(252, 243, 220, 130), IM_COL32(248, 238, 215, 135), IM_COL32(254, 248, 232, 140));
        draw_list->AddRectFilled(ImVec2(window_pos2.x, window_pos2.y + titlebar_height),
                                ImVec2(window_pos2.x + window_size.x, window_pos2.y + titlebar_height + 3.0f * g_density),
                                IM_COL32(230, 200, 150, 50), 0.0f);
        draw_list->AddLine(ImVec2(window_pos2.x + 20.0f * g_density, window_pos2.y + titlebar_height),
                           ImVec2(window_pos2.x + window_size.x - 20.0f * g_density, window_pos2.y + titlebar_height),
                           IM_COL32(230, 195, 140, 80), 1.5f * g_density);

        // ★ 标题 "大米饭先生" — 金色 + 白色描边效果
        const char *title = "大米饭先生";
        ImGui::PushFont(g_font_ui);
        const ImVec2 text_size_title = ImGui::CalcTextSize(title);
        float title_area_w = window_pos2.x + window_size.x - avatar_right_x;
        ImVec2 title_text_pos(avatar_right_x + (title_area_w - text_size_title.x) * 0.5f, window_pos2.y + (titlebar_height - text_size_title.y) * 0.5f);
        float title_font = ImGui::GetFontSize() * 1.20f;
        const float outline_off = 1.5f * g_density;
        // 白色描边 — 四个方向偏移绘制白色，形成轮廓
        draw_list->AddText(g_font_ui, title_font, ImVec2(title_text_pos.x - outline_off, title_text_pos.y),              IM_COL32(255,255,255,220), title);
        draw_list->AddText(g_font_ui, title_font, ImVec2(title_text_pos.x + outline_off, title_text_pos.y),              IM_COL32(255,255,255,220), title);
        draw_list->AddText(g_font_ui, title_font, ImVec2(title_text_pos.x, title_text_pos.y - outline_off),              IM_COL32(255,255,255,220), title);
        draw_list->AddText(g_font_ui, title_font, ImVec2(title_text_pos.x, title_text_pos.y + outline_off),              IM_COL32(255,255,255,220), title);
        draw_list->AddText(g_font_ui, title_font, ImVec2(title_text_pos.x - outline_off, title_text_pos.y - outline_off),IM_COL32(255,255,255,200), title);
        draw_list->AddText(g_font_ui, title_font, ImVec2(title_text_pos.x + outline_off, title_text_pos.y - outline_off),IM_COL32(255,255,255,200), title);
        draw_list->AddText(g_font_ui, title_font, ImVec2(title_text_pos.x - outline_off, title_text_pos.y + outline_off),IM_COL32(255,255,255,200), title);
        draw_list->AddText(g_font_ui, title_font, ImVec2(title_text_pos.x + outline_off, title_text_pos.y + outline_off),IM_COL32(255,255,255,200), title);
        // 主体 — 金色字盖在上面
        draw_list->AddText(g_font_ui, title_font, title_text_pos, IM_COL32(235, 180, 40, 255), title);
        ImGui::PopFont();

        // ========== 标题栏: 点击折叠 / 拖拽移动 ==========
        const ImVec2 mouse_pos_win = ImGui::GetMousePos();
        bool hit_titlebar = (mouse_pos_win.x >= window_pos2.x &&
                             mouse_pos_win.x <= window_pos2.x + window_size.x &&
                             mouse_pos_win.y >= window_pos2.y &&
                             mouse_pos_win.y <= window_pos2.y + titlebar_height);

        static bool title_drag_pending = false;
        static bool title_dragging = false;
        static ImVec2 title_drag_off;
        static float title_hold_time = 0;
        static ImVec2 title_press_pos;
        static ImVec2 title_drag_start_offset;

        if (ImGui::IsMouseClicked(0) && hit_titlebar) {
            title_drag_pending = true;
            title_dragging = false;
            title_hold_time = 0;
            title_press_pos = mouse_pos_win;
            title_drag_start_offset = ImVec2(mouse_pos_win.x - window_pos2.x, mouse_pos_win.y - window_pos2.y);
        }

        if (title_drag_pending) {
            title_hold_time += ImGui::GetIO().DeltaTime;
            float drag_dist = sqrtf(powf(mouse_pos_win.x - title_press_pos.x, 2) +
                                    powf(mouse_pos_win.y - title_press_pos.y, 2));
            if (drag_dist > 12.0f) {
                title_dragging = true;
                g_custom_pos_set = true;
                title_drag_pending = false;
            }
            if (!ImGui::IsMouseDown(0)) {
                // 短按释放 = 点击 → 折叠/展开
                if (!title_dragging && title_hold_time < 0.35f && drag_dist < 10.0f) {
                    MemuSwitch = !MemuSwitch;
                }
                title_drag_pending = false;
                title_dragging = false;
            }
        }

        if (title_dragging) {
            if (ImGui::IsMouseDown(0)) {
                ImVec2 new_pos = ImVec2(mouse_pos_win.x - title_drag_start_offset.x,
                                        mouse_pos_win.y - title_drag_start_offset.y);
                new_pos.x = std::clamp(new_pos.x, 0.0f, displayInfo.width - window_size.x);
                new_pos.y = std::clamp(new_pos.y, 0.0f, displayInfo.height - window_size.y);
                g_custom_win_pos = new_pos;
                ImGui::SetWindowPos(new_pos);
            } else {
                title_dragging = false;
            }
        }
        struct NavItem { const char *label; const char *icon; };
        static const NavItem nav_items[] = {
                {"状态信息", "\xee\xa2\x80"}, {"普通对局", "\xee\xa4\x82"},
                {"自动盖板", "\xee\xa5\x85"}, {"模仿者",   "\xee\xa6\x83"},
                {"摸金模式", "\xee\xa8\x84"},
                {"地图管理", "\xee\xa9\x85"},
                //{"数据管理", "\xee\xa3\x91"},  // 已注释，需要时取消注释
                //{"调试信息", "\xee\xa7\x81"},  // 已注释，需要时取消注释
                {"关于",     "\xee\xa7\x81"},
        };
        static int current_tab = 0;
        // 限制 current_tab 不超过最大索引
        if (current_tab >= (int)(sizeof(nav_items)/sizeof(nav_items[0])))
            current_tab = 0;

        float max_text_width = 0.0f;
        for (const auto& item : nav_items) {
            float w = ImGui::CalcTextSize(item.label).x;
            if (w > max_text_width) max_text_width = w;
        }
        const float btn_inner_padding = ImGui::GetStyle().FramePadding.x * 2.0f;
        const float indicator_space = 12.0f * g_density;
        const float sidebar_width = max_text_width + btn_inner_padding + indicator_space + 18.0f * g_density;
        const float content_x = sidebar_width + 12.0f * g_density;
        const float nav_start_y = titlebar_height + 16.0f * g_density;

        // 侧边栏 — 暖金宣纸半透明
        ImVec2 sidebar_pos(window_pos2.x + 10.0f * g_density, window_pos2.y + nav_start_y);
        ImVec2 sidebar_size(sidebar_width - 10.0f * g_density, window_size.y - nav_start_y - 20.0f * g_density);
        draw_list->AddRectFilled(sidebar_pos, ImVec2(sidebar_pos.x + sidebar_size.x, sidebar_pos.y + sidebar_size.y), IM_COL32(255, 250, 238, 85), 18.0f * g_density);
        draw_list->AddRect(sidebar_pos, ImVec2(sidebar_pos.x + sidebar_size.x, sidebar_pos.y + sidebar_size.y), IM_COL32(225, 200, 160, 45), 18.0f * g_density, 0, 1.0f);

        ImGui::SetCursorPos(ImVec2(8.0f * g_density, nav_start_y));
        ImGui::BeginChild("##Sidebar", ImVec2(sidebar_width, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * g_density);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 8.0f * g_density));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * g_density, 10.0f * g_density));
            for (int i = 0; i < IM_ARRAYSIZE(nav_items); i++) {
                bool is_active = (current_tab == i);
                ImVec4 bg_col = is_active ? g_theme.primary : g_theme.bg_card;
                ImVec4 hover_col = is_active ? g_theme.primary_hover : g_theme.bg_hover;
                ImVec4 active_col = is_active ? g_theme.primary_active : g_theme.bg_active;
                ImVec4 text_col = is_active ? g_theme.text_on_primary : g_theme.text;

                ImGui::PushStyleColor(ImGuiCol_Button, bg_col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover_col);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, active_col);
                ImGui::PushStyleColor(ImGuiCol_Text, text_col);
                if (ImGui::Button(nav_items[i].label, ImVec2(-1, 0))) current_tab = i;
                ImGui::PopStyleColor(4);

                if (is_active) {
                    ImVec2 btn_min = ImGui::GetItemRectMin();
                    ImVec2 btn_max = ImGui::GetItemRectMax();
                    float line_h = (btn_max.y - btn_min.y) * 0.55f;
                    float line_x = btn_min.x - 4.0f * g_density;
                    float line_y = btn_min.y + (btn_max.y - btn_min.y - line_h) * 0.5f;
                    ImGui::GetWindowDrawList()->AddRectFilled(
                            ImVec2(line_x, line_y),
                            ImVec2(line_x + 4.5f * g_density, line_y + line_h),
                            IM_COL32(220, 170, 50, 255), 2.5f * g_density);
                }
            }
            ImGui::PopStyleVar(3);
            ImGui::Dummy(ImVec2(0, 18.0f * g_density));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * g_density);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * g_density, 10.0f * g_density));
            ImGui::PushStyleColor(ImGuiCol_Button, g_theme.danger);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_theme.danger_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_theme.danger_active);
            ImGui::PushStyleColor(ImGuiCol_Text, g_theme.text_on_primary);
            if (ImGui::Button("退出脚本", ImVec2(-1, 0))) {
                *main_thread_flag = false;
                CloseDebugLog();
                SaveConfig();
            }
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
        }
        ImGui::EndChild();

        // 主内容区：暖金宣纸半透明 — 参考图风格
        ImGui::SetCursorPos(ImVec2(content_x, nav_start_y));
        ImGui::BeginChild("MainContent", ImVec2(window_size.x - content_x - 14.0f * g_density, window_size.y - nav_start_y - 20.0f * g_density), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImDrawList *child_draw = ImGui::GetWindowDrawList();
        ImVec2 child_pos = ImGui::GetWindowPos();
        ImVec2 child_size = ImGui::GetWindowSize();
        child_draw->AddRectFilled(child_pos, ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y), IM_COL32(255, 252, 242, 75), 20.0f * g_density);
        child_draw->AddRect(child_pos, ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y), IM_COL32(228, 205, 170, 42), 20.0f * g_density, 0, 1.0f);
        child_draw->AddRectFilled(ImVec2(child_pos.x, child_pos.y), ImVec2(child_pos.x + child_size.x, child_pos.y + 3.0f * g_density), IM_COL32(235, 210, 175, 40), 20.0f * g_density);

        switch (current_tab) {
            case 0:
                // ★ 状态信息 — 精简版：仅保留数据状态 + 游戏信息
                StyledSectionHeader("数据状态", g_theme.text_title, g_density);
                if (GlobalMemory::状态 == 2) ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "[OK] 数据已就绪");
                else if (GlobalMemory::状态 == 1) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), ":) 正在初始化...");
                else ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[X] 未获取数据");
                ImGui::Spacing();
                StyledSectionHeader("游戏信息", g_theme.text_title, g_density);
                ImGui::Text("进程 ID: %d", pid.load()); ImGui::SameLine();
                ImGui::Text("包名: %s", extractedString);
                /*
                // ★ 已隐藏，需要时取消注释
                StyledSectionHeader("系统信息", g_theme.text_title, g_density);
                ImGui::Text("渲染模式: %s", graphics->RenderName);
                ImGui::Text("GUI 版本: %s", IMGUI_VERSION);
                ImGui::Text("帧率: %.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
                ImGui::Spacing();
                StyledSectionHeader("内存地址", g_theme.text_title, g_density);
                ImGui::Text("模块基址: 0x%lx", GlobalMemory::libbase); ImGui::SameLine();
                ImGui::Text("对象数组: 0x%lx", GlobalMemory::Arrayaddr);
                ImGui::Text("矩阵地址: 0x%lx", GlobalMemory::Matrix);
                ImGui::Spacing();
                StyledSectionHeader("偏移与统计", g_theme.text_title, g_density);
                ImGui::Text("矩阵偏移: 0x%lx", GlobalMemory::MatrixOffset); ImGui::SameLine();
                ImGui::Text("数组偏移: 0x%lx", GlobalMemory::ArrayaddrOffset);
                ImGui::Text("模块页数: %ld", GlobalMemory::ModulePagesCount); ImGui::SameLine();
                ImGui::Text("对象数量: %d", GlobalMemory::数量);
                ImGui::Spacing();
                StyledSectionHeader("角色与坐标", g_theme.text_title, g_density);
                ImGui::Text("监管者: %s", 监管者预知);
                ImGui::Text("自身坐标: X:%.1f, Y:%.1f, Z:%.1f", Z.X, Z.Y, Z.Z);
                */
                break;
            case 1:
                StyledSectionHeader("绘制选项", g_theme.text_title, g_density);
                ImGui::Columns(2, "draw_cols", true);
                ImGui::Checkbox("增强框体", &show_draw_EnhancedFrame);
                ImGui::Checkbox("人物射线", &show_draw_Line);
                ImGui::Checkbox("绘制名字", &show_draw_Name);
                ImGui::Checkbox("绘制道具", &show_draw_Prop);
                ImGui::Checkbox("查看天赋", &g_talent_view);
                ImGui::NextColumn();
                ImGui::Checkbox("预知监管", &show_draw_prophet);
                if (ImGui::Checkbox("红夫人模式", &show_draw_redqueen)) {
                    if (show_draw_redqueen) disable_skip_filter = true;
                }
                ImGui::Checkbox("绘制地窖", &show_draw_Cellar);
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_muted, "方框颜色");
                ImGui::ColorEdit3("求生者", (float*)&g_BoxColor_Survivor);
                ImGui::ColorEdit3("监管者", (float*)&g_BoxColor_Hunter);
                ImGui::ColorEdit3("幽灵", (float*)&g_BoxColor_Ghost);
                ImGui::Columns(1);
                
                ImGui::Spacing(); ImGui::Separator();
                StyledSectionHeader("场景对象", g_theme.text_title, g_density);
                // ★ 每行：勾选框 + 对应距离滑块
                ImGui::Checkbox("椅子", &show_draw_Chair);
                ImGui::SameLine(0, 14.0f * g_density);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SliderInt("##chair_dist", &g_chair_dist, 10, 100, "%d m");
                ImGui::PopItemWidth();

                ImGui::Checkbox("板子", &show_draw_BANZI);
                ImGui::SameLine(0, 14.0f * g_density);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SliderInt("##board_dist", &g_board_dist, 10, 100, "%d m");
                ImGui::PopItemWidth();

                ImGui::Checkbox("道具箱", &show_draw_BoxItem);
                ImGui::SameLine(0, 14.0f * g_density);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SliderInt("##box_dist", &g_box_dist, 10, 100, "%d m");
                ImGui::PopItemWidth();

                ImGui::Checkbox("密码机", &show_draw_sender);
                ImGui::SameLine(0, 14.0f * g_density);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SliderInt("##sender_dist", &g_sender_dist, 10, 100, "%d m");
                ImGui::PopItemWidth();

                if (StyledButton("一键重置", ButtonVariant::Secondary, ImVec2(0,0), g_density)) { g_chair_dist = 30; g_board_dist = 30; g_box_dist = 30; g_sender_dist = 50; }
                
                ImGui::Spacing(); ImGui::Separator();
                StyledSectionHeader("其他", g_theme.text_title, g_density);
                ImGui::Checkbox("显示幽灵/残影", &inform_ghost); ImGui::SameLine();
                ImGui::Checkbox("无视过滤", &disable_skip_filter); ImGui::SameLine();
                if (ImGui::Checkbox("调试模式", &Debugging)) {
                    if (Debugging) OpenDebugLog();
                    else CloseDebugLog();
                }
                if (ImGui::Button("清理缓存", ImVec2(ImGui::GetContentRegionAvail().x, 32 * g_density))) {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    data_buffers[0].clear(); data_buffers[1].clear();
                    GlobalMemory::数量 = 0; 监管者预知[0] = '\0';
                    std::lock_guard<std::mutex> mimic_lock(mimic_mutex);
                    global_validRoles.clear(); bound_seat_by_class.clear();
                }
                if (ImGui::Button("打印全场坐标", ImVec2(ImGui::GetContentRegionAvail().x, 32 * g_density))) {
                    int current_idx = front_buffer_idx.load(std::memory_order_acquire);
                    const auto &current_data = data_buffers[current_idx];
                    int print_count = 0;
                    printf("\n================ 场景内所有对象详细数据 ================\n");
                    for (const auto &item : current_data) {
                        if (item.阵营 != 1 && item.阵营 != 2) continue;
                        Vector3A pos = getObjectCoordinates(item.objcoor, false);
                        if (isValidCoordinate(pos)) {
                            printf("[阵营:%d] 别名: %s | 类名: %s\n", item.阵营, item.str[0]!='\0' ? item.str : "无", item.类名[0]!='\0' ? item.类名 : "未知");
                            printf("  -> 坐标: X: %.2f, Y: %.2f, Z: %.2f\n", pos.X, pos.Y, pos.Z);
                            printf("  -> 调试: Obj: 0x%lx | Act: %d | 特征: 0x%x | 状态: %.1f | 幽灵: %s\n", item.obj, item.action, item.实体特征码, item.状态数值, item.is_ghost ? "Yes" : "No");
                            print_count++;
                        }
                    }
                    printf("共计输出 %d 个对象。\n", print_count);
                    fflush(stdout);
                }
                ImGui::Separator();
                if (ImGui::CollapsingHeader("骨骼与进度", ImGuiTreeNodeFlags_DefaultOpen)) {
                    SoHook::RenderPanel(g_current_game_pid);
                }
                break;
            case 2:
                StyledSectionHeader("自动盖板设置", g_theme.text_title, g_density);
                ImGui::Checkbox("启用自动盖板", &wood_enabled);
                ImGui::SameLine(); ImGui::Checkbox("显示触摸点", &show_touch_point);
                ImGui::SameLine(); ImGui::Checkbox("显示判定范围", &show_wood_rect);
                ImGui::SameLine(); ImGui::Checkbox("显示诊断", &g_show_wood_diag);
                ImGui::Spacing();

                // ★ 禁用时所有子控件灰掉
                ImGui::BeginDisabled(!wood_enabled);
                    ImGui::TextColored(g_theme.text_muted, "交互键坐标 (自动跨设备适配)");
                    if (ImGui::SliderFloat("X 坐标", &wood_touch_x, 0.0f, (float)displayInfo.width)) {
                        wood_touch_pct_x = wood_touch_x / (float)displayInfo.width;
                    }
                    if (ImGui::SliderFloat("Y 坐标", &wood_touch_y, 0.0f, (float)displayInfo.height)) {
                        wood_touch_pct_y = wood_touch_y / (float)displayInfo.height;
                    }
                    ImGui::Spacing();
                    ImGui::TextColored(g_theme.text_muted, "微调偏移 (粗定后精调)");
                    ImGui::InputFloat("X 偏移", &wood_offset_x, 1.0f, 10.0f, "%.0f");
                    ImGui::InputFloat("Y 偏移", &wood_offset_y, 1.0f, 10.0f, "%.0f");
                    if (StyledButton("归零偏移", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                        wood_offset_x = 0.0f; wood_offset_y = 0.0f;
                    }
                    ImGui::SameLine();
                    if (g_calib_done) ImGui::TextColored(g_theme.success, "已校准");
                    ImGui::SameLine();
                    if (StyledButton("测试触摸", ButtonVariant::Secondary, ImVec2(0,0), g_density)) { SimulateClick(wood_touch_x, wood_touch_y); }
                ImGui::Spacing();
                // 四点校准
                if (g_calib_step == 0) {
                    if (StyledButton("四点校准(重置)", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                        g_calib_done = false;
                        float w = displayInfo.width, h = displayInfo.height;
                        g_calib_inj[0][0]=500;     g_calib_inj[0][1]=500;
                        g_calib_inj[1][0]=w-500;   g_calib_inj[1][1]=500;
                        g_calib_inj[2][0]=w-500;   g_calib_inj[2][1]=h-500;
                        g_calib_inj[3][0]=500;     g_calib_inj[3][1]=h-500;
                        g_calib_step = 1;
                        SimulateClick((int)g_calib_inj[0][0], (int)g_calib_inj[0][1]);
                    }
                    ImGui::TextColored(g_theme.text_muted, "开启指针位置→四点校准→输入观察坐标");
                } else {
                    int s = g_calib_step - 1;
                    const char* cn[4]={"左上","右上","右下","左下"};
                    ImGui::TextColored(g_theme.warning, "校准 %d/4: %s 注入(%.0f,%.0f)",
                        g_calib_step, cn[s], g_calib_inj[s][0], g_calib_inj[s][1]);
                    ImGui::TextColored(g_theme.text_muted, "查看指针位置坐标, 输入观察到的X/Y:");
                    ImGui::InputFloat("观察X", &g_calib_obs_buf[s][0], 100.0f, 1000.0f, "%.0f");
                    ImGui::InputFloat("观察Y", &g_calib_obs_buf[s][1], 100.0f, 1000.0f, "%.0f");
                    if (StyledButton("重新注入此点", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                        SimulateClick((int)g_calib_inj[s][0], (int)g_calib_inj[s][1]);
                    }
                    ImGui::SameLine();
                    if (StyledButton("确认此点", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                        if (g_calib_step >= 4) {
                            float x1=g_calib_inj[0][0],y1=g_calib_inj[0][1];
                            float X1=g_calib_obs_buf[0][0],Y1=g_calib_obs_buf[0][1];
                            float x2=g_calib_inj[1][0],y2=g_calib_inj[1][1];
                            float X2=g_calib_obs_buf[1][0],Y2=g_calib_obs_buf[1][1];
                            float x3=g_calib_inj[2][0],y3=g_calib_inj[2][1];
                            float X3=g_calib_obs_buf[2][0],Y3=g_calib_obs_buf[2][1];
                            float det3 = x1*(y2-y3) + x2*(y3-y1) + x3*(y1-y2);
                            if (fabsf(det3) > 0.01f) {
                                g_calib_A = (X1*(y2-y3) + X2*(y3-y1) + X3*(y1-y2)) / det3;
                                g_calib_B = (x1*(X2-X3) + x2*(X3-X1) + x3*(X1-X2)) / det3;
                                g_calib_C = (x1*(y2*X3-y3*X2)+x2*(y3*X1-y1*X3)+x3*(y1*X2-y2*X1)) / det3;
                                g_calib_D = (Y1*(y2-y3) + Y2*(y3-y1) + Y3*(y1-y2)) / det3;
                                g_calib_E = (x1*(Y2-Y3) + x2*(Y3-Y1) + x3*(Y1-Y2)) / det3;
                                g_calib_F = (x1*(y2*Y3-y3*Y2)+x2*(y3*Y1-y1*Y3)+x3*(y1*Y2-y2*Y1)) / det3;
                                g_calib_done = true;
                            }
                            g_calib_step = 0;
                        } else {
                            g_calib_step++;
                            SimulateClick((int)g_calib_inj[g_calib_step-1][0],
                                          (int)g_calib_inj[g_calib_step-1][1]);
                        }
                    }
                    ImGui::SameLine();
                    if (StyledButton("取消", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                        g_calib_step = 0;
                    }
                }
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_muted, "判定参数");
                ImGui::SliderFloat("触发距离(米)", &wood_trigger_dist, 0.0f, 5.0f);
                ImGui::SliderFloat("冷却时间(秒)", &wood_cooldown_dur, 0.5f, 5.0f);
                ImGui::SliderFloat("判定长度", &wood_length, 5.0f, 30.0f);
                ImGui::SliderFloat("判定宽度", &wood_width, 3.0f, 20.0f);
                ImGui::Spacing();
                ImGui::EndDisabled();
                ImGui::TextColored(g_theme.warning, "提示：先测试触摸，确认交互键有反应后再开启");
                break;
            case 3:
                StyledSectionHeader("模仿者模式扫描", g_theme.text_title, g_density);
                if (ImGui::Checkbox("启用模仿者识别", &g_MimicModeEnabled)) {
                    if (!g_MimicModeEnabled) {
                        std::lock_guard<std::mutex> lock(mimic_mutex);
                        global_validRoles.clear(); bound_seat_by_class.clear();
                        if (is_scanning_mimic.load()) is_scanning_mimic.store(false);
                    }
                }
                if (is_scanning_mimic.load()) {
                    ImGui::TextColored(g_theme.warning, "正在深度扫描内存中...");
                } else {
                    if (StyledButton("扫描对局身份", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                        is_scanning_mimic.store(true);
                        std::thread([]() {
                            cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(7, &cpuset);
                            sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
                            scan_mimic_roles();
                            is_scanning_mimic.store(false);
                        }).detach();
                    }
                    ImGui::SameLine();
                    if (StyledButton("清空上局数据", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                        std::lock_guard<std::mutex> lock(mimic_mutex);
                        global_validRoles.clear(); bound_seat_by_class.clear();
                    }
                    ImGui::SameLine(); ImGui::Checkbox("显示独立悬浮窗", &show_mimic_overlay);
                }
                ImGui::Spacing(); ImGui::Separator();
                StyledSectionHeader("扫描结果", g_theme.text_title, g_density);
                {
                    std::lock_guard<std::mutex> lock(mimic_mutex);
                    if (global_validRoles.empty()) {
                        ImGui::Text("暂无数据或尚未扫描。");
                    } else {
                        for (const auto &r : global_validRoles) {
                            std::string roleName = RoleIdToChinese(r.roleId);
                            const char *campStr = r.campId == 1 ? "侦探团" : (r.campId == 2 ? "狼人" : "神秘客");
                            ImVec4 color;
                            if (r.campId == 1) color = ImVec4(0.2f, 0.8f, 0.8f, 1.0f);
                            else if (r.campId == 2) color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                            else color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                            ImGui::TextColored(color, "[%02d]号 | 阵营: %-6s | 身份: %s", r.index + 1, campStr, roleName.c_str());
                        }
                    }
                }
                break;
            case 4:  // 摸金模式
            {
                StyledSectionHeader("摸金模式", g_theme.text_title, g_density);
                // ★ 摸金模式 — 一键全开/全关两个模块所有选项
                if (ImGui::Checkbox("摸金模式", &MjSubsystem::draw_props)) {
                    if (MjSubsystem::draw_props) {
                        MjSubsystem::show_monsters = true;  MjSubsystem::show_big_chest = true;
                        MjSubsystem::show_small_chest = true; MjSubsystem::show_traps = true;
                        MjSubsystem::show_interactables = true; MjSubsystem::show_high_value = true;
                        MjSubsystem::show_low_value = true; MjSubsystem::show_distance = true;
                        disable_skip_filter = true; inform_ghost = true;
                        g_map_enabled = true; g_map_auto_detect = true;
                        g_show_nav_line = true; g_show_3d_paths = true;
                        g_show_saved_paths = true; g_use_calib = true;
                    } else {
                        MjSubsystem::show_monsters = false; MjSubsystem::show_big_chest = false;
                        MjSubsystem::show_small_chest = false; MjSubsystem::show_traps = false;
                        MjSubsystem::show_interactables = false; MjSubsystem::show_high_value = false;
                        MjSubsystem::show_low_value = false; MjSubsystem::show_distance = false;
                        disable_skip_filter = false; inform_ghost = false;
                        g_map_enabled = false; g_map_auto_detect = false;
                        g_show_nav_line = false; g_show_3d_paths = false;
                        g_show_saved_paths = false; g_use_calib = false;
                    }
                }
                ImGui::SameLine();
                ImGui::Checkbox("显示距离", &MjSubsystem::show_distance);
                ImGui::Text("高价值阈值");
                ImGui::SameLine(0, 12.0f * g_density);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SliderFloat("##threshold", &g_treasure_threshold, 1000.0f, 20000.0f, "%.0f");
                ImGui::PopItemWidth();
                if (g_treasure_threshold != MjSubsystem::high_value_threshold) {
                    MjSubsystem::high_value_threshold = g_treasure_threshold;
                }

                ImGui::Spacing(); ImGui::Separator();
                // ★ 每行：物品勾选框 + 对应距离滑块 — 7行统一排版
                #define MJ_ITEM_ROW(label, var_bool, var_dist) do { \
                    ImGui::Checkbox(label, &var_bool); \
                    ImGui::SameLine(0, 12.0f * g_density); \
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x); \
                    ImGui::SliderFloat("##" #var_dist, &var_dist, 5.0f, 500.0f, "%.0f m"); \
                    ImGui::PopItemWidth(); \
                } while(0)

                MJ_ITEM_ROW("怪物",            MjSubsystem::show_monsters,      MjSubsystem::max_dist_monsters);
                MJ_ITEM_ROW("紫/金宝箱",       MjSubsystem::show_big_chest,     MjSubsystem::max_dist_big_chest);
                MJ_ITEM_ROW("小箱子",          MjSubsystem::show_small_chest,   MjSubsystem::max_dist_small_chest);
                MJ_ITEM_ROW("陷阱/夹子/碎石",  MjSubsystem::show_traps,         MjSubsystem::max_dist_traps);
                MJ_ITEM_ROW("门/板/钢琴/花瓶", MjSubsystem::show_interactables, MjSubsystem::max_dist_interactables);
                MJ_ITEM_ROW("高价值物品",      MjSubsystem::show_high_value,    MjSubsystem::max_dist_high_value);
                MJ_ITEM_ROW("低价值物品",      MjSubsystem::show_low_value,     MjSubsystem::max_dist_low_value);
                #undef MJ_ITEM_ROW

                ImGui::TextColored(g_theme.text_muted, "颜色: 紫宝箱(紫) 金宝箱(金) 高价(粉) 怪物(红) 其他见过滤");
            }
                break;
            case 5:  // 地图管理（已在前面替换为完整新代码）
            {
                StyledSectionHeader("地图管理", g_theme.text_title, g_density);

                // === ★ 一键设为新地图（始终可见的快捷按钮） ===
                ImGui::PushStyleColor(ImGuiCol_Button, g_theme.success);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_theme.success_hover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_theme.success_active);
                ImGui::PushStyleColor(ImGuiCol_Text, g_theme.text_on_primary);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f * g_density);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f * g_density, 14.0f * g_density));
                // 宽高用0让ImGui根据内容自动适配 + 加大填充确保文字完整
                ImGui::SetNextItemWidth(-1); // 占满整行
                if (ImGui::Button("★ 注册当前场景为新地图", ImVec2(0, 0))) {
                    // ★ 检查是否已知地图
                    if (g_current_map_index >= 0 && g_current_map_index < (int)g_all_maps.size() && !g_all_maps[g_current_map_index].empty()) {
                        AddNotification("当前已是已知地图: " + std::string(g_all_maps[g_current_map_index][0].name), 3.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                    } else {
                        if (g_detected_musicbox_pos.X != 0.0f || g_detected_musicbox_pos.Y != 0.0f) {
                        new_music_x = g_detected_musicbox_pos.X;
                        new_music_y = g_detected_musicbox_pos.Y;
                        new_music_z = g_detected_musicbox_pos.Z;
                    } else if (Z.X != 0.0f || Z.Y != 0.0f) {
                        new_music_x = Z.X;
                        new_music_y = Z.Y;
                        new_music_z = Z.Z;
                    }
                    // 同步检测钢琴位置（第二信号源）
                    if (g_detected_piano_pos.X != 0.0f || g_detected_piano_pos.Y != 0.0f) {
                        new_piano_x = g_detected_piano_pos.X;
                        new_piano_y = g_detected_piano_pos.Y;
                        new_piano_z = g_detected_piano_pos.Z;
                    } else {
                        new_piano_x = new_piano_y = new_piano_z = 0.0f;
                    }
                    int next_id = (int)g_all_maps.size() + 1;
                    snprintf(new_name, sizeof(new_name), "地图%d 一楼", next_id);
                    snprintf(new_texture, sizeof(new_texture), MAPS_ROOT "map%d_floor1.png", next_id);
                    new_map_number = 0;
                    ImGui::OpenPopup("添加新地图##detected");
                    }
                }
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(4);

                bool map_invalid = (g_current_map_index < 0 || g_current_map_index >= (int)g_all_maps.size() ||
                                    g_all_maps[g_current_map_index].empty());

                if (map_invalid) {
                    ImGui::TextColored(g_theme.warning, "尚未识别地图，请用下方「切换地图」手动选择");
                    ImGui::TextColored(g_theme.text_muted, "%s", g_map_scores_buf);
                }

                ImGui::Checkbox("启用导航地图", &g_map_enabled);
                ImGui::SameLine();
                ImGui::Checkbox("自动识别", &g_map_auto_detect);
                ImGui::SameLine();
                ImGui::Checkbox("路线规划", &g_show_nav_line);
                ImGui::SameLine();
                ImGui::Checkbox("显示识别状态", &g_show_map_status);

                ImGui::Checkbox("3D立体路径", &g_show_3d_paths);
                if (g_show_3d_paths) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100.0f * g_density);
                    ImGui::SliderFloat("高度(cm)", &g_3d_path_height, 0.0f, 200.0f, "%.0f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100.0f * g_density);
                    ImGui::SliderFloat("渐变(m)", &g_3d_path_fade_dist, 5.0f, 100.0f, "%.0f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.0f * g_density);
                    const char* style_items[] = {"线条", "圆点", "箭头"};
                    ImGui::Combo("##style", &g_3d_path_style, style_items, 3);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f * g_density);
                    ImGui::SliderFloat("速度", &g_3d_flow_speed, 0.0f, 60.0f, "%.0f");
                    ImGui::SameLine();
                    if (g_3d_path_style == 0) {
                        ImGui::SetNextItemWidth(100.0f * g_density);
                        ImGui::SliderFloat("线宽(px)", &g_3d_line_width, 1.0f, 10.0f, "%.1f");
                    } else {
                        ImGui::SetNextItemWidth(100.0f * g_density);
                        ImGui::SliderFloat("大小(px)", &g_3d_dot_radius, 20.0f, 100.0f, "%.0f");
                    }
                }

                // ★ 手动选择地图 + 楼层（始终可见）
                {
                    // ── 地图下拉 ──
                    std::vector<const char*> manual_names;
                    std::vector<int> manual_indices;
                    for (int i = 0; i < (int)g_all_maps.size(); i++) {
                        if (!g_all_maps[i].empty() && g_all_maps[i][0].name && strlen(g_all_maps[i][0].name) > 0) {
                            manual_names.push_back(g_all_maps[i][0].name);
                            manual_indices.push_back(i);
                        }
                    }
                    if (!manual_names.empty()) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(130);
                        int combo_idx = 0;
                        for (int i = 0; i < (int)manual_indices.size(); i++) {
                            if (manual_indices[i] == g_current_map_index) { combo_idx = i; break; }
                        }
                        if (ImGui::Combo("切换地图", &combo_idx, manual_names.data(), manual_names.size())) {
                            if (combo_idx >= 0 && combo_idx < (int)manual_indices.size()) {
                                int new_idx = manual_indices[combo_idx];
                                g_current_map_index = new_idx;
                                g_current_floor_index = SafeClampFloorIdx(new_idx, 0);
                                g_detect_phase = MapDetectPhase::LOCKED;
                                g_detect_debounce_frames = 0; g_detect_best_fp_id = -1; g_locked_stable_frames = 0;
                                g_map_auto_detect = false;
                                LoadMapTexture(new_idx, g_current_floor_index);
                                AddNotification("已切换地图", 1.5f, ImVec4(0.3f,1.0f,0.3f,1.0f));
                            }
                        }

                        // ── 楼层下拉（仅当前地图有 ≥2 层时显示）──
                        if (g_current_map_index >= 0 && g_current_map_index < (int)g_all_maps.size()) {
                            int nFloors = (int)g_all_maps[g_current_map_index].size();
                            if (nFloors >= 2) {
                                ImGui::SameLine();
                                ImGui::SetNextItemWidth(70);
                                int curF = g_current_floor_index;
                                if (curF < 0) curF = 0;
                                if (curF >= nFloors) curF = nFloors - 1;
                                // 构建楼层名列表
                                std::vector<const char*> floor_names;
                                std::vector<int> floor_vals;
                                static std::list<std::string> floor_name_buf;
                                for (int f = 0; f < nFloors; f++) {
                                    floor_name_buf.push_back(std::to_string(f + 1) + "楼");
                                    floor_names.push_back(floor_name_buf.back().c_str());
                                    floor_vals.push_back(f);
                                }
                                int fl_combo = 0;
                                for (int f = 0; f < nFloors; f++) {
                                    if (f == curF) { fl_combo = f; break; }
                                }
                                if (ImGui::Combo("楼层", &fl_combo, floor_names.data(), nFloors)) {
                                    g_current_floor_index = SafeClampFloorIdx(g_current_map_index, fl_combo);
                                    g_detect_phase = MapDetectPhase::LOCKED;
                                    g_map_auto_detect = false;
                                    LoadMapTexture(g_current_map_index, g_current_floor_index);
                                    AddNotification("已切换楼层", 1.5f, ImVec4(0.3f,1.0f,0.3f,1.0f));
                                }
                                // 清理临时字符串（每帧重建但没问题）
                                floor_name_buf.clear();
                            }
                        }
                    }
                }

                // ★ 手动选择后重新评估
                map_invalid = (g_current_map_index < 0 || g_current_map_index >= (int)g_all_maps.size() ||
                               g_all_maps[g_current_map_index].empty());

                if (!map_invalid) {
                    bool unknown_map = false;
                    if (g_map_enabled && g_map_auto_detect) {
                        bool found_in_db = false;
                        for (auto& mb : g_musicbox_db) {
                            if (mb.mapIndex == g_current_map_index) {
                                found_in_db = true;
                                break;
                            }
                        }
                        if (!found_in_db) unknown_map = true;
                    }

                    if (unknown_map) {
                        ImGui::TextColored(g_theme.warning, "未知地图 (自动识别未匹配)");
                        if (g_detected_musicbox_pos.X != 0.0f || g_detected_musicbox_pos.Y != 0.0f) {
                            ImGui::SameLine();
                            if (StyledButton("以此坐标添加新地图", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                                ImGui::SetNextWindowFocus();
                                new_music_x = g_detected_musicbox_pos.X;
                                new_music_y = g_detected_musicbox_pos.Y;
                                new_music_z = g_detected_musicbox_pos.Z;
                                new_piano_x = g_detected_piano_pos.X;
                                new_piano_y = g_detected_piano_pos.Y;
                                new_piano_z = g_detected_piano_pos.Z;
                                int next_id = (int)g_all_maps.size() + 1;
                                snprintf(new_name, sizeof(new_name), "地图%d 一楼", next_id);
                                snprintf(new_texture, sizeof(new_texture), MAPS_ROOT "map%d_floor1.png", next_id);
                                new_map_number = 0;
                                ImGui::OpenPopup("添加新地图##detected");
                            }
                        }
                    } else if (g_current_map_index >= 0 && g_current_map_index < (int)g_all_maps.size() && !g_all_maps[g_current_map_index].empty()) {
                        int dispFloor = SafeClampFloorIdx(g_current_map_index, g_current_floor_index);
                        ImGui::TextColored(g_theme.success, "当前: %s", g_all_maps[g_current_map_index][dispFloor].name);
                        ImGui::SameLine();
                        ImGui::TextColored(g_theme.text_muted, " [idx=%d tex=%s]",
                            g_current_map_index,
                            g_all_maps[g_current_map_index][dispFloor].texturePath);

                        ImGui::SameLine();
                        const char* floorName = (g_current_floor_index == 0) ? "(1楼)" : "(2楼)";
                        ImGui::TextColored(g_theme.text_muted, "%s", floorName);

                        ImGui::TextColored(g_theme.text_muted, "%s", g_score_debug_buf);

                        // ★ 评分可视化：解析 Top3 绘制进度条
                        const char* p = g_score_debug_buf;
                        for (int rank = 0; rank < 3; rank++) {
                            const char* hash = strstr(p, "#");
                            if (!hash) break;
                            const char* slash = strstr(hash, "/110");
                            if (!slash) break;
                            // 从 "/110" 往前找 ": " → ": 60.0"
                            const char* colon = slash;
                            while (colon > hash && *colon != ':') colon--;
                            if (colon <= hash) break;
                            float score = strtof(colon + 2, nullptr);
                            const char* fp = strstr(hash, "fp[");
                            const char* fpe = fp ? strchr(fp + 3, ']') : nullptr;
                            char lbl[80];
                            if (fp && fpe) snprintf(lbl, sizeof(lbl), "#%d 地图%.*s %.0f/110", rank + 1, (int)(fpe - fp - 3), fp + 3, score);
                            else snprintf(lbl, sizeof(lbl), "#%d %.0f/110", rank + 1, score);
                            ImGui::ProgressBar(score / 110.0f, ImVec2(-1, 0), lbl);
                            p = slash + 4;
                        }

                        // === 音乐盒被移动到了同地图其他位置 ===
                        if (g_musicbox_moved && g_map_auto_detect) {
                            ImGui::TextColored(g_theme.warning,
                                "[警告] 音乐盒位置已改变 (可能被拾取后重新放置)");
                            if (g_detected_musicbox_pos.X != 0.0f || g_detected_musicbox_pos.Y != 0.0f) {
                                ImGui::SameLine();
                                if (StyledButton("更新音乐盒坐标", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                                    g_musicbox_moved = false;
                                    new_music_x = g_detected_musicbox_pos.X;
                                    new_music_y = g_detected_musicbox_pos.Y;
                                    new_music_z = g_detected_musicbox_pos.Z;
                                    new_piano_x = g_detected_piano_pos.X;
                                    new_piano_y = g_detected_piano_pos.Y;
                                    new_piano_z = g_detected_piano_pos.Z;
                                    // 使用当前地图名称
                                    snprintf(new_name, sizeof(new_name), "%s",
                                        g_all_maps[g_current_map_index][g_current_floor_index].name);
                                    snprintf(new_texture, sizeof(new_texture), "%s",
                                        g_all_maps[g_current_map_index][g_current_floor_index].texturePath);
                                    new_map_number = 0;
                                    ImGui::OpenPopup("添加新地图##detected");
                                }
                            }
                            ImGui::Separator();
                        }

                        // === 同步场景物体到当前地图（音乐盒+钢琴+凳子）===
                        ImGui::TextColored(g_theme.text_muted, "[M][P][C] 音乐盒:%s | 钢琴:%s | 凳子:%zu",
                            (g_detected_musicbox_pos.X != 0.0f ? "OK" : "--"),
                            (g_detected_piano_pos.X != 0.0f ? "OK" : "--"),
                            g_detected_chairs.size());
                        ImGui::SameLine();
                        if (StyledButton("[同步] 物体到当前地图", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                            SyncResult sr = SaveSceneObjectsToJSON(g_current_map_index, g_current_floor_index);
                            // ★ 详细通知: 地图名 + 索引 + 各类型数量
                            const char* mapName = g_all_maps[g_current_map_index][g_current_floor_index].name;
                            char syncMsg[256];
                            snprintf(syncMsg, sizeof(syncMsg),
                                "同步到: %s[%d楼] | 音乐盒:%d 钢琴:%d 凳子:%d",
                                mapName, g_current_floor_index + 1,
                                sr.musicbox, sr.piano, sr.chairs);
                            AddNotification(syncMsg, 4.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                            // 指纹数据库也刷新
                            LoadFingerprintDB(); RebuildFingerprintMapping();
                            MarkExitsDirty();
                        }
                        // ★ 手动模式提示
                        if (!g_map_auto_detect) {
                            ImGui::SameLine();
                            ImGui::TextColored(g_theme.warning, "(手动地图)");
                        }
                    } else {
                        ImGui::TextColored(g_theme.danger, "地图索引无效");
                    }

                    ImGui::TextColored(g_theme.warning, "纹理: %s", g_texture_status);
                    ImGui::TextColored(g_theme.text_muted, "检测: %s", g_map_detect_debug);

                    ImGui::SliderFloat("尺寸", &g_map_display_size, 100, 1800, "%.0f");
                    ImGui::SliderFloat("位置X", &g_map_pos_x, 0, displayInfo.width, "%.0f");
                    ImGui::SliderFloat("位置Y", &g_map_pos_y, 0, displayInfo.height, "%.0f");
                    ImGui::SliderFloat("标签大小", &g_map_label_scale, 0.2f, 0.8f, "%.2f");

                    ImGui::Separator();
                }

                if (ImGui::CollapsingHeader("透明度设置")) {
                    ImGui::SliderFloat("地图底图", &g_map_opacity, 0.1f, 1.0f);
                    ImGui::SliderFloat("物品标签", &g_label_opacity, 0.1f, 1.0f);
                    ImGui::SliderFloat("自身标记", &g_self_opacity, 0.1f, 1.0f);
                    ImGui::SliderFloat("路线", &g_route_opacity, 0.1f, 1.0f);
                    ImGui::SliderFloat("已保存路径", &g_saved_path_opacity, 0.1f, 1.0f);
                }

                if (!map_invalid) {
                    ImGui::Separator();

                    StyledSectionHeader("路径绘制", g_theme.text_title, g_density);
                    auto& cfg_path = g_all_maps[g_current_map_index][g_current_floor_index];
                    if (cfg_path.calibrated) {
                        ImGui::Checkbox("显示已绘制路径", &g_show_saved_paths);
                        ImGui::SliderFloat("平滑强度", &g_smooth_strength, 0.0f, 20.0f, "%.0f px");
                        // 网格吸附功能已移除，路径点定位在用户实际点击的精确位置
                        ImGui::Checkbox("显示网格参考线", &g_show_grid);
                        if (g_show_grid) {
                            ImGui::SliderFloat("网格透明度", &g_grid_alpha, 0.1f, 1.0f);
                        }
                        ImGui::Separator();
                        if (StyledButton("开始绘制路径", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                            g_draw_map_size_bak = g_map_display_size;
                            g_draw_map_posx_bak = g_map_pos_x;
                            g_draw_map_posy_bak = g_map_pos_y;
                            g_map_display_size = std::min(displayInfo.height * 0.75f, 1600.0f);
                            g_map_pos_x = (displayInfo.width - g_map_display_size) * 0.5f;
                            g_map_pos_y = (displayInfo.height - g_map_display_size) * 0.5f;
                            g_path_edit_mode = 1;
                            g_current_drawing_path.clear();
                        }
                        ImGui::SameLine();
                        if (StyledButton("取消绘制", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                            g_map_display_size = g_draw_map_size_bak;
                            g_map_pos_x = g_draw_map_posx_bak;
                            g_map_pos_y = g_draw_map_posy_bak;
                            g_path_edit_mode = 0;
                            g_current_drawing_path.clear();
                        }
                        ImGui::SameLine();
                        if (StyledButton("清除所有路径", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                            ImGui::OpenPopup("确认清除路径");
                        }
                    } else {
                        ImGui::TextColored(g_theme.warning, "请先校准地图才能绘制路径");
                    }
                    if (ImGui::BeginPopupModal("确认清除路径", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("确定要清除所有手动画线路径吗？此操作不可撤销。");
                        if (StyledButton("确定", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                            g_saved_paths.clear();
                            g_current_drawing_path.clear();
                            g_path_edit_mode = 0;
                            MarkPathsDirty();
                            AddNotification("路径已清除", 2.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (StyledButton("取消", ButtonVariant::Secondary, ImVec2(0,0), g_density)) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    if (g_path_edit_mode == 1) {
                        ImGui::SameLine();
                        if (StyledButton("撤销上一个点", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                            if (!g_current_drawing_path.empty()) {
                                g_current_drawing_path.pop_back();
                            }
                        }
                    }
                    if (g_pending_save_confirm) {
                        g_pending_save_timeout -= ImGui::GetIO().DeltaTime;
                        ImGui::Separator();
                        ImGui::TextColored(g_theme.warning, "路径绘制完成 (%d点), 请确认:  %.0f秒后自动丢弃", (int)g_pending_path.size(), std::max(0.0f, g_pending_save_timeout));
                        if (StyledButton("保存路径", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                            // ★ 端点自动吸附：首尾点贴近已有路径端点时自动对齐
                            if (g_pending_path.size() >= 2) {
                                auto snapPoint = [&](Vector3A& pt) {
                                    float best = kPathSnapThreshold;
                                    Vector3A bestPos = pt;
                                    for (auto& p : g_saved_paths) {
                                        if (p.size() < 2) continue;
                                        for (int ep : {0, (int)p.size()-1}) {
                                            float dx = pt.X-p[ep].X, dy = pt.Y-p[ep].Y;
                                            float d = sqrtf(dx*dx+dy*dy);
                                            if (d < best) { best = d; bestPos = p[ep]; }
                                        }
                                    }
                                    pt = bestPos;
                                };
                                snapPoint(g_pending_path.front());
                                snapPoint(g_pending_path.back());
                            }
                            g_saved_paths.push_back(g_pending_path); g_path_visible.push_back(true); g_path_colors.push_back(0);
                            while (g_saved_paths_by_map.size() <= g_current_map_index) g_saved_paths_by_map.push_back({});
                            while (g_saved_paths_by_map[g_current_map_index].size() <= g_current_floor_index) g_saved_paths_by_map[g_current_map_index].push_back({});
                            g_saved_paths_by_map[g_current_map_index][g_current_floor_index].push_back(g_pending_path);
                            MarkPathsDirty();
                            AddNotification("路径已保存 [OK]", 2.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                            g_pending_path.clear(); g_pending_save_confirm = false;
                            g_map_display_size = g_draw_map_size_bak; g_map_pos_x = g_draw_map_posx_bak; g_map_pos_y = g_draw_map_posy_bak;
                        }
                        ImGui::SameLine();
                        if (StyledButton("丢弃", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                            g_pending_path.clear(); g_pending_save_confirm = false;
                            AddNotification("路径已丢弃", 1.5f, ImVec4(0.8f, 0.5f, 0.3f, 1.0f));
                            g_map_display_size = g_draw_map_size_bak; g_map_pos_x = g_draw_map_posx_bak; g_map_pos_y = g_draw_map_posy_bak;
                        }
                        if (g_pending_save_timeout <= 0.0f) {
                            g_pending_path.clear(); g_pending_save_confirm = false;
                            AddNotification("路径已自动丢弃(超时)", 1.5f, ImVec4(0.8f, 0.5f, 0.3f, 1.0f));
                            g_map_display_size = g_draw_map_size_bak; g_map_pos_x = g_draw_map_posx_bak; g_map_pos_y = g_draw_map_posy_bak;
                        }
                    }
                    // 路径列表（触屏设备用）
                    if (!g_saved_paths.empty()) {
                        while (g_path_visible.size() < g_saved_paths.size()) g_path_visible.push_back(true);
                        while (g_path_colors.size() < g_saved_paths.size()) g_path_colors.push_back(0);
                        ImGui::TextColored(g_theme.text_title, "已保存路径列表:");
                        int deleteTarget = -1;
                        int reverseTarget = -1;
                        static bool show_all = true;
                        if (ImGui::SmallButton(show_all ? "全部隐藏" : "全部显示")) {
                            show_all = !show_all;
                            for (size_t v = 0; v < g_path_visible.size(); v++) g_path_visible[v] = show_all;
                        }
                        for (size_t pi = 0; pi < g_saved_paths.size(); pi++) {
                            char label[64];
                            bool isSelected = ((int)pi == g_selected_path_index);
                            ImGui::PushID((int)pi + 10000);
                            bool vis_tmp = (bool)g_path_visible[pi];
                            ImGui::Checkbox("##vis", &vis_tmp);
                            g_path_visible[pi] = (char)vis_tmp;
                            ImGui::PopID();
                            ImGui::SameLine(0, 14.0f * g_density);
                            {
                                const ImU32 palette[] = {
                                    IM_COL32(0,255,255,255), IM_COL32(255,100,255,255),
                                    IM_COL32(255,255,0,255), IM_COL32(100,255,100,255),
                                    IM_COL32(255,150,50,255), IM_COL32(100,150,255,255),
                                    IM_COL32(255,100,100,255), IM_COL32(200,200,200,255),
                                };
                                ImU32 cur = (pi < g_path_colors.size() && g_path_colors[pi] != 0) ? g_path_colors[pi] : palette[pi % 8];
                                ImGui::PushID((int)pi + 20000);
                                if (ImGui::ColorButton("##clr", ImVec4(((cur>>0)&0xFF)/255.0f, ((cur>>8)&0xFF)/255.0f, ((cur>>16)&0xFF)/255.0f, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(44, 32))) {
                                    int ci = 0;
                                    for (int pidx = 0; pidx < 8; pidx++) if (palette[pidx] == cur) { ci = pidx; break; }
                                    if (g_path_colors.size() <= pi) g_path_colors.resize(pi+1, 0);
                                    g_path_colors[pi] = palette[(ci + 1) % 8];
                                }
                                ImGui::PopID();
                            }
                            ImGui::SameLine();
                            snprintf(label, sizeof(label), "路径 #%zu [%zu点]%s", pi, g_saved_paths[pi].size(), isSelected ? " ★" : "");
                            ImGui::PushID((int)pi);
                            if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, g_theme.success);
                            if (ImGui::SmallButton(label)) { g_selected_path_index = isSelected ? -1 : (int)pi; }
                            if (isSelected) ImGui::PopStyleColor();
                            ImGui::SameLine();
                            if (ImGui::SmallButton("删除")) { deleteTarget = (int)pi; }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("反向")) { reverseTarget = (int)pi; }
                            ImGui::PopID();
                        }
                        if (deleteTarget >= 0) {
                            g_saved_paths.erase(g_saved_paths.begin() + deleteTarget);
                            if (deleteTarget < (int)g_path_visible.size()) g_path_visible.erase(g_path_visible.begin() + deleteTarget);
                            if (deleteTarget < (int)g_path_colors.size()) g_path_colors.erase(g_path_colors.begin() + deleteTarget);
                            if (g_selected_path_index == deleteTarget) g_selected_path_index = -1;
                            else if (g_selected_path_index > deleteTarget) g_selected_path_index--;
                            MarkPathsDirty();
                            AddNotification("路径已删除", 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                        }
                        if (reverseTarget >= 0 && reverseTarget < (int)g_saved_paths.size()) {
                            std::reverse(g_saved_paths[reverseTarget].begin(), g_saved_paths[reverseTarget].end());
                            MarkPathsDirty();
                            AddNotification("路径已反向", 1.5f, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                        }
                    }
                    ImGui::TextColored(g_theme.text_muted, "长按地图拖动绘制路径，松开保存（自由模式：任意方向平滑弯曲）");
                    if (g_path_edit_mode == 1) {
                        ImGui::TextColored(g_theme.info,
                            g_ortho_draw ? "[OK] 正交模式：强制水平/垂直线段" : "[*] 自由模式：任意方向平滑曲线");
                        if (g_current_drawing_path.size() >= 2) {
                            float total_len = 0.0f;
                            for (size_t k = 1; k < g_current_drawing_path.size(); k++) {
                                float ddx = g_current_drawing_path[k].X - g_current_drawing_path[k-1].X;
                                float ddy = g_current_drawing_path[k].Y - g_current_drawing_path[k-1].Y;
                                total_len += sqrtf(ddx*ddx + ddy*ddy);
                            }
                            ImGui::SameLine();
                            ImGui::TextColored(g_theme.success, "| 长度: %.1f米 (%zu点)", total_len / 100.0f, g_current_drawing_path.size());
                        }
                    }

                    ImGui::Separator();
                    StyledSectionHeader("路径绘制选项", g_theme.text_title, g_density);
                    ImGui::Checkbox("正交绘制 (水平/垂直)", &g_ortho_draw);
                    ImGui::SliderFloat("平滑度", &g_smooth_strength, 1.0f, 30.0f, "%.1f");

                    ImGui::Separator();
                    StyledSectionHeader("路径显示", g_theme.text_title, g_density);
                    ImGui::Checkbox("显示已保存路径", &g_show_saved_paths);
                    ImGui::SameLine();
                    if (StyledButton(g_show_saved_paths ? "隐藏" : "显示", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                        g_show_saved_paths = !g_show_saved_paths;
                    }
                    if (g_show_saved_paths) {
                        ImGui::SliderFloat("路径透明度", &g_saved_path_opacity, 0.1f, 1.0f, "%.2f");
                    }
                    ImGui::Separator();

                    if (ImGui::CollapsingHeader("地图校准", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::Checkbox("启用校准", &g_use_calib)) {
                            if (g_use_calib) {
                                // ★ 校准模式：自动放大居中地图
                                g_draw_map_size_bak = g_map_display_size;
                                g_draw_map_posx_bak = g_map_pos_x; g_draw_map_posy_bak = g_map_pos_y;
                                g_map_display_size = std::min((float)displayInfo.height * 0.75f, 1600.0f);
                                g_map_pos_x = (displayInfo.width - g_map_display_size) * 0.5f;
                                g_map_pos_y = (displayInfo.height - g_map_display_size) * 0.5f;
                            } else {
                                // 退出校准：恢复
                                g_map_display_size = g_draw_map_size_bak;
                                g_map_pos_x = g_draw_map_posx_bak; g_map_pos_y = g_draw_map_posy_bak;
                            }
                        }
                        if (g_use_calib) {
                            ImGui::SameLine();
                            ImGui::TextColored(g_theme.warning, "  ⚠ 校准中 — 调整完毕请保存");
                            if (ImGui::CollapsingHeader("校准步骤说明", ImGuiTreeNodeFlags_None)) {
                                ImGui::TextWrapped(
                                        "1. 站在可辨认位置，点按钮记录点1/点2\n"
                                        "2. 参照游戏小地图，拖动（橙色/蓝色标记）到纹理图对应位置\n"
                                        "3. 第二个点与第一个点必须有一段距离（两点距离要远）\n"
                                        "4. 完成上述步骤点击\"自动计算\"并保存\n"
                                        "（若已知音乐盒纹理位置，可直接\"填入预设\"）\n"
                                        "（若这些文字让您的CPU过载，建议您与手机一同进入休眠模式，对彼此都好）"
                                );
                            }
                            ImGui::SeparatorText("📍 点1 — 音乐盒位置");
                            {
                                auto& floors = g_all_maps[g_current_map_index];
                                if (!floors.empty()) {
                                    auto& cfg = floors[g_current_floor_index];
                                    if (cfg.calibrated) {
                                        g_map_scale_x = cfg.scaleX; g_map_scale_y = cfg.scaleY;
                                        g_map_offset_u = cfg.offsetU; g_map_offset_v = cfg.offsetV;
                                        g_map_flip_x = cfg.flipX; g_map_flip_y = cfg.flipY;
                                    }
                                }
                            }
                            ImGui::PushStyleColor(ImGuiCol_Text, g_theme.info);
                            if (StyledButton("填入预设音乐盒位置", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                                MusicboxKey* found = nullptr;
                                for (auto& mb : g_musicbox_db) {
                                    if (mb.mapIndex == g_current_map_index) {
                                        if (mb.floorIndex == g_current_floor_index) { found = &mb; break; }
                                        if (!found) found = &mb;
                                    }
                                }
                                if (found) { g_pt1_wx = found->x; g_pt1_wy = found->y; g_pt1_tu = found->texU; g_pt1_tv = found->texV; }
                            }
                            if (StyledButton("将我当前位置设为点1 (音乐盒)", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                                g_pt1_wx = Z.X;
                                g_pt1_wy = Z.Y;
                            }
                            ImGui::DragFloat("世界X1", &g_pt1_wx, 1.0f, -50000, 50000, "%.1f");
                            ImGui::DragFloat("世界Y1", &g_pt1_wy, 1.0f, -50000, 50000, "%.1f");

                            ImGui::PushItemWidth(80);
                            ImGui::DragFloat("纹理U1", &g_pt1_tu, 0.001f, -2.0f, 2.0f, "%.4f");
                            ImGui::PopItemWidth();
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("◀ 左##u1", ImVec2(50, 40))) {
                                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                                }
                                g_pt1_tu -= 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_u1l += ImGui::GetIO().DeltaTime;
                                if (hold_u1l > 0.3f) g_pt1_tu -= 0.002f;
                            } else { hold_u1l = 0; }
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▶ 右##u1", ImVec2(50, 40))) {
                                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                                }
                                g_pt1_tu += 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_u1r += ImGui::GetIO().DeltaTime;
                                if (hold_u1r > 0.3f) g_pt1_tu += 0.002f;
                            } else { hold_u1r = 0; }

                            ImGui::PushItemWidth(80);
                            ImGui::DragFloat("纹理V1", &g_pt1_tv, 0.001f, -2.0f, 2.0f, "%.4f");
                            ImGui::PopItemWidth();
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▲ 上##v1", ImVec2(50, 40))) {
                                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                                }
                                g_pt1_tv -= 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_v1u += ImGui::GetIO().DeltaTime;
                                if (hold_v1u > 0.3f) g_pt1_tv -= 0.002f;
                            } else { hold_v1u = 0; }
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▼ 下##v1", ImVec2(50, 40))) {
                                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                                }
                                g_pt1_tv += 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_v1d += ImGui::GetIO().DeltaTime;
                                if (hold_v1d > 0.3f) g_pt1_tv += 0.002f;
                            } else { hold_v1d = 0; }

                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("↩##undo1", ImVec2(40, 40)) && !g_pt1_history.empty()) {
                                auto prev = g_pt1_history.back();
                                g_pt1_history.pop_back();
                                g_pt1_tu = prev.first;
                                g_pt1_tv = prev.second;
                            }

                            ImGui::PopStyleColor(); // end 点1 blue text

                            ImGui::SeparatorText("📍 点2 — 大门位置");
                            ImGui::PushStyleColor(ImGuiCol_Text, g_theme.warning);
                            if (StyledButton("将我当前位置设为点2 (大门)", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                                g_pt2_wx = Z.X;
                                g_pt2_wy = Z.Y;
                            }
                            ImGui::DragFloat("世界X2", &g_pt2_wx, 1.0f, -50000, 50000, "%.1f");
                            ImGui::DragFloat("世界Y2", &g_pt2_wy, 1.0f, -50000, 50000, "%.1f");

                            ImGui::PushItemWidth(80);
                            ImGui::DragFloat("纹理U2", &g_pt2_tu, 0.001f, -2.0f, 2.0f, "%.4f");
                            ImGui::PopItemWidth();
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("◀ 左##u2", ImVec2(50, 40))) {
                                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                                }
                                g_pt2_tu -= 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_u2l += ImGui::GetIO().DeltaTime;
                                if (hold_u2l > 0.3f) g_pt2_tu -= 0.002f;
                            } else { hold_u2l = 0; }
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▶ 右##u2", ImVec2(50, 40))) {
                                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                                }
                                g_pt2_tu += 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_u2r += ImGui::GetIO().DeltaTime;
                                if (hold_u2r > 0.3f) g_pt2_tu += 0.002f;
                            } else { hold_u2r = 0; }

                            ImGui::PushItemWidth(80);
                            ImGui::DragFloat("纹理V2", &g_pt2_tv, 0.001f, -2.0f, 2.0f, "%.4f");
                            ImGui::PopItemWidth();
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▲ 上##v2", ImVec2(50, 40))) {
                                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                                }
                                g_pt2_tv -= 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_v2u += ImGui::GetIO().DeltaTime;
                                if (hold_v2u > 0.3f) g_pt2_tv -= 0.002f;
                            } else { hold_v2u = 0; }
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▼ 下##v2", ImVec2(50, 40))) {
                                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                                }
                                g_pt2_tv += 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_v2d += ImGui::GetIO().DeltaTime;
                                if (hold_v2d > 0.3f) g_pt2_tv += 0.002f;
                            } else { hold_v2d = 0; }

                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("↩##undo2", ImVec2(40, 40)) && !g_pt2_history.empty()) {
                                auto prev = g_pt2_history.back();
                                g_pt2_history.pop_back();
                                g_pt2_tu = prev.first;
                                g_pt2_tv = prev.second;
                            }

                            ImGui::PopStyleColor(); // end 点2 orange text

                            if (StyledButton("自动计算并保存", ButtonVariant::Success, ImVec2(0,0), g_density)) {
                                if ((fabsf(g_pt1_wx) < 0.01f && fabsf(g_pt1_wy) < 0.01f) ||
                                    (fabsf(g_pt2_wx) < 0.01f && fabsf(g_pt2_wy) < 0.01f)) {
                                    AddNotification("请先记录点1和点2的世界坐标", 3.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                                } else {
                                    float dx = g_pt2_wx - g_pt1_wx;
                                    float dy = g_pt2_wy - g_pt1_wy;
                                    float du = g_pt2_tu - g_pt1_tu;
                                    float dv = g_pt2_tv - g_pt1_tv;

                                    if (fabsf(dx) < 0.01f || fabsf(dy) < 0.01f) {
                                        AddNotification("两点距离太近，请重新选点", 2.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                                    } else {
                                        g_map_scale_x = du / dx;
                                        g_map_scale_y = dv / dy;
                                        g_map_offset_u = g_pt1_tu - g_map_scale_x * g_pt1_wx;
                                        g_map_offset_v = g_pt1_tv - g_map_scale_y * g_pt1_wy;

                                        auto& floorRef = g_all_maps[g_current_map_index];
                                        if (!floorRef.empty()) {
                                            auto& Cfg = floorRef[g_current_floor_index];
                                            Cfg.scaleX = g_map_scale_x;
                                            Cfg.scaleY = g_map_scale_y;
                                            Cfg.offsetU = g_map_offset_u;
                                            Cfg.offsetV = g_map_offset_v;
                                            Cfg.flipX = g_map_flip_x;
                                            Cfg.flipY = g_map_flip_y;
                                            Cfg.calibrated = true;
                                            g_use_calib = false;

                                            for (auto& mb : g_musicbox_db) {
                                                if (mb.mapIndex == g_current_map_index && mb.floorIndex == g_current_floor_index) {
                                                    mb.texU = g_pt1_tu;
                                                    mb.texV = g_pt1_tv;
                                                    break;
                                                }
                                            }
                                            SaveConfig();

                                            float dist = sqrtf(dx*dx + dy*dy);
                                            int score = 100;
                                            if (dist < 100.0f) {
                                                score -= (int)((100.0f - dist) * 0.5f);
                                                if (score < 0) score = 0;
                                                char msg[128];
                                                snprintf(msg, sizeof(msg), "校准已保存: %s (精准度: %d%%) - 两点距离过近，精度可能较低", Cfg.name, score);
                                                AddNotification(msg, 4.0f, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                                            } else {
                                                char msg[128];
                                                snprintf(msg, sizeof(msg), "校准已保存: %s (精准度: %d%%)", Cfg.name, score);
                                                AddNotification(msg, 3.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                                            }
                                        }
                                    }
                                }
                            }
                            ImGui::SameLine();
                            ImGui::Checkbox("水平翻转", &g_map_flip_x);
                            ImGui::SameLine();
                            ImGui::Checkbox("垂直翻转", &g_map_flip_y);
                        }
                    }

                    ImGui::Separator();

                    // 预览功能已整合到小地图中，请调整小地图尺寸和位置查看
                    ImGui::TextColored(g_theme.text_muted, "目的地/校准功能已整合到小地图中");

                    ImGui::Separator();
                }

                if (ImGui::CollapsingHeader("配置管理")) {
                    if (StyledButton("添加新地图", ButtonVariant::Primary, ImVec2(0,0), g_density)) { new_map_number = 0; ImGui::OpenPopup("添加新地图##detected"); }
                    ImGui::SameLine();
                    if (StyledButton("清理绘制缓存", ButtonVariant::Secondary, ImVec2(0,0), g_density)) { skipClassCache.clear(); fakeHunterCache.clear(); }

                    if (!map_invalid) {
                        bool any_calib = false;
                        for (int i = 0; i < (int)g_all_maps.size(); i++) {
                            for (int j = 0; j < (int)g_all_maps[i].size(); j++) {
                                auto& cfg = g_all_maps[i][j];
                                if (cfg.calibrated) {
                                    any_calib = true;
                                    ImGui::Text("%s (已校准)", cfg.name);
                                    ImGui::SameLine();
                                    std::string resetBtn = "重置##" + std::to_string(i) + "_" + std::to_string(j);
                                    if (StyledButton(resetBtn.c_str(), ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                                        ImGui::OpenPopup(("确认重置校准##" + std::to_string(i) + "_" + std::to_string(j)).c_str());
                                    }
                                    std::string popupName = "确认重置校准##" + std::to_string(i) + "_" + std::to_string(j);
                                    if (ImGui::BeginPopupModal(popupName.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                                        ImGui::Text("确定要重置 %s 的校准数据吗？此操作不可撤销。", cfg.name);
                                        if (StyledButton("确定", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                                            if (i == 0 && j == 0) { cfg.minX = -548.48f; cfg.maxX = 4730.32f; cfg.minY = -3500.49f; cfg.maxY = 1380.00f; }
                                            else if (i == 0 && j == 1) { cfg.minX = -548.48f; cfg.maxX = 4730.32f; cfg.minY = -3500.49f; cfg.maxY = 1380.00f; }
                                            else { cfg.minX = -500.0f; cfg.maxX = 5000.0f; cfg.minY = -3000.0f; cfg.maxY = 1500.0f; }
                                            cfg.scaleX = 1.0f; cfg.scaleY = 1.0f; cfg.offsetU = 0.0f; cfg.offsetV = 0.0f;
                                            cfg.flipX = false; cfg.flipY = false; cfg.calibrated = false;
                                            SaveConfig();
                                            AddNotification("校准已重置", 2.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                                            ImGui::CloseCurrentPopup();
                                        }
                                        ImGui::SameLine();
                                        if (StyledButton("取消", ButtonVariant::Secondary, ImVec2(0,0), g_density)) ImGui::CloseCurrentPopup();
                                        ImGui::EndPopup();
                                    }
                                    ImGui::SameLine();
                                    std::string viewBtn = "查看##" + std::to_string(i) + "_" + std::to_string(j);
                                    if (StyledButton(viewBtn.c_str(), ButtonVariant::Secondary, ImVec2(0,0), g_density)) ImGui::OpenPopup("校准详情");
                                    if (ImGui::BeginPopup("校准详情")) {
                                        ImGui::Text("边界: %.1f~%.1f, %.1f~%.1f", cfg.minX, cfg.maxX, cfg.minY, cfg.maxY);
                                        ImGui::Text("缩放: %.4f x %.4f", cfg.scaleX, cfg.scaleY);
                                        ImGui::Text("偏移: %.4f, %.4f", cfg.offsetU, cfg.offsetV);
                                        ImGui::Text("翻转: %s %s", cfg.flipX?"X":"", cfg.flipY?"Y":"");
                                        ImGui::EndPopup();
                                    }
                                }
                            }
                        }
                        if (!any_calib) ImGui::TextDisabled("暂无已校准地图");
                    }
                }

                static bool show_restart_confirm = false;
                if (!show_restart_confirm) {
                    if (StyledButton("初始化 (城堡专用)", ButtonVariant::Danger, ImVec2(0,0), g_density)) ImGui::OpenPopup("确认初始化");
                }
                if (ImGui::BeginPopupModal("确认初始化", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("确定要初始化吗？这将重启程序。");
                    if (StyledButton("确定", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                        SaveConfig();
                        char exe_path[256] = {0};
                        if (readlink("/proc/self/exe", exe_path, sizeof(exe_path)) > 0) {
                            char *argv[] = { exe_path, nullptr };
                            execv(exe_path, argv);
                        }
                        exit(0);
                    }
                    ImGui::SameLine();
                    if (StyledButton("取消", ButtonVariant::Secondary, ImVec2(0,0), g_density)) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
                break;
#if 0  // 数据管理 — 已禁用，需要时删除 #if 0 行
            case 6:  // 数据管理
            {
                StyledSectionHeader("数据管理", g_theme.text_title, g_density);
                
                // 数据统计
                ImGui::TextColored(g_theme.info, "=== 数据存储状态 ===");
                std::string stats = DataManager::GetDataStats();
                ImGui::TextWrapped("%s", stats.c_str());
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                // 导出数据
                if (StyledButton("📤 导出全部数据", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                    std::string exported = DataManager::ExportAllData();
                    // 导出到文件
                    std::string export_path = MAPS_ROOT "map_config_export.json";
                    std::ofstream ofs(export_path);
                    if (ofs) {
                        ofs << exported;
                        ofs.close();
                        AddNotification("数据已导出到: " + export_path, 3.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                    } else {
                        AddNotification("导出失败: 无法写入 " + export_path, 3.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    }
                }
                ImGui::SameLine();
                if (StyledButton("📥 从备份恢复", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                    if (DataManager::RestoreFromBackup()) {
                        AddNotification("已从备份恢复，请重启脚本应用", 4.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                    } else {
                        AddNotification("恢复失败: 备份文件不存在", 3.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    }
                }
                
                ImGui::Spacing();
                if (StyledButton("💾 立即保存所有数据", ButtonVariant::Success, ImVec2(0,0), g_density)) {
                    SaveConfig();
                    // 立即刷写所有脏数据
                    g_dirty_exits = true; g_dirty_paths = true;
                    FlushDirtyData();
                    SaveSceneObjectsToJSON(g_current_map_index, g_current_floor_index);
                    AddNotification("所有数据已保存", 2.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                }
                
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_muted, "数据目录: " MAPS_ROOT);
                ImGui::TextColored(g_theme.text_muted, "备份文件: map_config.json.bak (自动创建)");
            }
                break;
#endif  // 数据管理
#if 0  // 调试信息 — 已禁用，需要时删除 #if 0 行
            {
                StyledSectionHeader("实时调试信息", g_theme.text_title, g_density);
                
                ImGui::TextColored(g_theme.info, "=== 地图识别状态 ===");
                ImGui::Text("检测阶段: %d", (int)g_detect_phase);
                ImGui::Text("稳定帧数: %d", g_locked_stable_frames);
                ImGui::Text("音乐盒: %s (%.1f, %.1f, %.1f)",
                    g_has_cached_musicbox ? "已缓存" : "未缓存",
                    g_cached_musicbox_pos.X, g_cached_musicbox_pos.Y, g_cached_musicbox_pos.Z);
                ImGui::Text("钢琴: %s", g_has_cached_piano ? "已缓存" : "未缓存");
                ImGui::Text("凳子数: %zu", g_cached_chairs.size());
                ImGui::Text("识别状态: %s", g_detect_status_text);
                ImGui::Text("评分调试: %s", g_score_debug_buf);
                ImGui::TextColored(g_theme.text_muted, "%s", g_map_detect_debug);
                ImGui::Text("目的地列表: %zu", 
                    g_current_map_index < g_exits.size() && g_current_floor_index < g_exits[g_current_map_index].size()
                    ? g_exits[g_current_map_index][g_current_floor_index].size() : 0);
                ImGui::Text("路径数: %zu", g_saved_paths.size());
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                ImGui::TextColored(g_theme.info, "=== 玩家坐标 ===");
                ImGui::Text("玩家位置: (%.1f, %.1f, %.1f)", Z.X, Z.Y, Z.Z);
                ImGui::Text("当前地图: [%d]", g_current_map_index);
                ImGui::Text("当前楼层: [%d]", g_current_floor_index);
                if (g_current_map_index >= 0 && g_current_map_index < (int)g_all_maps.size() && 
                    g_current_floor_index < (int)g_all_maps[g_current_map_index].size()) {
                    ImGui::Text("地图名称: %s", g_all_maps[g_current_map_index][g_current_floor_index].name);
                    ImGui::Text("纹理路径: %s", g_all_maps[g_current_map_index][g_current_floor_index].texturePath);
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                ImGui::TextColored(g_theme.info, "=== 玩家识别诊断 ===");
                ImGui::Text("扫描对象总数: %d", g_debug_scanned_count);
                ImGui::Text("求生者(阵营2): %d", g_debug_player_count);
                ImGui::Text("监管者(阵营1): %d", g_debug_boss_count);
                ImGui::Text("自识别结果: %s", g_debug_self_found ? "✅ 成功" : "❌ 失败");
                ImGui::Text("识别的类名: %s", g_debug_self_cls);
                ImGui::Text("自识别地址: 0x%lx", (unsigned long)GlobalMemory::自身);
                ImGui::Text("最后一个 cam_z: %.2f", g_debug_last_cam_z);
                ImGui::Text("matrix[15]: %.4f", matrix[15]);
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::Text("游戏状态: %d", GlobalMemory::状态);
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                ImGui::TextColored(g_theme.info, "=== 渲染调试 ===");
                ImGui::Checkbox("显示出口调试十字", &g_show_exit_debug);
                // 显示通行网络已移除
                if (g_show_exit_debug) {
                    ImGui::Text("出口点击位置: (%.0f, %.0f)", g_last_exit_screen_pos.x, g_last_exit_screen_pos.y);
                    ImGui::Text("出口渲染位置: (%.0f, %.0f)", g_last_exit_rendered_pos.x, g_last_exit_rendered_pos.y);
                }
                ImGui::Text("屏幕分辨率: %d x %d", displayInfo.width, displayInfo.height);
                ImGui::Text("地图尺寸: %.0f x %.0f", g_map_display_size, g_map_display_size);
                
                // 清除调试标志
                if (StyledButton("清除出口调试十字", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                    g_show_exit_debug = false;
                }
            }
                break;
#endif  // 调试信息
            case 6:  // 免责声明
                ImGui::PushFont(g_font_ui);
                ImGui::TextColored(g_theme.danger, "免责声明");
                ImGui::PopFont();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(g_theme.text_title, "1. 版权声明");
                ImGui::TextWrapped("本软件（ImGuiOverlay）由 大米饭先生 独立开发。保留所有权利。");
                ImGui::TextWrapped("禁止删除或修改本版权声明和作者信息，以确保所有用户都能了解软件的来源和许可条款。");
                ImGui::TextColored(g_theme.info, "文件来源：Telegram 频道 @MrRice2778");
                ImGui::TextColored(g_theme.text_muted, "侵权联系删除。");
                ImGui::Spacing();

                ImGui::TextColored(g_theme.text_title, "2. 使用许可");
                ImGui::TextWrapped("本软件仅供学习、研究和个人合法使用。");
                ImGui::TextColored(g_theme.warning, "下载后请在 24 小时内删除！");
                ImGui::TextWrapped("禁止将本软件用于任何非法用途。");
                ImGui::Spacing();

                ImGui::TextColored(g_theme.text_title, "3. 免责条款");
                ImGui::TextWrapped("本软件按\"原样\"提供，开发者不承担任何明示或暗示的担保，不对使用本软件导致的任何直接或间接损失负责，包括但不限于：设备损坏、数据丢失、账号封禁、法律纠纷。使用本软件即表示您同意承担所有风险。");
                ImGui::Spacing();

                ImGui::TextColored(g_theme.text_title, "4. 传播限制");
                ImGui::TextWrapped("禁止将本软件传播至任何大陆平台（包括但不限于：哔哩哔哩、抖音、快手、微信公众号、知乎、贴吧等），否则后果自负！");
                ImGui::Spacing();

                ImGui::TextColored(g_theme.text_title, "5. 联系方式");
                ImGui::TextColored(g_theme.info, "Telegram 频道：@MrRice2778");
                ImGui::TextColored(g_theme.text_muted, "侵权联系请通过 Telegram 联系我们");
                ImGui::Spacing();

                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(g_theme.danger, "下载此文件即代表你同意上述声明。");
                break;
        }
        ImGui::EndChild();

        RestoreImGuiStyle(style_bak);
        g_window = ImGui::GetCurrentWindow();
        ImGui::End();
    }

    // ========== ★ 最小化横条 (菜单折叠 / Volume Down 时显示) ==========
    if (g_minimized_bar_anim > 0.01f) {
        ImDrawList *bar_draw = ImGui::GetForegroundDrawList();
        const float bar_w = 540.0f * g_density;   // ★ 单行宽条，容纳完整文字
        const float bar_h = 42.0f * g_density;    // 单行紧凑高度
        const float bar_round = 12.0f * g_density;

        if (g_bar_custom_pos.x < 0) {
            g_bar_custom_pos.x = (displayInfo.width - bar_w) * 0.5f;
            g_bar_custom_pos.y = 60.0f * g_density;
        }

        ImVec2 bar_mouse = ImGui::GetMousePos();
        ImRect bar_rect(g_bar_custom_pos, ImVec2(g_bar_custom_pos.x + bar_w, g_bar_custom_pos.y + bar_h));

        // ★ 横条交互：点击展开 / 按住拖拽
        static bool bar_pending = false;
        static bool bar_dragging = false;
        static ImVec2 bar_drag_off;
        static float bar_hold_time = 0;
        static ImVec2 bar_press_pos;

        if (ImGui::IsMouseClicked(0) && bar_rect.Contains(bar_mouse)) {
            bar_pending = true;
            bar_dragging = false;
            bar_hold_time = 0;
            bar_press_pos = bar_mouse;
        }

        if (bar_pending) {
            bar_hold_time += ImGui::GetIO().DeltaTime;
            float drag_dist = sqrtf(powf(bar_mouse.x - bar_press_pos.x, 2) + powf(bar_mouse.y - bar_press_pos.y, 2));
            if (drag_dist > 12.0f) {
                bar_dragging = true;
                bar_drag_off = ImVec2(bar_press_pos.x - g_bar_custom_pos.x, bar_press_pos.y - g_bar_custom_pos.y);
                bar_pending = false;
            }
            if (!ImGui::IsMouseDown(0)) {
                if (!bar_dragging && bar_hold_time < 0.35f && drag_dist < 10.0f) {
                    MemuSwitch = true;
                }
                bar_pending = false;
                bar_dragging = false;
            }
        }

        if (bar_dragging) {
            if (ImGui::IsMouseDown(0)) {
                g_bar_custom_pos.x = std::clamp(bar_mouse.x - bar_drag_off.x, 0.0f, displayInfo.width - bar_w);
                g_bar_custom_pos.y = std::clamp(bar_mouse.y - bar_drag_off.y, 0.0f, displayInfo.height - bar_h);
            } else {
                bar_dragging = false;
            }
        }

        int bar_alpha = (int)(g_minimized_bar_anim * 210.0f);
        int bar_border_alpha = (int)(g_minimized_bar_anim * 100.0f);
        int text_alpha = (int)(g_minimized_bar_anim * 245.0f);

        bar_draw->AddRectFilled(bar_rect.Min, bar_rect.Max,
                                IM_COL32(255, 250, 235, bar_alpha), bar_round);
        bar_draw->AddRect(bar_rect.Min, bar_rect.Max,
                          IM_COL32(230, 195, 140, bar_border_alpha), bar_round, 0, 1.5f * g_density);

        // ★ 单行排版: "大米饭先生 · @MrRice2778" — 左白右金 + 分隔点
        const char *bar_title = "大米饭先生";
        const char *bar_sep   = " \xc2\xb7 ";
        const char *tg_text   = "@MrRice2778";
        float bar_font = ImGui::GetFontSize() * 0.95f;
        ImVec2 title_sz = ImGui::CalcTextSize(bar_title);
        ImVec2 sep_sz    = ImGui::CalcTextSize(bar_sep);
        ImVec2 tg_sz     = ImGui::CalcTextSize(tg_text);
        float total_w = title_sz.x + sep_sz.x + tg_sz.x;
        float start_x = bar_rect.Min.x + (bar_w - total_w) * 0.5f;
        float text_y  = bar_rect.Min.y + (bar_h - ImGui::GetFontSize()) * 0.5f;

        bar_draw->AddText(g_font_ui, bar_font,
                          ImVec2(start_x, text_y),
                          IM_COL32(255, 255, 255, text_alpha), bar_title);
        bar_draw->AddText(g_font_ui, bar_font * 0.85f,
                          ImVec2(start_x + title_sz.x, text_y),
                          IM_COL32(210, 185, 145, text_alpha), bar_sep);
        bar_draw->AddText(g_font_ui, bar_font * 0.85f,
                          ImVec2(start_x + title_sz.x + sep_sz.x, text_y + 1.0f * g_density),
                          IM_COL32(60, 45, 30, text_alpha), tg_text);
    }

    // ========== 添加新地图弹窗 ==========
    if (ImGui::BeginPopupModal("添加新地图##detected", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        snprintf(new_name, sizeof(new_name), "地图%d 一楼", std::max(1, new_map_number));
        snprintf(new_texture, sizeof(new_texture), MAPS_ROOT "map%d_floor1.png", std::max(1, new_map_number));
        ImGui::TextColored(g_theme.text_title, "请输入地图编号");
        ImGui::Spacing();
        char num_buf[16]; snprintf(num_buf, sizeof(num_buf), "地图 [%d]", std::max(0, new_map_number));
        ImGui::TextColored(new_map_number > 0 ? g_theme.success : g_theme.warning, "%s", num_buf);
        ImGui::TextColored(g_theme.text_muted, "一楼  %s", new_map_number > 0 ? new_texture : "");
        ImGui::Spacing();
        auto numBtn = [&](int digit) {
            char lbl[4]; snprintf(lbl, sizeof(lbl), "%d", digit);
            if (ImGui::Button(lbl, ImVec2(44 * g_density, 40 * g_density))) {
                if (new_map_number < 999) new_map_number = new_map_number * 10 + digit;
            }
        };
        for (int row = 1; row <= 3; row++) {
            for (int col = 0; col < 3; col++) { int d = row * 3 + col - 2; numBtn(d); if (col < 2) ImGui::SameLine(); }
        }
        if (ImGui::Button("←", ImVec2(44 * g_density, 40 * g_density))) { new_map_number /= 10; }
        ImGui::SameLine(); numBtn(0); ImGui::SameLine();
        if (ImGui::Button("确定", ImVec2(88 * g_density, 40 * g_density))) {
            if (new_map_number > 0) {
                json j; std::ifstream ifs(MAPS_ROOT "map_config.json");
                if (ifs) ifs >> j; else j["maps"] = json::array();
                bool updated = false;
                if (j.contains("maps") && j["maps"].is_array()) {
                    for (auto& existing : j["maps"]) {
                        if (existing.value("name", "") == std::string(new_name) && existing.value("floor", -1) == 0) {
                            existing["music_x"]=new_music_x;existing["music_y"]=new_music_y;existing["music_z"]=new_music_z;
                            existing["piano_x"]=new_piano_x;existing["piano_y"]=new_piano_y;existing["piano_z"]=new_piano_z;
                            if(strlen(new_texture)>0)existing["texture"]=new_texture;
                            existing["floor_z_threshold"]=250.0f;updated=true;break;
                        }
                    }
                }
                if(!updated){
                    json m; m["name"]=new_name;m["floor"]=0;m["texture"]=new_texture;
                    m["minX"]=-500.0f;m["maxX"]=5000.0f;m["minY"]=-3000.0f;m["maxY"]=1500.0f;
                    m["floor_z_threshold"]=250.0f;
                    m["music_x"]=new_music_x;m["music_y"]=new_music_y;m["music_z"]=new_music_z;
                    m["piano_x"]=new_piano_x;m["piano_y"]=new_piano_y;m["piano_z"]=new_piano_z;
                    m["music_texU"]=0.5;m["music_texV"]=0.5;j["maps"].push_back(m);
                }
                try{std::filesystem::copy(MAPS_ROOT"map_config.json",MAPS_ROOT"map_config.json.bak",std::filesystem::copy_options::overwrite_existing);}catch(...){}
                std::ofstream ofs(MAPS_ROOT"map_config.json");ofs<<j.dump(4);
                LoadMapConfigFromJSON();LoadFingerprintDB();RebuildFingerprintMapping();SaveConfig();
                AddNotification(updated?("已更新地图:"+std::string(new_name)):("已添加新地图:"+std::string(new_name)),3.0f,ImVec4(0.3f,1.0f,0.3f,1.0f));
                new_map_number=0;ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("取消",ImVec2(0,40*g_density))){new_map_number=0;ImGui::CloseCurrentPopup();}
        ImGui::Separator();
        ImGui::TextColored(g_theme.text_muted,"音乐盒坐标 (主信号源)");
        ImGui::InputFloat("音乐盒 X",&new_music_x);ImGui::InputFloat("音乐盒 Y",&new_music_y);ImGui::InputFloat("音乐盒 Z",&new_music_z);
        ImGui::TextColored(g_theme.text_muted,"钢琴坐标 (辅助信号源)");
        ImGui::InputFloat("钢琴 X",&new_piano_x);ImGui::InputFloat("钢琴 Y",&new_piano_y);ImGui::InputFloat("钢琴 Z",&new_piano_z);
        ImGui::EndPopup();
    }

    // ========== 大图预览已移除，功能整合到小地图 ==========
    if (false) { (void)0; }

    if (show_mimic_overlay) {
        std::lock_guard<std::mutex> lock(mimic_mutex);
        if (!global_validRoles.empty()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * g_density);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * g_density, 12 * g_density));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(g_theme.bg_panel.x, g_theme.bg_panel.y, g_theme.bg_panel.z, 0.92f));
            ImGui::Begin("MimicOverlayWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
            ImDrawList* mimic_draw = ImGui::GetWindowDrawList();
            ImVec2 mimic_pos = ImGui::GetWindowPos();
            ImVec2 mimic_size = ImGui::GetWindowSize();
            mimic_draw->AddRect(mimic_pos, ImVec2(mimic_pos.x + mimic_size.x, mimic_pos.y + mimic_size.y), IM_COL32(60, 80, 120, 160), 16.0f * g_density, 0, 2.0f);
            bool is_overlay_hovered = ImGui::IsWindowHovered();

            // ========== 高级拖动交互：长按拖动 + 单击/双击识别 ==========
            static double last_tap_time = 0.0;
            static bool is_overlay_dragging = false;
            static bool drag_triggered = false;  // 是否已触发拖动（超过拖动阈值）
            static ImVec2 overlay_drag_start_pos;
            static ImVec2 press_mouse_pos;       // 按下时鼠标位置，用于判断拖动距离
            static double press_time = 0.0;      // 按下时间戳，用于判断长按
            const float drag_threshold = 10.0f;  // 拖动触发的最小移动距离（像素）
            const double long_press_time = 0.25; // 长按触发拖动的时间（秒）

            ImVec2 overlay_pos = ImGui::GetWindowPos();
            ImVec2 overlay_size2 = ImGui::GetWindowSize();
            ImVec2 mouse_pos = ImGui::GetMousePos();

            if (ImGui::IsMouseClicked(0) && is_overlay_hovered) {
                double current_time = ImGui::GetTime();

                // 双击（两次点击间隔 <0.4s）→ 关闭悬浮窗
                if (current_time - last_tap_time < 0.4) {
                    show_mimic_overlay = false;
                    last_tap_time = 0.0;
                    is_overlay_dragging = false;
                    drag_triggered = false;
                } else {
                    // 记录按下信息，等待判断是点击/双击还是拖动
                    last_tap_time = current_time;
                    press_time = current_time;
                    press_mouse_pos = mouse_pos;
                    is_overlay_dragging = true;       // 进入拖动候选状态
                    drag_triggered = false;            // 尚未触发实际拖动
                    overlay_drag_start_pos = ImVec2(mouse_pos.x - overlay_pos.x, mouse_pos.y - overlay_pos.y);
                }
            }

            if (is_overlay_dragging) {
                if (ImGui::IsMouseDown(0)) {
                    // 计算手指/鼠标移动距离
                    float dx = mouse_pos.x - press_mouse_pos.x;
                    float dy = mouse_pos.y - press_mouse_pos.y;
                    float moveDist = sqrtf(dx*dx + dy*dy);
                    double holdTime = ImGui::GetTime() - press_time;

                    // 长按超过阈值 或 移动距离超过阈值 → 触发实际拖动
                    if (!drag_triggered && (holdTime > long_press_time || moveDist > drag_threshold)) {
                        drag_triggered = true;
                    }

                    if (drag_triggered) {
                        // 平滑跟随：直接用鼠标位置 - 起始偏移 = 新窗口位置
                        ImVec2 new_pos = ImVec2(mouse_pos.x - overlay_drag_start_pos.x,
                                                 mouse_pos.y - overlay_drag_start_pos.y);
                        // 边界限制：不能超出屏幕可视区域
                        new_pos.x = std::clamp(new_pos.x, 0.0f, displayInfo.width - overlay_size2.x);
                        new_pos.y = std::clamp(new_pos.y, 0.0f, displayInfo.height - overlay_size2.y);
                        ImGui::SetWindowPos(new_pos);
                    }
                } else {
                    // 松手
                    if (!drag_triggered) {
                        // 短按且未拖动→视为点击操作（保持窗口打开，不关闭不拖动）
                        // 如果距上次点击已超过双击间隔则认为是一次单击，不处理
                    }
                    is_overlay_dragging = false;
                    drag_triggered = false;
                }
            }

            for (const auto &r : global_validRoles) {
                std::string roleName = RoleIdToChinese(r.roleId);
                const char *campStr = r.campId == 1 ? "侦探团" : (r.campId == 2 ? "狼  人" : "神秘客");
                ImVec4 color;
                if (r.campId == 1) color = ImVec4(0.2f, 0.8f, 0.8f, 1.0f);
                else if (r.campId == 2) color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                else color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                ImGui::TextColored(color, "[%02d]号 | 阵营: %-6s | 身份: %s", r.index + 1, campStr, roleName.c_str());
            }
            if (is_overlay_dragging && drag_triggered) {
                if (ImGui::IsMouseDown(0)) {
                    ImVec2 new_pos = ImVec2(mouse_pos.x - overlay_drag_start_pos.x, mouse_pos.y - overlay_drag_start_pos.y);
                    new_pos.x = std::clamp(new_pos.x, 0.0f, displayInfo.width - overlay_size2.x);
                    new_pos.y = std::clamp(new_pos.y, 0.0f, displayInfo.height - overlay_size2.y);
                    ImGui::SetWindowPos(new_pos);
                }
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }
    }

    if (main_thread_flag && !*main_thread_flag) {
        CloseDebugLog();
    }

    // === 定期自动保存配置（每300帧 ≈ 5秒） ===
    {
        static int auto_save_counter = 0;
        auto_save_counter++;
        if (auto_save_counter >= 300) {
            auto_save_counter = 0;
            SaveConfig();
        }
    }
}
