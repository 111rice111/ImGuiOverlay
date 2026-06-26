# 四个 Bug 修复完成

## 问题 1：路径绘制不能弯曲
**根因**：`g_ortho_draw` 默认为 `true`，强制线段水平/垂直，用户无法绘制曲线。

**修复**：
- `g_ortho_draw` 默认值改为 `false` — 现在是自由绘制模式
- 阈值调低：`g_path_draw_threshold` 25→15（更密点，曲线更平滑）
- `g_smooth_strength` 8→4（RDP 简化更少，保留更多曲线细节）
- 菜单中可随时切换正交/自由模式

## 问题 2：绘制路径不可见
**根因**：`Draw_MapOverlay()` 函数的入口条件为 `if (!g_map_enabled || !MjSubsystem::draw_props)`，路径渲染被摸金模式开关挡住了。

**修复**：移除 `!MjSubsystem::draw_props` 条件，路径现在只要地图启用就可见。

## 问题 3：注册新地图按钮过窄
**根因**：按钮使用 `GetContentRegionAvail().x` 宽度，UI 现代化后全局 padding 增大，剩余宽度不够显示完整文字。

**修复**：
- 使用 `g_font_ui` 大字显示
- 根据文字大小计算最小宽度（`text_width + 30*density`），取最大值
- 按钮高度增加到 `42*density`

## 问题 4：拾取音乐盒导致地图变化
**根因**：在 `TryAutoDetectMap()` 中，音乐盒匹配到不同地图时，即使玩家还在当前地图范围内，也会启动 30 帧切换冷却。

**修复**：
- 在匹配到不同地图时，先检查 `IsPlayerInMapBounds(g_last_valid_map_index, Z)`
- 如果玩家还在当前地图 → 直接忽略误匹配，重置冷却，保持地图不变
- 如果玩家确实离开 → 正常启动 30 帧连续确认
- 额外加固：Step 2（音乐盒消失）中重置 `g_frames_since_musicbox_lost` 避免累积超时

## 部署
| 步骤 | 状态 |
|------|------|
| 编译 | ✅ BUILD SUCCESSFUL |
| 推送 | ✅ /data/local/tmp/overlay |
| 权限 | ✅ 777 |
