# 自动识别地图模块 — 重构设计方案

## 一、问题分析

### 现有架构的三大缺陷

1. **识别触发机制错误**
   - 当前：每帧扫描物体坐标 → 评分 → 切换地图
   - 问题：地图切换时物体坐标呈现"瞬移"假象，评分系统误判为"物体被移动"
   
2. **无地图切换事件检测**
   - 没有主动检测玩家传送/跳转事件
   - 依赖物体位置的连续性来做判断，这是根本性的设计缺陷

3. **状态管理混乱**
   - 11 个全局变量交织管理，`g_musicbox_moved_on_same_map` 等变量逻辑脆弱
   - 评分与切换逻辑耦合，边界条件太多

### 根本原因
```
地图切换时游戏的行为:
  第1帧: 玩家在旧地图坐标 (X=100, Y=200, Z=50)
  第2帧: 游戏正在加载新地图，玩家坐标 (X=0, Y=0, Z=0) (过渡)
  第3帧: 玩家在新地图坐标 (X=3000, Y=4000, Z=160)  ← 跳了 ~5000 单位
  第4帧: 物体在新场景中出现

当时评分系统看到"新坐标"的物体（音乐盒、凳子等），
试图匹配旧地图的数据库 → 匹配失败
然后发现物体"位置变了" → 误报"音乐盒被移动"
```

## 二、新架构设计

### 核心原则
> **"先检测传送，再识别地图"**
> 地图切换是原因，物体坐标变化是结果

### 状态机设计

```
                  ┌─────────────────────────┐
                  │       LOCKED (锁定)       │ ← 正常运行
                  │   评分持续确认当前地图    │
                  └──────┬──────────────────┘
                         │ 玩家坐标大幅跳跃 (>100)
                         │ Z轴跨越楼层阈值 (>190)
                         v
                  ┌─────────────────────────┐
                  │   SWITCH_DETECTED (检测)  │ ← 检测到切换
                  │   立即触发识别流程        │
                  └──────┬──────────────────┘
                         │ 收集当前帧物体数据
                         v
                  ┌─────────────────────────┐
                  │   IDENTIFYING (识别中)    │ ← 评分进行中
                  │   防抖计时 (200ms)       │
                  └──────┬──────────────────┘
                         │
              ┌──────────┴──────────┐
              │                     │
              v                     v
    ┌─────────────────┐   ┌─────────────────┐
    │   CONFIRMED     │   │ LOW_CONFIDENCE  │ ← 评分<65
    │   评分≥65且唯一  │   │   等待下帧确认   │
    └──────┬──────────┘   └──────┬──────────┘
           │                     │ 连续3帧后仍低
           v                     v
    ┌─────────────────┐   ┌─────────────────┐
    │ 切换到新地图     │   │ 保持当前地图     │
    │ 清空所有误判标记  │   │ 等待下次切换事件  │
    └──────┬──────────┘   └─────────────────┘
           │
           v
    ┌─────────────────┐
    │    LOCKED       │
    │ 正常运行时每周   │
    │ 期做信号验证     │
    └─────────────────┘
```

### 数据结构设计

```cpp
// === 新：地图指纹（从 JSON 文件加载）===
struct MapFingerprint {
    int id;           // 唯一 ID（来自 JSON）
    Vector3A musicBox; // 音乐盒坐标
    std::vector<Vector3A> stools;  // 凳子坐标列表
    std::vector<Vector3A> pianos;  // 钢琴坐标列表（0~1架）
};

// === 新：指纹→g_all_maps 索引映射 ===
// 因为 g_all_maps 是按文件顺序排列的（map1, map2, ...）
// 而指纹 ID 不一定连续
// 使用双向映射:
//   g_mapidx_from_fp_id[fp_id] = g_all_maps 中的索引
//   g_fp_id_from_mapidx[map_idx] = 对应的指纹 ID（-1 表示未关联）
```

### 核心算法

```
每帧调用 DetectMap(current_data, player_pos):

1. 检测传送事件:
   if (abs(player_pos_xy - last_player_pos_xy) > TELEPORT_THRESHOLD (100))
     → 标记 SWITCH_DETECTED，重置所有物体误判标记
   
2. 检测楼层变化:
   if (floor(player_pos_z) != last_floor)
     → 更新楼层索引
     → 如果 Z 大幅变化 (距离上次 > 50) 也触发 SWITCH_DETECTED

3. 根据状态执行:
   
   LOCKED 状态:
   - 常规评分验证当前地图
   - 如果评分持续低于阈值且玩家位置稳定 → 保持LOCKED
   - 如果评分低于阈值但连续多帧 → 标记 LOW_CONFIDENCE（非强制切换）
   
   SWITCH_DETECTED 状态:
   - 立即启动评分流程
   - 如果评分 ≥ 65 且唯一 → CONFIRMED
   - 如果 40 ≤ 评分 < 65 且与第二名差距 ≥ 20 → 差距兜底
   - 设置防抖 = 12帧 (200ms)
   
   IDENTIFYING 状态:
   - 防抖倒计时
   - 如果倒计时结束 → 用最新评分结果做最终决策
   - 如果倒计时期间又检测到新传送 → 重置防抖
   
4. 物体变化处理:
   - 只有在 LOCKED 状态下才检查"物体是否被移动"
   - 在 SWITCH_DETECTED 和 IDENTIFYING 状态下，忽略所有物体变化
   - 地图切换后，重置 g_musicbox_moved_on_same_map = false
```

## 三、变更清单

### 新增
1. `MapFingerprint` 结构体
2. `g_fingerprint_db` 指纹数据库
3. `g_mapidx_from_fp_id` 映射表（可动态扩展）
4. `g_fp_id_from_mapidx` 反向映射
5. `enum MapDetectState` 状态枚举
6. `LoadFingerprintDB()` 加载指纹 JSON
7. 防抖变量 `g_detect_debounce`
8. 全面重写 `TryAutoDetectMap()`
9. 新的地图识别状态机逻辑

### 修改
1. `LoadMapConfigFromJSON()` — 加载时建立指纹映射
2. `Draw_MapOverlay()` — 显示当前识别状态和置信度
3. 地图管理 UI — 显示关联的指纹 ID
4. `SaveExitsToJSON()` / `SavePlayerPathsToJSON()` — 兼容指纹 ID 存储

### 删除（重构后不再需要的变量）
- `g_switch_candidate_index`
- `g_switch_confirm_frames`
- `g_musicbox_moved_on_same_map`（部分保留但简化）
- `g_new_map_detected` / `g_new_map_prompted`（部分保留）
- `g_last_valid_map_index` (整合到状态机中)
- 相关 6~8 个辅助变量

## 四、兼容性保障

### 路径数据兼容
- 路径存储在 `g_saved_paths_by_map[mapIdx][floorIdx]`，索引不变
- `g_all_maps` 的结构不变，只是识别逻辑变了
- 旧 `map_config.json` 中的路径依然按 `mapIdx` 索引

### 配置兼容
- `overlay_config.txt` 中的校准参数按 `map_idx_floor` 索引，不变
- 重构只改识别逻辑，不改配置文件格式

### 指纹 JSON 整合
- 指纹数据来源：`音乐盒凳子.txt` 或其他指定的 JSON 文件
- 启动时加载到 `g_fingerprint_db`
- 建立 `fp_id <-> mapIdx` 映射关系
- 用户添加新地图时自动关联指纹
