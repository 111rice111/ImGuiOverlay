// ============================================================
// draw_UITheme.cpp — 现代 UI 主题系统（draw_Gui 拆分 v2.47）
// 本文件由 draw_Gui.cpp 拆分而来，包含：
//   - InitModernUITheme / Lerp / LerpColor
//   - BackupImGuiStyle / RestoreImGuiStyle / ApplyModernUIStyle
//   - StyledButton / StyledSectionHeader / StyledCardBegin / StyledCardEnd
// 全局变量、struct 定义（StyleBackup）与 enum 定义（ButtonVariant）
// 已移至 draw_Gui_internal.h 头文件中声明。
// ============================================================

#include "draw_Gui_internal.h"

// ============================================================
//  现代 UI 主题系统：统一配色、字体层级、间距、圆角、动效
// ============================================================
void InitModernUITheme() {
    // 大米花先生 — 暖金宣纸半透明主题 ★ 卡通暖调 + 高透明
    g_theme.bg_dark         = ImVec4(0.98f, 0.95f, 0.88f, 0.42f);
    g_theme.bg_panel        = ImVec4(0.99f, 0.96f, 0.90f, 0.38f);
    g_theme.bg_card         = ImVec4(0.96f, 0.92f, 0.84f, 0.36f);
    g_theme.bg_card_hover   = ImVec4(0.94f, 0.88f, 0.78f, 0.42f);
    g_theme.bg_input        = ImVec4(0.93f, 0.88f, 0.80f, 0.58f);   // ★ 从0.35提至0.58, 控件背景可见
    g_theme.bg_hover        = ImVec4(0.90f, 0.84f, 0.74f, 0.65f);   // ★ 从0.40提至0.65
    g_theme.bg_active       = ImVec4(0.86f, 0.78f, 0.66f, 0.72f);   // ★ 从0.48提至0.72
    g_theme.bg_overlay      = ImVec4(0.00f, 0.00f, 0.00f, 0.18f);

    // ★ 暖金色 — 更亮更像参考图
    g_theme.primary         = ImVec4(0.95f, 0.78f, 0.22f, 0.90f);
    g_theme.primary_hover   = ImVec4(1.00f, 0.86f, 0.28f, 0.95f);
    g_theme.primary_active  = ImVec4(0.85f, 0.66f, 0.10f, 1.00f);
    g_theme.primary_soft    = ImVec4(0.95f, 0.78f, 0.22f, 0.18f);

    // 嫩绿勾选（更鲜艳）
    g_theme.success         = ImVec4(0.20f, 0.72f, 0.30f, 0.95f);
    g_theme.success_hover   = ImVec4(0.24f, 0.66f, 0.30f, 0.96f);
    g_theme.success_active  = ImVec4(0.12f, 0.42f, 0.16f, 0.98f);

    // 温暖红
    g_theme.danger          = ImVec4(0.82f, 0.22f, 0.18f, 0.92f);
    g_theme.danger_hover    = ImVec4(0.92f, 0.28f, 0.24f, 0.96f);
    g_theme.danger_active   = ImVec4(0.68f, 0.16f, 0.12f, 0.98f);

    // 琥珀黄
    g_theme.warning         = ImVec4(0.92f, 0.58f, 0.10f, 0.95f);
    g_theme.info            = ImVec4(0.30f, 0.52f, 0.80f, 0.95f);

    // 深棕文字 — 半透明白底上高对比
    g_theme.text            = ImVec4(0.16f, 0.12f, 0.08f, 1.00f);
    g_theme.text_muted      = ImVec4(0.50f, 0.44f, 0.36f, 0.90f);
    g_theme.text_title      = ImVec4(0.12f, 0.08f, 0.04f, 1.00f);
    g_theme.text_on_primary = ImVec4(0.55f, 0.36f, 0.05f, 1.00f);

    // 暖灰边框 — 低可见度
    g_theme.border          = ImVec4(0.62f, 0.50f, 0.36f, 0.45f);
    g_theme.border_strong   = ImVec4(0.52f, 0.38f, 0.24f, 0.60f);
    g_theme.border_light    = ImVec4(0.78f, 0.72f, 0.62f, 0.35f);

    // 勾选/滑块
    g_theme.check_mark      = ImVec4(0.18f, 0.74f, 0.28f, 1.00f);
    g_theme.slider_grab     = ImVec4(0.92f, 0.74f, 0.18f, 1.00f);
    g_theme.slider_grab_active = ImVec4(0.94f, 0.75f, 0.20f, 1.00f);
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }
ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t));
}

