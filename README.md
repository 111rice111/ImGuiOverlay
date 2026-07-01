# ImGuiOverlay — Android 游戏辅助叠加层

基于 ImGui + OpenGL ES 的 Android 原生叠加层，通过内核驱动内存读写实现对游戏内实体（玩家、物品、宝箱等）的透视渲染、地图导航、自动盖板与路径规划。

---

## 快速开始

### 环境要求

| 项目 | 版本/说明 |
|------|----------|
| Android SDK | API 30+ |
| NDK | r26+ |
| Gradle | 9.x |
| CMake | 3.10+ |
| Java | JDK 17 |
| 手机 | 需要 root 权限 |

### 编译

```bash
cd E:\ImGuiOverlay
./gradlew assembleRelease
```

编译产物：
- **APK**: `app/build/outputs/apk/release/app-release-unsigned.apk`
- **原生二进制**: `app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a/overlay`

> **注意**：只需要部署原生二进制，不需要安装 APK。APK 仅用于触发 NDK 编译。

### 部署到手机

```bash
# 停止旧进程
adb shell "su -c 'killall overlay'"

# 推送二进制（两阶段：先到 /sdcard，再 su cp）
adb push "app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a/overlay" /sdcard/overlay_tmp
adb shell "su -c 'cp /sdcard/overlay_tmp /data/local/bin/overlay && chmod 777 /data/local/bin/overlay && rm /sdcard/overlay_tmp'"

# 启动
adb shell "su -c '/data/local/bin/overlay &'"
```

**路径约定**：
- 二进制：`/data/local/bin/overlay`（必须 chmod 777）
- 数据目录：`/data/local/bin/maps/`
- 配置文件：`/data/local/bin/overlay_config.txt`
- 禁止使用 `/data/local/tmp/`（SELinux tmpfs:s0 阻止执行）

---

## 项目结构

```
ImGuiOverlay/
├── app/
│   ├── build.gradle                          # Gradle 构建配置
│   └── src/main/cpp/
│       ├── CMakeLists.txt                    # CMake Native 编译配置
│       └── src/
│           ├── Android_draw/
│           │   ├── draw_Gui.cpp              # ★ 核心文件（7500+ 行）
│           │   │                             #   - 所有 GUI 标签页
│           │   │                             #   - 实体渲染 (2D/3D ESP)
│           │   │                             #   - 地图系统
│           │   │                             #   - 路径绘制与导航
│           │   │                             #   - 配置存取
│           │   │                             #   - 玩家识别
│           │   ├── Structs.h                 # 共享数据结构定义
│           │   ├── DataManager.h/cpp         # JSON 数据存取封装
│           │   └── CoordTransform.h          # 世界↔UV 坐标转换
│           ├── ImGui/                        # ImGui 库（源码集成）
│           └── main.cpp                      # 入口：Vulkan 初始化 + 启动画面
├── app/build/intermediates/cxx/.../overlay   # 编译产物（native ELF）
└── *.md                                      # 文档
```

---

## 核心架构

### 标签页（9 个 Tab）

| Tab | 名称 | 功能 |
|-----|------|------|
| 0 | 通用设置 | 方框、骨骼、距离、颜色等基础 ESP |
| 1 | 玩家列表 | 局内玩家信息、天赋查看 |
| 2 | 摸金模式 | 精细过滤（怪物/宝箱/陷阱/交互物按需开关） |
| 3 | 绘制管理 | 线条颜色、透明度、显示距离滑块 |
| 4 | 角色切换 | 多角色配置管理 |
| 5 | 地图管理 | 导航地图、路径绘制、地图校准 |
| 6 | 路径导入 | 从外部 txt 导入像素坐标路径 |
| 7 | 调试信息 | 诊断数据显示 |
| 8 | 关于 | 版本信息 + Telegram 链接 |

### 地图系统

地图通过**指纹匹配**自动识别：检测场景中的音乐盒（prop_musicbox）坐标与数据库比对，识别玩家所在的地图和楼层。

地图数据存储在 `map_config.json`，支持：
- 多楼层地图
- UV 坐标与世界坐标双向转换
- 路径数据关联存储

### 路径系统

- **手动画线**：点击地图打点，保存后自动成为路径
- **路径管理**：独立显隐、颜色切换、删除、反向
- **端点吸附**：保存路径时首尾点自动贴近已有路径端点（80 世界单位内）
- **跨路径导航**：设置目的地后，Dijkstra 算法自动扫描连通路网，走最短多路径路线
- **3D 渲染**：游戏世界中立体路径线（带流光脉冲效果）

### 驱动系统

内核驱动负责跨进程内存读写，启动时提供交互菜单选择：

| 驱动 | 说明 |
|------|------|
| TWT | 基于 /dev 字符设备的内核模块 |
| RT | 实时驱动，替代方案 |
| 自动 | 按优先级依次探测可用驱动 |

启动时如果连接终端（如 adb shell），可手动选择驱动和运行模式（无后台隐身 / 普通模式）。无终端时（MT 管理器启动）自动走隐身模式。

### 自动盖板

