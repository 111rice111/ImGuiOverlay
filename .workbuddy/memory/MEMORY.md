# ImGuiOverlay 项目记忆

## ★ 部署路径约定 (2026-06-29)
- **二进制**: `/data/local/bin/overlay`（chmod 777）
- **编译产物路径**: `E:\ImGuiOverlay\app\build\intermediates\cxx\RelWithDebInfo\6u3a2a4x\obj\arm64-v8a\overlay`（native ELF，不是 APK！）
- **数据目录**: `/data/local/bin/maps/`
- **配置**: `/data/local/bin/overlay_config.txt`
- **推送方式**: `adb push → /sdcard/overlay_tmp → su cp → /data/local/bin/`（/data/local/bin/ 不能直接 adb push）
- **禁令**: 禁止使用 `/data/local/tmp/`（SELinux tmpfs:s0 阻止执行，且反作弊高频命中）
- **⚠️ 禁止推送 APK**: `app-release-unsigned.apk` 是 ZIP 包不能执行，必须推送 `cxx/.../obj/arm64-v8a/overlay`

## ★ v2.13-stable — 路径导入功能 (2026-06-29 21:36)

二进制: `E:\ImGuiOverlay\overlay-v2.13-stable` (MD5: 356479c671652fa56d290228d00ea2fd)

### v2.13 新增
- **Tab 7「路径导入」**: 从 extract_paths.py 生成的像素坐标 txt 导入世界坐标导航线
  - 像素→世界转换: `UV = (px/texW, py/texH)` → `CoordTransform::UVToWorld(u, v, z, cfg)`
  - 黄色虚线预览 (在小地图确认)
  - 确认导入后持久化到 map_config.json + 参与 PathGraph 寻路
- **纹理尺寸存储**: `g_map_texture_w/h[MAX_MAP_COUNT][MAX_FLOOR_COUNT]`
  - 在 FlushTextures() 上传纹理时记录
  - ResetMapState() / InvalidateMapTextures() 时重置
- **Tab 编号变更**: 7→路径导入, 8→调试信息, 9→关于
- **完整工作流**: extract_paths.py → txt → adb push → Tab 7 加载预览 → 确认导入

## ★ v2.12-stable — 启动画面品牌重命名 (2026-06-29 20:22)
Git commit: dee1c56

二进制: `E:\ImGuiOverlay\overlay-v2.12-stable` (MD5: 09d19bd6b23fff7f58437a9fad0e8853)

### v2.12 变更
- main.cpp 启动画面从简单的 "系统初始化中..." 改为品牌横幅：
  - 标题：**大米饭先生**
  - Telegram 链接：https://t.me/+67uRf9NT_04xMGM1
- 系统状态提示保留在横幅下方

## ★ v2.10 动作ID多链扫描 (2026-06-29)
Git commit: ff6784e

- draw_Gui.cpp case 7 调试标签页新增"动作ID多链扫描"面板
- 20条待测指针链，从 GlobalMemory::自身 扫描
- 绿色高亮与当前链匹配的链，灰色显示断链
- 需与哈基米 v2.3 对比验证找到正确链

---

## ★ v2.16-stable — 当前稳定版 (2026-06-30 15:10)

二进制: `E:\ImGuiOverlay\overlay-v2.16-stable` (MD5: b5575c306182e8e5ee74fda6d0082c9e)
Git commit: c36341de, tag: v2.16-stable

### v2.16 vs v2.9 差异
| 修复 | 说明 |
|------|------|
| 板子扫描 | `is_woodplane` 加回主过滤器 + 独立分支 |
| 自身检测 | 绕过 `self_is_survivor`（GM::自身指向错误对象） |
| 触摸校准 | A=1.00334,B=0,C=-1.67,D=0,E=-1,F=2400 |
| 判定框 | 钻石切面: 极淡银白+三层光晕+银边+四角亮点 |
| 诊断面板 | 实时显示监管者/板子/判区/冷却 |

### v2.16 已知问题
- GlobalMemory::自身 指向错误对象（self_is_survivor 已注释）
- 诊断面板需正式发布前隐藏

## ★ v2.9-stable — 上一稳定版 (2026-06-27 18:47)

二进制: `E:\ImGuiOverlay\overlay-v2.9-stable` (MD5: 6425eaa414dc76d6fad72c662d599400)
源码备份: `E:\ImGuiOverlay\draw_Gui.cpp-v2.9-backup`
Git commit: 4130378, tag: v2.9-stable