StyleBackup BackupImGuiStyle() {
    StyleBackup bak;
    ImGuiStyle& s = ImGui::GetStyle();
    bak.WindowRounding = s.WindowRounding; bak.FrameRounding = s.FrameRounding; bak.ChildRounding = s.ChildRounding;
    bak.PopupRounding = s.PopupRounding; bak.ScrollbarRounding = s.ScrollbarRounding; bak.GrabRounding = s.GrabRounding; bak.TabRounding = s.TabRounding;
    bak.WindowPadding = s.WindowPadding; bak.FramePadding = s.FramePadding; bak.ItemSpacing = s.ItemSpacing; bak.ItemInnerSpacing = s.ItemInnerSpacing;
    bak.CellPadding = s.CellPadding; bak.TouchExtraPadding = s.TouchExtraPadding;
    bak.IndentSpacing = s.IndentSpacing; bak.ScrollbarSize = s.ScrollbarSize;
    memcpy(bak.Colors, s.Colors, sizeof(s.Colors));
    return bak;
}

void RestoreImGuiStyle(const StyleBackup& bak) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = bak.WindowRounding; s.FrameRounding = bak.FrameRounding; s.ChildRounding = bak.ChildRounding;
    s.PopupRounding = bak.PopupRounding; s.ScrollbarRounding = bak.ScrollbarRounding; s.GrabRounding = bak.GrabRounding; s.TabRounding = bak.TabRounding;
    s.WindowPadding = bak.WindowPadding; s.FramePadding = bak.FramePadding; s.ItemSpacing = bak.ItemSpacing; s.ItemInnerSpacing = bak.ItemInnerSpacing;
    s.CellPadding = bak.CellPadding; s.TouchExtraPadding = bak.TouchExtraPadding;
    s.IndentSpacing = bak.IndentSpacing; s.ScrollbarSize = bak.ScrollbarSize;
    memcpy(s.Colors, bak.Colors, sizeof(bak.Colors));
}

void ApplyModernUIStyle(float density) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 24.0f * density;   // ★ 大圆角 — 宣纸卡片感
    s.ChildRounding     = 18.0f * density;
    s.FrameRounding     = 12.0f * density;
    s.PopupRounding     = 20.0f * density;
    s.ScrollbarRounding = 14.0f * density;
    s.GrabRounding      = 10.0f  * density;
    s.TabRounding       = 12.0f * density;

    s.WindowPadding     = ImVec2(20.0f * density, 16.0f * density);
    s.FramePadding      = ImVec2(12.0f * density, 7.0f  * density);
    s.ItemSpacing       = ImVec2(12.0f * density, 10.0f * density);
    s.ItemInnerSpacing  = ImVec2(8.0f  * density, 5.0f  * density);
    s.CellPadding       = ImVec2(10.0f * density, 7.0f  * density);
    s.TouchExtraPadding = ImVec2(6.0f  * density, 6.0f  * density);
    s.IndentSpacing     = 22.0f * density;
    s.ScrollbarSize     = 32.0f * density;   // 触摸热区 ≈44px (@1.0密度), 适配无障碍标准
    s.ScrollbarRounding = 16.0f * density;    // 圆角滚动条更顺滑
    s.AntiAliasedLines  = true;
    s.AntiAliasedFill   = true;
    s.FrameBorderSize   = 1.2f * density;      // ★ 为所有 Frame 组件添加描边（复选框/滑块/颜色编辑）

    s.Colors[ImGuiCol_Text]                 = g_theme.text;
    s.Colors[ImGuiCol_TextDisabled]         = g_theme.text_muted;
    s.Colors[ImGuiCol_WindowBg]             = g_theme.bg_dark;
    s.Colors[ImGuiCol_ChildBg]              = g_theme.bg_panel;
    s.Colors[ImGuiCol_PopupBg]               = g_theme.bg_card;
    s.Colors[ImGuiCol_Border]                = g_theme.border;
    s.Colors[ImGuiCol_BorderShadow]          = ImVec4(0,0,0,0);
    s.Colors[ImGuiCol_FrameBg]               = g_theme.bg_input;
    s.Colors[ImGuiCol_FrameBgHovered]        = g_theme.bg_hover;
    s.Colors[ImGuiCol_FrameBgActive]         = g_theme.bg_active;
    s.Colors[ImGuiCol_TitleBg]               = g_theme.bg_panel;
    s.Colors[ImGuiCol_TitleBgActive]         = g_theme.bg_panel;
    s.Colors[ImGuiCol_TitleBgCollapsed]      = g_theme.bg_panel;
    s.Colors[ImGuiCol_MenuBarBg]             = g_theme.bg_panel;
    s.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.85f, 0.82f, 0.78f, 0.30f);
    s.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.70f, 0.62f, 0.50f, 0.55f);
    s.Colors[ImGuiCol_ScrollbarGrabHovered]  = g_theme.primary;
    s.Colors[ImGuiCol_ScrollbarGrabActive]   = g_theme.primary_active;
    s.Colors[ImGuiCol_CheckMark]             = g_theme.check_mark;
    s.Colors[ImGuiCol_SliderGrab]            = g_theme.slider_grab;
    s.Colors[ImGuiCol_SliderGrabActive]      = g_theme.slider_grab_active;
    s.Colors[ImGuiCol_Button]                = g_theme.primary;
    s.Colors[ImGuiCol_ButtonHovered]         = g_theme.primary_hover;
    s.Colors[ImGuiCol_ButtonActive]          = g_theme.primary_active;
    s.Colors[ImGuiCol_Header]                = g_theme.bg_hover;
    s.Colors[ImGuiCol_HeaderHovered]         = g_theme.bg_active;
    s.Colors[ImGuiCol_HeaderActive]          = g_theme.primary_active;
    s.Colors[ImGuiCol_Separator]             = g_theme.border;
    s.Colors[ImGuiCol_SeparatorHovered]      = g_theme.border_strong;
    s.Colors[ImGuiCol_SeparatorActive]       = g_theme.primary;
    s.Colors[ImGuiCol_ResizeGrip]            = g_theme.border_strong;
    s.Colors[ImGuiCol_ResizeGripHovered]     = g_theme.primary;
    s.Colors[ImGuiCol_ResizeGripActive]       = g_theme.primary_active;
    s.Colors[ImGuiCol_Tab]                   = g_theme.bg_card;
    s.Colors[ImGuiCol_TabHovered]            = g_theme.bg_hover;
    s.Colors[ImGuiCol_TabActive]             = g_theme.primary;
    s.Colors[ImGuiCol_TabUnfocused]          = g_theme.bg_card;
    s.Colors[ImGuiCol_TabUnfocusedActive]    = g_theme.bg_active;
    s.Colors[ImGuiCol_PlotLines]             = g_theme.primary;
    s.Colors[ImGuiCol_PlotLinesHovered]      = g_theme.primary_hover;
    s.Colors[ImGuiCol_PlotHistogram]         = g_theme.primary;
    s.Colors[ImGuiCol_PlotHistogramHovered]  = g_theme.primary_hover;
    s.Colors[ImGuiCol_TextSelectedBg]        = g_theme.primary_active;
    s.Colors[ImGuiCol_DragDropTarget]        = g_theme.warning;
    s.Colors[ImGuiCol_NavHighlight]          = g_theme.primary;
    s.Colors[ImGuiCol_NavWindowingHighlight] = g_theme.primary;
    s.Colors[ImGuiCol_NavWindowingDimBg]     = g_theme.bg_overlay;
    s.Colors[ImGuiCol_ModalWindowDimBg]      = g_theme.bg_overlay;
}