求生者模式下自动检测最近的板子与监管者，监管者进入判定范围时自动模拟触摸点击交互键砸板。

- 触摸坐标可调（X/Y 滑块）
- 四点触摸校准（硬编码矩阵 A-F，启动即校准）
- 判定范围可视化（钻石切面银色框 + 四角亮点）
- 触发距离可调（5~40 米）

### 进程隐身 (stealth.h)

- 进程名伪装为 `[kworker/u:0]`
- OOM 保护（oom_score_adj = -1000）
- 不影响游戏进程扫描

### 摸金模式

一键开关控制 16 个设置项（精细过滤 + 地图管理全模块）：

| 勾选摸金 | 取消摸金 |
|----------|----------|
| 全部 ON | 全部 OFF |

涵盖：怪物、宝箱、陷阱、交互物、高低价值物品、显示距离、导航地图、3D路径、路线规划、自动识别、校准、无视过滤、幽灵残影。

---

## 关键变量命名约定

| 前缀 | 含义 |
|------|------|
| `g_` | 全局变量 |
| `mj_` | 摸金模式相关 |
| `show_` | 显示开关 |
| `draw_` | 绘制相关 |
| `map_` | 地图系统 |
| `path_` | 路径系统 |
| `dest_` | 目的地导航 |
| `nav_` | 导航相关 |
| `3d_` | 3D 渲染 |

---

## 配置文件

| 文件 | 位置 | 内容 |
|------|------|------|
| `overlay_config.txt` | `/data/local/bin/` | 运行时配置（KV 格式，自动读写） |
| `map_config.json` | `/data/local/bin/maps/` | 地图数据（指纹、坐标、路径） |
| `fingerprint_db.txt` | `/data/local/bin/maps/` | 地图指纹库 |

---

## 开发版 ↔ 用户版 同步流程

### 架构概览

```
开发版 (E:\ImGuiOverlay)              用户版 (E:\ImGuiOverlay-User)
┌─────────────────────────┐          ┌─────────────────────────────┐
│ 无联网控制              │  同步→   │ 卡密授权系统                 │
│ 无 license.enc          │          │ main.cpp: AUTH_SERVER        │
│ CMakeLists: overlay     │          │ CMakeLists: overlay_user     │
│ 二进制名: overlay       │          │ 二进制名: overlay_user       │
└─────────────────────────┘          │ backend/ (server.py+数据库)  │
                                     │ license.enc + deploy.sh     │
                                     └─────────────────────────────┘
```

### 两个版本的差异

| 文件 | 开发版 | 用户版 |
|------|--------|--------|
| `main.cpp` | 无卡密逻辑 | 含 AUTH_SERVER + HTTP 验证 |
| `CMakeLists.txt` | target: overlay | target: overlay_user + curl/openssl |
| `backend/` | 不存在 | 服务端 PHP/Python + 数据库 |
| `license.enc` | 不存在 | 加密许可证 |
| `deploy.sh` | 不存在 | 自动化部署脚本 |
| `overlay.db` | 不存在 | 本地卡密数据库 |
| 其他所有文件 | **完全相同** | **从开发版同步** |

### 同步步骤（每次开发版更新后执行）

```bash
# 1. 开发版提交并推送 GitHub
cd E:\ImGuiOverlay
git add -A && git commit -m "描述改动"
git push origin main

# 2. 用户版拉取合并
cd E:\ImGuiOverlay-User
git pull --no-rebase origin main

# 3. 如果 main.cpp / CMakeLists.txt 有冲突 → 保留用户版（含卡密系统）
#    README.md 等文档冲突 → 用开发版（更新更全）

# 4. 编译用户版
.\gradlew.bat assembleRelease

# 5. 推送用户版、部署
```

### ⚠️ 关键规则

1. **开发版不需要联网控制** — main.cpp 中不应包含 AUTH_SERVER 代码
2. **用户版必须保留卡密系统** — 同步时绝不能覆盖 main.cpp / CMakeLists.txt / backend/
3. **大部分文件（draw_Gui.cpp、Structs.h、千叶.h 等）应保持一致** — 这些是公共代码
4. **开发版是唯一修改源** — 所有新功能先在开发版实现，再同步到用户版

---

## 常见问题

**Q: 为什么不能推到 `/data/local/tmp/`？**
A: SELinux 上下文为 `tmpfs:s0`，禁止执行。`/data/local/bin/` 为 `system_data_file:s0`，允许执行。

**Q: 编译后找不到二进制？**
A: 编译产物是 `cxx/.../obj/arm64-v8a/overlay`（native ELF），不是 APK。不要推送 APK 到手机执行。

**Q: 地图识别不准？**
A: JSON 中每个地图不应有多个独立条目，多楼层应合并在一个 slot 下。

---

## 版本历史

详见 [CHANGELOG.md](CHANGELOG.md)

当前稳定版：**v2.36-stable**（2026-07-01）
- 地图识别重构（32 张指纹库 + 音乐盒/钢琴/凳子匹配）
- 纹理分帧上传 + 近点合并（性能大幅提升）
- 内核模块隐身（伪装 Synaptics 驱动）
- 钻石切面判定框 + 触摸校准硬编码
