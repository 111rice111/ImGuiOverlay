# v2.0 全面重构完成

## 变更概况

### 架构重构
- **Structs.h** — 共享数据结构头文件（Vector3A, MapConfig, PathGraph 等）
- **DataManager.h/cpp** — 数据管理层，统一 JSON 存取
- **CoordTransform** 命名空间 — 统一坐标转换管道

### 7 个核心 Bug 修复
| # | Bug | 修复方案 |
|---|-----|----------|
| 1 | 路径数据重启消失 | LoadMapConfigFromJSON 不再误清除数据 |
| 2 | 坐标映射偏移 | 全部改用 CoordTransform 统一管道 |
| 3 | 出口拖拽与地图拖动冲突 | 直接检查 g_drag_exit_idx，移除 g_map_drag_blocked |
| 4 | 地图识别误报(音乐盒) | 新增 g_locked_stable_frames ≥30 帧防抖 |
| 5 | text1_pos/title_size 作用域 | 移入正确的 if 块内 |
| 6 | 3处重复坐标变换代码 | 替换为 CoordTransform 调用 |
| 7 | 缺少地图/楼层标识 | 小地图左上角半透明标签 |

### 新增标签页
- **数据管理** — 数据统计、导出、备份恢复
- **调试信息** — 实时坐标、识别状态、渲染调试

### UI 优化
- 路径列表"X"改"删除" + 选中高亮★
- 每个标签页统一主题风格

### 构建
- ✅ BUILD SUCCESSFUL（clean build, 0 error）
- 需手动: adb push + git commit + git push