### v2.9 核心修复清单
| 修复 | 说明 |
|------|------|
| ★ 指纹映射覆盖 | `g_mapidx_from_fp_id` 只取首次匹配，防止 JSON 中"二楼"条目覆盖"一楼" |
| SWITCH 冷却 | 2秒（120帧），防瞬移6连触发 → segfault |
| LOW_CONFIDENCE 死循环 | fp_id 无映射时留在 LOW_CONFIDENCE 等超时，不再跳 LOCKED |
| 首次检测预检 | ExecuteMapSwitch 前检查 g_mapidx_from_fp_id |
| 防抖自动楼层 | 30帧（0.5s）持续在对面才切换，避免楼梯抖动 |
| SWITCH 楼层安全 | g_current_map_index < 0 时跳过楼层更新 |

### v2.9 已知注意事项
- JSON 中同一地图不应有多个独立条目（"一楼"和"二楼"应合并为一个 slot 的两层）
- 指纹映射只在 LoadFingerprintDB 时建立，新地图需重启 overlay 或手动刷新

---

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

## v2.0 架构最佳实践 (2026-06-27)
- **Structs.h**: 所有共享数据结构定义在这里（Vector3A, MapConfig, PathGraph 等）
- **DataManager.h/cpp**: 所有 JSON 存取操作封装在此类中
- **CoordTransform**: 世界↔UV 坐标转换必须使用此命名空间，禁止在其他地方重复实现
- **GetActiveMapConfig()**: 在 g_all_maps 声明后定义，带全局回退参数
- **出口拖拽冲突修复**: 直接检查 `g_drag_exit_idx >= 0` 阻止地图拖动，而非使用 g_map_drag_blocked
- **地图识别防抖**: `g_locked_stable_frames ≥ 30` 后才检测音乐盒移动

## 交付流程（必须遵守）
1. **修改代码前**：先参考 `.agents/skills/` 中 mattpocock-skills 的工作流（diagnose → plan → implement）
2. 修改代码 → 编译（`./gradlew clean assembleRelease`）
3. adb push 编译产物的 **native ELF** 到手机（`cxx/.../obj/arm64-v8a/overlay`，不是 APK！），推至 `/data/local/bin/overlay`（**chmod 777**）
4. 推送 CHANGELOG.txt + version-latest.txt + `[更新日志]` 到手机
5. 更新 CHANGELOG.md
6. git commit + git push

## 最终确定的优化方案

### 1. LOCKED 状态定期重检
- 每 60 帧评分对比，#1 候选 ≥ 当前 + 15 分 且 ≥ 65 分 且 持续 3 秒则切换

### 2. 新版评分系统（110 分制，及格 60 分）
| 特征 | 满分 | 规则 |
|------|:----:|------|
| 音乐盒坐标 | 40 | ≤5→40；>5 线性递减(40-(dist-5)×2)；≥25→0 |
| 凳子数量 | 10 | 相等→10；差1→5；差≥2→0 |
| 凳子位置重合度 | 50 | 贪心最近邻，距离<**4.0**算成功；得分=成功数/已知总数×50 |
| 钢琴位置 | 10 | **纯加分**：没扫到→10；扫到且匹配→按距离给10/5/0；扫到但已知无→0 |

### 3. 同音乐盒变体分组（7 组）
- (4887.87,1043.27,160.82) → 地图2(凳8)、地图23(凳3)
- (4763.27,1292.13,20.80) → 地图7(凳7)、地图24(凳3)
- (4767.87,1583.27,160.82) → 地图10(凳5)、地图12(凳5)、地图25(凳4)
- (4047.87,1883.27,160.82) → 地图13(凳9)、地图26(凳1)
- (3752.13,636.73,230.83) → 地图19(凳8)、地图27(凳7)
- (4103.27,1712.13,160.82) → 地图20(凳7)、地图28(凳5)
- (4527.87,1763.27,160.82) → 地图21(凳7)、地图29(凳6)

### 4. 三层决策流程
- **Layer 1**：音乐盒匹配 → 唯一则直接返回
  - 0 家匹配 → ALL-IN Layer 2（全部指纹）
  - N 家匹配 → 进 Layer 2
- **Layer 2**：孪生/候选比对（核心靠凳子 50 分）
- **Layer 3**：最高分 < 60 → 返回 -1（未知新地图）