// 按钮变体：主按钮、成功、危险、次要

bool StyledButton(const char* label, ButtonVariant variant, const ImVec2& size, float density) {
    ImVec4 bg, hover, active, text;
    switch (variant) {
        case ButtonVariant::Success:
            bg = g_theme.success; hover = g_theme.success_hover; active = g_theme.success_active; text = g_theme.text_on_primary;
            break;
        case ButtonVariant::Danger:
            bg = g_theme.danger; hover = g_theme.danger_hover; active = g_theme.danger_active; text = g_theme.text_on_primary;
            break;
        case ButtonVariant::Secondary:
            bg = g_theme.bg_card; hover = g_theme.bg_hover; active = g_theme.bg_active; text = g_theme.text;
            break;
        default:
            bg = g_theme.primary; hover = g_theme.primary_hover; active = g_theme.primary_active; text = g_theme.text_on_primary;
            break;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * density);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * density, 8.0f * density));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return clicked;
}

void StyledSectionHeader(const char* label, const ImVec4& color, float density) {
    ImVec4 c = (color.w > 0.0f) ? color : g_theme.text_title;
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::PushFont(g_font_ui);
    ImGui::Text("%s", label);
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

void StyledCardBegin(const char* id, const ImVec2& size, float density, ImDrawList* draw_list, ImVec2& out_pos, ImVec2& out_size) {
    ImGui::BeginChild(id, size, false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    out_pos = ImGui::GetWindowPos();
    out_size = ImGui::GetWindowSize();
    draw_list->AddRectFilled(out_pos, ImVec2(out_pos.x + out_size.x, out_pos.y + out_size.y),
                             IM_COL32(255, 253, 245, 65), 14.0f * density);
    draw_list->AddRect(out_pos, ImVec2(out_pos.x + out_size.x, out_pos.y + out_size.y),
                       IM_COL32(230, 210, 180, 38), 14.0f * density, 0, 1.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * density);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f * density);
}

void StyledCardEnd() {
    ImGui::EndChild();
}
