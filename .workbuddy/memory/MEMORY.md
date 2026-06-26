# ImGuiOverlay 项目记忆

## 关键 Bug 历史

### v1.5 - g_all_maps 索引越界 (2026-06-27)
- **现象**: 评分正确识别地图（如 map17），但显示"地图2"
- **根因**: JSON 解析中每个 floor 创建独立 g_all_maps slot，奇数 slot 只有 1 层（size=1）。`GetFloorFromPlayerZ(Z)` 返回 1 时，`g_all_maps[odd][1]` 越界读取相邻 slot
- **修复**: `SafeClampFloorIdx()` 函数钳制楼层索引到 `g_all_maps[idx].size() - 1`
- **涉及**: `LoadMapTexture`, `Draw_MapOverlay`, `TryAutoDetectMap` 中 3 处, UI 显示

### v1.4 - 120 分制评分系统
- 4 维度评分: 音乐盒 40pt, 凳子数量 20pt, 凳子位置 30pt, 钢琴位置 30pt
- 阈值 65/120
- 并列保护: 多地图同分时不切换
- 调试显示: `g_map_scores_buf` 显示 Top3 + `[idx]` 索引

### v1.3 - 楼层检测修复
- `GetFloorFromPlayerZ(Z > 190) → 1` 而非硬编码 0

## 常见问题
- `SafeClampFloorIdx` 在访问 `g_all_maps` 前必须调用
- JSON 多楼层: 偶数 slot 有 2 层（含 auto-created），奇数 slot 只有 1 层
