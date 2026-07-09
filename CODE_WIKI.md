# ImGuiOverlay-User 代码 Wiki

> **当前版本**: v2.47-stable (version code 247)
> **目标平台**: Android 9-16 (arm64-v8a), 需 root 权限
> **目标游戏**: 第五人格 (`com.netease.idv` / `dwrg`)
> **最后更新**: 2026-07-09

---

## 目录

1. [项目概述](#1-项目概述)
2. [整体架构](#2-整体架构)
3. [目录结构](#3-目录结构)
4. [核心模块职责](#4-核心模块职责)
5. [关键类与函数说明](#5-关键类与函数说明)
6. [数据结构与数据流](#6-数据结构与数据流)
7. [线程模型](#7-线程模型)
8. [依赖关系](#8-依赖关系)
9. [构建与运行方式](#9-构建与运行方式)
10. [安卓 16 UI 卡死问题分析](#10-安卓-16-ui-卡死问题分析)

---

## 1. 项目概述

ImGuiOverlay-User 是基于 ImGui + OpenGL ES 的 Android 原生游戏辅助叠加层(Overlay),通过内核驱动跨进程内存读写实现对第五人格游戏内实体的透视渲染、地图导航、自动盖板与自瞄功能。

**技术特点**:
- 纯 Native C++ 程序(非 APK),由 root 直接执行 ELF 二进制
- 直接通过 `SurfaceComposerClient` 在 SurfaceFlinger 创建 Layer,绕过 Activity/View 体系
- 通过 evdev (`/dev/input/event*`) 直接读取输入设备,绕过 Android InputDispatcher
- 内核驱动 ioctl 跨进程内存读写(非 ptrace,非 `/proc/pid/mem`)
- 双缓冲 + 原子索引实现数据线程与渲染线程无锁通信
- 完整的卡密验证、反调试、反逆向、隐身保护体系

**双版本架构**:
- **开发版** (`overlay`): 无联网控制,无卡密
- **用户版** (`overlay_user`): 含 `AUTH_SERVER` 卡密验证系统,源码相同仅多一个编译宏

---

## 2. 整体架构

### 2.1 系统分层

```
┌─────────────────────────────────────────────────────────┐
│                    用户操作层                              │
│  音量键(展开/折叠) │ 触摸(UI交互/拖动) │ 悬浮按钮(开关)     │
└─────────┬───────────────────┬──────────────────┬────────┘
          │ evdev              │ evdev            │ ImGui
          ▼                    ▼                  ▼
┌─────────────────┐  ┌──────────────────┐  ┌──────────────┐
│  音量线程        │  │  Touch::TypeA    │  │  ImGui IO    │
│  select+O_NONBLK│  │  阻塞read        │  │  MousePos/Down│
│  → MemuSwitch   │  │  → io.MousePos   │  │              │
└─────────────────┘  └──────────────────┘  └──────┬───────┘
                                                   │
                          ┌────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────┐
│                    渲染线程 (DrawThread)                  │
│  Layout_tick_UI → ImGui Begin/End → OpenGL ES 提交        │
│  ├─ Draw_Main_Optimized (实体ESP)                        │
│  ├─ Draw_MapOverlay (地图覆盖)                           │
│  ├─ AutoWoodCheck (自动盖板)                             │
│  └─ AutoAimCheck (自瞄)                                  │
└─────────────────────────┬───────────────────────────────┘
                          │ 读取前台缓冲
                          ▼
┌─────────────────────────────────────────────────────────┐
│                  数据线程 (DataThread)                    │
│  read_thread:                                            │
│  ├─ 扫描游戏进程内存 (vm_readv via 内核驱动)              │
│  ├─ 实体分类 (类名→阵营/子类)                             │
│  └─ 写入后台缓冲 → 原子切换 front_buffer_idx             │
└─────────────────────────┬───────────────────────────────┘
                          │ ioctl
                          ▼
┌─────────────────────────────────────────────────────────┐
│              内核驱动层 (TWT / RT / mem_rw)               │
│  /dev/ssZDdB (RT) │ anon_inode:TwT_driver (TWT)          │
│  ioctl: READ_MEM / WRITE_MEM / TOUCH_INJECT              │
└─────────────────────────────────────────────────────────┘
```

### 2.2 启动流程

`main.cpp` 的启动顺序:

1. **安全检测 checkpoint 1** (`cp_environment`): 模拟器/VPN 检测
2. **CPU 亲和性设置**: 根据核心数绑核
3. **更新检查** (`api_check_v2_fast`): 3s 短超时,超时降级
4. **反调试检测 checkpoint 2** (`cp_anti_debug`)
5. **卡密授权** (`doAuth`): token 快速验证 → 卡密输入验证
6. **驱动选择**: 交互式选择 TWT/RT 或自动探测
7. **图形初始化**: `GraphicsManager` → `ANativeWindowCreator::Create` → `graphics->Init_Render`
8. **触摸初始化** (`Touch::Init`): 失败则折叠 UI
9. **渲染线程优先级**: `SCHED_RR` 实时调度
10. **隐身初始化** (`stealth_init`): 进程名伪装为 `[kworker/u:0]`
11. **心跳线程**: 每 60s 通知服务器在线
12. **完整性监控线程** (`start_integrity_monitor`)
13. **数据读取线程** (`read_thread`)
14. **配置异步拉取线程**: 拉取服务端配置和游戏偏移
15. **音量键线程** (`音量`)
16. **主渲染循环**: `while(flag) { drawBegin; NewFrame; Layout_tick_UI; EndFrame; ControlFps; }`

---

## 3. 目录结构

```
ImGuiOverlay-User/
├── app/src/main/cpp/
│   ├── CMakeLists.txt              # CMake 构建配置
│   ├── include/                    # 头文件
│   │   ├── ImGui/                  # ImGui 库 (v1.89+)
│   │   ├── Android_Graphics/       # 图形渲染 (OpenGL/Vulkan)
│   │   ├── Android_draw/           # 绘制核心
│   │   ├── Android_my_imgui/       # ImGui Android 适配
│   │   ├── Android_touch/          # 触摸输入处理
│   │   ├── My_Utils/               # stb_image 等工具
│   │   ├── native_surface/         # ANativeWindowCreator (SurfaceFlinger 交互)
│   │   ├── ui_resources/           # UI 图片资源 (内嵌二进制: ui_bg.h, ui_avatar.h)
│   │   └── json.hpp                # nlohmann/json
│   └── src/
│       ├── main.cpp                # ★ 程序入口
│       ├── Android_draw/           # ★ 核心业务模块 (拆分为 10 个文件)
│       │   ├── draw_Gui_internal.h #   共享头: 全局变量/结构体/函数原型
│       │   ├── draw_Gui.cpp        #   主文件: 初始化/全局变量/命名空间
│       │   ├── draw_LayoutUI.cpp   #   UI 布局: Layout_tick_UI 主循环
│       │   ├── draw_ReadThread.cpp #   数据读取线程
│       │   ├── draw_EntityDraw.cpp #   实体绘制 (ESP)
│       │   ├── draw_MapSystem.cpp  #   地图系统 (指纹识别/纹理)
│       │   ├── draw_MapOverlay.cpp #   地图覆盖渲染
│       │   ├── draw_Config.cpp     #   配置加载/保存
│       │   ├── draw_Talent.cpp     #   天赋查看器
│       │   ├── draw_Touch.cpp      #   触摸注入 (盖板/点击)
│       │   ├── draw_UITheme.cpp    #   UI 主题 (暖金宣纸)
│       │   ├── AutoAim.h           #   自动瞄准 (include 模式,不独立编译)
│       │   ├── Structs.h           #   核心数据结构
│       │   ├── DataManager.h/cpp   #   JSON 数据持久化
│       │   ├── driver_*.h          #   内核驱动接口 (RT/TWT/registry)
│       │   ├── net_*.h             #   网络客户端 (卡密验证)
│       │   ├── anti_debug.h        #   反调试
│       │   ├── anti_re.h           #   反逆向
│       │   ├── crypto.h            #   XOR-CBC 加密
│       │   ├── stealth.h           #   进程隐身
│       │   ├── secure_runtime.h    #   运行时反绕过
│       │   ├── game_offsets.h      #   游戏偏移量 (服务端下发)
│       │   ├── ThreadAffinity.h    #   CPU 亲和性/Timer
│       │   ├── 千叶.h              #   千叶功能模块
│       │   └── Name.h              #   实体名称定义
│       ├── Android_Graphics/       # OpenGL/Vulkan 渲染实现
│       ├── Android_my_imgui/       # ImGui Android 后端
│       ├── Android_touch/          # TouchHelperA (evdev 触摸)
│       └── ImGui/                  # ImGui 核心实现
├── kernel_module/                  # 内核内存读写模块
│   ├── mem_rw.c                    # Linux 内核驱动 (伪装 Synaptics 触摸驱动)
│   └── mem_rw_client.h             # 用户空间客户端
├── backend/                        # 卡密验证服务端 (Python/PHP)
│   ├── server_v3.py                # 主服务器
│   ├── overlay.db                  # SQLite 用户数据库
│   └── schema_v2.sql               # 数据库 Schema
├── archive/                        # 历史归档
│   ├── 历史版本二进制/              #   ELF 文件 + MD5
│   ├── 历史源码备份/                #   v2.47 源码快照
│   └── 更新日志归档/                #   所有版本更新日志
├── maps/                           # 地图纹理 + 路径数据
├── tools/                          # 逆向工具集 (Python 脚本)
├── log_analysis/                   # 日志分析 + 指纹数据库
├── 历史源码备份/v2.47/              # v2.47 完整源码备份
├── README.md
├── CHANGELOG.md
├── version-latest.txt              # v2.47-stable / 247
└── AI发版指南.md
```

---

## 4. 核心模块职责

### 4.1 主入口 (`main.cpp`)

| 函数 | 职责 |
|------|------|
| `main()` | 程序入口:安全检测→授权→驱动→图形→触摸→渲染循环 |
| `doAuth()` | 卡密验证:token 快速验证→卡密输入→保存 token |
| `k_print()` | 启动文字输出 (v2.47 移除 sleep 优化) |

### 4.2 绘制模块 (`Android_draw/`)

v2.47 将原 8897 行的 `draw_Gui.cpp` 拆分为 10 个文件,通过 `draw_Gui_internal.h` 共享声明。

| 文件 | 职责 | 关键函数 |
|------|------|----------|
| `draw_Gui_internal.h` | 共享头:200+ 全局变量 extern、结构体、函数原型、inline 工具 | `optimizedWorldToScreen`, `getObjectCoordinates` |
| `draw_Gui.cpp` | 全局变量定义、命名空间实现、核心初始化 | `init_My_drawdata`, `screen_config`, `drawBegin`, `音量` |
| `draw_LayoutUI.cpp` | UI 布局与主渲染入口 | `Layout_tick_UI` (9 个 Tab) |
| `draw_ReadThread.cpp` | 数据读取线程 | `read_thread`, `WriteDebugLog` |
| `draw_EntityDraw.cpp` | 实体 ESP 绘制 | `Draw_Main_Optimized`, `ProcessObjectWithFullDetails` |
| `draw_MapSystem.cpp` | 地图指纹识别 | `LoadMapConfigFromJSON`, `ScoreMapFingerprints`, `LoaderLoop` |
| `draw_MapOverlay.cpp` | 地图覆盖渲染 | `Draw_MapOverlay`, `RDPRecursive` |
| `draw_Config.cpp` | 配置管理 | `LoadConfig`, `SaveConfig`, `DetectGameProcess` |
| `draw_Talent.cpp` | 天赋查看器 | `parse_pickle_talents`, `show_talent_viewer` |
| `draw_Touch.cpp` | 触摸注入 | `SimulateClick`, `AutoWoodCheck`, `InitTouch` |
| `draw_UITheme.cpp` | UI 主题 | `InitModernUITheme`, `ApplyModernUIStyle`, `StyledButton` |

### 4.3 图形渲染 (`Android_Graphics/`)

| 文件 | 职责 |
|------|------|
| `GraphicsManager.h/cpp` | 图形接口工厂:`getGraphicsInterface(OPENGL/VULKAN)` |
| `OpenGLGraphics.h/cpp` | OpenGL ES 3.0 渲染实现 (默认) |
| `VulkanGraphics.h/cpp` | Vulkan 渲染实现 (备选) |
| `vulking_wrapper.h/cpp` | Vulkan 函数指针动态加载 |

### 4.4 ImGui Android 适配 (`Android_my_imgui/`)

| 文件 | 职责 |
|------|------|
| `AndroidImgui.cpp` | ImGui 上下文管理、纹理加载 (stb_image) |
| `my_imgui.cpp` | ImGui 初始化封装 |
| `my_imgui_impl_android.cpp` | ImGui Android 后端 (⚠️ `HandleInputEvent` 为死代码,未调用) |

> **注意**: 项目实际不使用 ImGui 标准 Android 输入管线,所有 `io.MousePos`/`io.MouseDown` 由 `Touch::TypeA` 线程直接写入。

### 4.5 触摸输入 (`Android_touch/TouchHelperA.cpp`)

| 函数 | 职责 |
|------|------|
| `Touch::Init(size, readOnly)` | 初始化触摸:扫描 `/dev/input/event*`,检测 ABS_MT_POSITION_X/Y,创建 uinput 虚拟设备 |
| `Touch::TypeA()` (static) | 触摸读取线程:阻塞 `read()` evdev,解析 EV_ABS,更新 `io.MousePos`/`io.MouseDown[0]` |
| `Touch::SimulateClick()` | 触摸注入:通过 uinput 写入 input_event 序列 |
| `Touch::setOrientation()` | 屏幕旋转时调整坐标映射 |
| `checkDeviceIsTouch()` (static) | 检测设备是否为触摸屏:通过 `EVIOCGBIT(EV_ABS)` 检查 ABS_MT_POSITION_X/Y |

### 4.6 窗口创建 (`native_surface/ANativeWindowCreator.h`)

通过 `dlopen("/system/lib64/libgui.so")` + `dlsym` 直接调用 `SurfaceComposerClient::createSurface`,在 SurfaceFlinger 创建 Layer。**不建立 Android InputChannel**,窗口纯渲染表面。

支持 Android 9-16 的符号适配 (`patchesTable`),不同版本使用不同的 mangled 符号签名。

### 4.7 驱动系统

| 文件 | 职责 |
|------|------|
| `driver_interface.h` | 抽象接口 `IDriver`:read_mem/write_mem/touch_*/生命周期 |
| `driver_registry.h` | 注册中心:`driver_init()` 按优先级尝试 RT→TWT |
| `driver_rt.h` | RT 驱动:扫描 `/dev/` 字符设备,evdev 触摸注入 |
| `driver_twt.h` | TWT 驱动:reboot 系统调用获取 fd,ioctl 触摸注入 |
| `kernel_module/mem_rw.c` | 内核模块:伪装 Synaptics 触摸驱动,`pin_user_pages_remote` 跨进程读写 |

### 4.8 网络验证 (`net_client.h` / `net_config.h`)

- 原生 socket HTTP/1.0 客户端,无外部依赖
- 字符串混淆 (`_S` 宏,编译期 `0x5A` 异或)
- DNS 超时 (`alarm(5)` + `gethostbyname`)
- connect 超时 (非阻塞 connect + select)
- 配置回退链:本地文件 → GitHub raw → 默认值

### 4.9 安全模块

| 模块 | 职责 |
|------|------|
| `anti_debug.h` | 基础反调试:TracerPid/Frida/Xposed 检测,延迟 5s 退出 |
| `anti_re.h` | 增强反逆向:7 维度检测 + 代码完整性校验 (BKPT/NOP slide) |
| `crypto.h` | XOR-CBC 通信加密 (密钥 `ImGuiOverlay2026`) |
| `stealth.h` | 进程隐身:`prctl(PR_SET_NAME, "kworker/u:0")` + OOM 保护 |
| `secure_runtime.h` | 运行时反绕过:心跳降级系统 + AntiBypassGuard 互锁 + 看门狗 |

---

## 5. 关键类与函数说明

### 5.1 核心初始化函数

#### `init_My_drawdata()` — [draw_Gui.cpp](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_draw/draw_Gui.cpp)

模块启动入口,执行一次性初始化:
1. `anti_debug_init()` 反调试初始化
2. `driver_init()` 驱动初始化
3. `LoadMapConfigFromJSON()` 加载地图配置
4. `LoadFingerprintDB()` 加载指纹库
5. `LoadConfig()` 加载用户配置
6. `MjSubsystem::Init()` 摸金模式初始化
7. 触摸坐标跨设备适配 (百分比→绝对坐标)
8. 字体初始化 (`fontSize = sqrt(min(W,H)) * 0.91`)
9. `LoadUITextures()` UI 纹理预加载

#### `Layout_tick_UI(bool* main_thread_flag)` — [draw_LayoutUI.cpp](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_draw/draw_LayoutUI.cpp)

每帧渲染主入口,组织整个 UI:
1. 前置检查 + 屏幕配置
2. `Draw_Main_Optimized()` 实体 ESP
3. `AutoWoodCheck()` 自动盖板
4. 延迟 JSON 写入 (`g_dirty_flush_counter`)
5. UI 动画 (`MemuSwitch` 展开/折叠)
6. 主题应用 + 主窗口渲染 (9 个 Tab)

#### `read_thread(value1, value2, value3)` — [draw_ReadThread.cpp](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_draw/draw_ReadThread.cpp)

后台数据读取线程:
1. 进程定位 (扫描 `/proc/*/cmdline` 找 `dwrg`/`com.netease.idv`)
2. 模块基址定位 (`get_module_bss` / `get_module_bssgjf`)
3. 签名扫描 (magic_matrix=442745336 等魔法值)
4. 实体数组遍历 (上限 1500)
5. 逐实体:类名读取→分类→坐标读取→双缓冲写入
6. `front_buffer_idx.store()` 原子切换缓冲

### 5.2 关键数据结构

#### `DataStruct` — 实体数据 (draw_Gui_internal.h)

```cpp
struct DataStruct {
    uintptr_t obj;          // 对象地址
    uintptr_t objcoor;      // 坐标基址
    int action;
    int 阵营;               // 1=Boss, 2=Player, 3=场景, 4/6=Prop, 5=非猎人
    int sub_type;           // ObjSubClass 枚举
    Vector3A 坐标;          // 世界坐标
    float 状态数值;
    uint64_t 实体特征码;
    std::string str;        // 显示名称
    std::string 类名;
    std::string prop_name;
    bool is_ghost;
    // ... 更多字段
};
```

#### `MapConfig` — 地图配置 (Structs.h)

```cpp
struct MapConfig {
    float minX, maxX, minY, maxY;   // 世界边界
    bool isVerticalMap;
    std::string name;
    int floorIndex;
    float scaleX, scaleY;           // 世界→UV 缩放
    float offsetU, offsetV;         // UV 偏移
    bool flipX, flipY;
    bool calibrated;
    float floorZThreshold = 250;    // 楼层 Z 阈值
};
```

#### `MapFingerprint` — 地图指纹 (Structs.h)

```cpp
struct MapFingerprint {
    int id;
    Vector3A musicBox;              // 音乐盒坐标 (精确匹配)
    Vector3A piano;                 // 钢琴坐标
    std::vector<Vector3A> stools;   // 凳子列表
    int chairCount, coreDoorCount;  // 物体计数 (预筛)
    int mapIndex;                   // 关联 g_all_maps 索引
    bool valid;
};
```

### 5.3 命名空间

#### `GlobalMemory` — 内存扫描状态

```cpp
namespace GlobalMemory {
    uintptr_t libbase;      // libclient.so 基址
    uintptr_t Arrayaddr;    // 实体数组地址
    uintptr_t Matrix;       // 视图矩阵地址
    uintptr_t 自身;         // 自身玩家对象地址
    int 状态;               // 0=未就绪, 1=扫描中, 2=就绪
}
```

#### `MjSubsystem` — 摸金模式

```cpp
namespace MjSubsystem {
    bool draw_props, show_monsters, show_big_chest;
    void Init();
    bool IsMjPropClass(std::string_view cls);
    bool ShouldBypassFilter();
}
```

#### `FastMath` — 快速数学运算 (全 inline)

- `fastDistanceSquared` / `fastDistance`: 避免 sqrt
- `CalculateSurvivorMirrorPos`: 红夫人水镜镜像坐标计算

### 5.4 驱动接口

```cpp
class IDriver {
    virtual bool probe() = 0;                    // 探测设备
    virtual bool connect() = 0;                  // 打开设备
    virtual bool read_mem(uint64_t, void*, size_t) = 0;
    virtual bool write_mem(uint64_t, void*, size_t) = 0;
    virtual bool touch_down(float, float) { return true; }  // 默认实现
    virtual bool touch_up() { return true; }
    virtual const char* name() = 0;
};
```

### 5.5 安全防护

#### `AntiBypassGuard` — 反绕过互锁 (secure_runtime.h)

5 个 flag 分散在 3 个独立内存位置:
- `g_flags1` (堆分配,地址随机)
- `g_flags2` (BSS 段)
- `g_auth2_verified` (独立 atomic bool)

```cpp
bool all_clear() {
    return g_flags1->auth && g_flags1->checkpoint &&
           g_flags2->integrity && g_flags2->hb_ok &&
           g_auth2_verified.load();
}
```

`SECURE_GUARD()` 宏门控所有绘图:`= hb_core_available() && AntiBypassGuard::all_clear() && monitor_watchdog_ok()`

---

## 6. 数据结构与数据流

### 6.1 主数据流

```
游戏进程内存
    │ (vm_readv via 内核驱动 ioctl)
    ▼
read_thread (DataThread CPU)
    │ 扫描实体数组 → 类名分类 → DataStruct
    ▼
data_buffers[2] (双缓冲)
    │ (front_buffer_idx atomic, memory_order_release)
    ▼
Layout_tick_UI (DrawThread CPU)
    │ 读取 data_buffers[front_buffer_idx.load(acquire)]
    ├→ Draw_Main_Optimized → ProcessObjectWithFullDetails (ESP)
    ├→ Draw_MapOverlay (地图覆盖)
    ├→ AutoWoodCheck (盖板)
    └→ AutoAimCheck (自瞄)
    │
    ▼
ImGui 渲染 → OpenGL ES → SurfaceFlinger → 屏幕
```

### 6.2 配置数据流

```
启动:
  LoadConfig() ← /data/local/bin/overlay_config.txt (KV 格式)
  LoadMapConfigFromJSON() ← /data/local/bin/maps/map_config.json
  LoadFingerprintDB() ← /data/local/bin/maps/musicbox_stools.json

运行时:
  UI 操作 → 全局变量
  脏标记 → g_dirty_exits / g_dirty_paths
  倒计时 (1800帧) → FlushDirtyData() → DataManager::Save* → .bak + .json

退出/定期:
  SaveConfig() → config.txt
```

### 6.3 实体分类规则

`read_thread` 中基于类名字符串匹配的分类:

| 类名关键字 | 阵营 | 子类 | 说明 |
|-----------|------|------|------|
| `random01_in_piano01.gim` | 6 | Prop | 钢琴随机道具 |
| `trap.gim` | 6 | Prop | 陷阱 |
| `monster_*` | 6 | Prop | 摸金怪物 |
| `redqueen_mirror` | 5 | - | 红夫人水镜 |
| `chuanhuo` | 5 | - | 厂长残火 (假猎人) |
| `boss` (非prop) | 1 | Boss | 监管者 |
| `h55_prop_tieqiao` | 2 | Player | 守墓人遁地 |
| `player` / `npc_deluosi_dress_ghost` | 2 | Player | 玩家/幽灵 |
| `woodplane` | 3 | Pallet | 板子 |
| `sender` | 3 | CipherMachine | 密码机 |
| `dm65_scene_prop_01` | 3 | Box | 宝箱 |
| `dm65_scene_gallows` | 3 | Chair | 狂欢之椅 |
| `dm65_scene_prop_76` | 3 | Cellar | 地窖 |

### 6.4 地图指纹识别

```
1. 检测场景中的音乐盒/钢琴/凳子坐标
2. 与 g_fingerprint_db 比对:
   - 音乐盒坐标匹配 (tolerance=3.0)
   - 钢琴坐标匹配 (tolerance=5.0)
   - 凳子数量预筛
3. 评分 (MapScoreResult)
4. 状态机: LOCKED → SWITCH_DETECTED → IDENTIFYING → LOCKED
5. 传送检测 (瞬移阈值: XY=80, Z=50)
```

---

## 7. 线程模型

| 线程 | 入口 | CPU 亲和性 | 调度策略 | 职责 |
|------|------|-----------|----------|------|
| 主线程 | `main` | CPU 0 + 4 | SCHED_RR (优先级1) | 渲染循环 |
| 数据线程 | `read_thread` | DataThread 核心 (大核) | 普通调度 | 内存扫描 |
| 音量键线程 | `音量` | - | 普通 | evdev 读音量键 |
| 触摸线程 | `Touch::TypeA` | - | 普通 | evdev 读触摸 |
| 心跳线程 | lambda | - | 普通 | 每 60s 通知服务器 |
| 完整性监控 | `start_integrity_monitor` | - | 普通 | 每 20-30s 自检 |
| 配置拉取 | lambda | - | 普通 | 异步拉取服务端配置 |
| 纹理加载 | `LoaderLoop` | - | 普通 | 异步加载地图纹理 |

**线程安全机制**:
- **双缓冲**: `data_buffers[2]` + `front_buffer_idx` (atomic) + `data_mutex`
- **内存序**: `memory_order_release` (写) / `memory_order_acquire` (读)
- **延迟脏数据**: `g_dirty_flush_counter` 合并多次修改为一次 I/O
- **重初始化信号**: `g_need_reinit` (atomic)
- **纹理队列锁**: `g_pending_mtx`
- **模仿者列表锁**: `mimic_mutex`
- **调试日志锁**: `g_debug_mutex`

**卡屏修复历史** (v2.39+):
- 签名扫描每 256 页 `sched_yield()` 防饿死渲染线程
- 渲染线程提升为 SCHED_RR 实时优先级
- 渲染线程不执行阻塞 I/O
- 每 1800 帧重新强制绑核

---

## 8. 依赖关系

### 8.1 系统库依赖

| 库 | 用途 |
|----|------|
| `liblog` | Android 日志 (`__android_log_print`) |
| `libandroid` | ANativeWindow API |
| `libEGL` | EGL 上下文管理 |
| `libGLESv3` | OpenGL ES 3.0 渲染 |
| `libvulkan` | Vulkan 渲染 (备选) |
| `libdl` | dlopen/dlsym (动态加载 libgui.so) |
| `libgui.so` (系统) | SurfaceComposerClient (窗口创建) |
| `libutils.so` (系统) | RefBase/String8 (跨进程对象) |

### 8.2 第三方源码集成

| 库 | 版本 | 用途 |
|----|------|------|
| ImGui | v1.89+ | 即时模式 GUI |
| nlohmann/json | - | JSON 解析 |
| stb_image | - | 图像加载 (PNG/JPG/GIF) |
| zlib | - | 压缩 (地图数据) |

### 8.3 编译依赖

- **NDK**: r26+
- **CMake**: 3.10+
- **C++ 标准**: C++17
- **编译标志** (Release):
  ```
  -O2 -DNDEBUG -fvisibility=hidden -flto
  -ffunction-sections -fdata-sections
  -fstack-protector-strong -D_FORTIFY_SOURCE=2
  ```
- **链接标志**:
  ```
  -Wl,-s -Wl,--gc-sections -Wl,-z,relro -Wl,-z,now
  ```
- **编译定义**: `VK_NO_PROTOTYPES`, `VK_USE_PLATFORM_ANDROID_KHR`, `IMGUI_DISABLE_DEBUG_TOOLS`, `AUTH_SERVER` (用户版)

### 8.4 运行时依赖

- **root 权限**: 必需 (访问 `/dev/input/event*`、`/dev/uinput`、内核驱动)
- **内核驱动**: TWT 或 RT 驱动需预先加载 (或使用 mem_rw.ko)
- **数据目录**: `/data/local/bin/` (SELinux `system_data_file:s0`,可执行)
- **字体文件**: `/data/local/bin/与辅助放同一目录.ttf` (回退到系统 NotoSerifCJK)

---

## 9. 构建与运行方式

### 9.1 编译

```bash
cd E:\ImGuiOverlay-User
.\gradlew.bat assembleRelease
```

**编译产物**:
- 原生 ELF: `app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a/overlay_user`
- APK (仅触发 NDK 编译,不用于安装): `app/build/outputs/apk/release/app-release-unsigned.apk`

> **Debug 版本**: `.\gradlew.bat :app:assembleDebug` → 产物在 `cxx/Debug/` 目录,含调试符号

### 9.2 部署

```bash
# 停止旧进程
adb shell "su -c 'killall overlay_user'"

# 推送二进制 (两阶段:先到 /sdcard,再 su cp)
adb push "app/build/.../overlay_user" /sdcard/overlay_user_tmp
adb shell "su -c 'cp /sdcard/overlay_user_tmp /data/local/bin/overlay_user && chmod 777 /data/local/bin/overlay_user && rm /sdcard/overlay_user_tmp'"

# 启动
adb shell "su -c '/data/local/bin/overlay_user &'"
```

**路径约定**:
- 可执行文件: `/data/local/bin/overlay_user` (必须 chmod 777)
- 地图数据: `/data/local/bin/maps/`
- 配置文件: `/data/local/bin/overlay_config.txt`
- 卡密文件: `/data/local/bin/key.txt` (非终端模式)
- 授权 token: `/data/local/bin/auth_token.dat`
- 字体文件: `/data/local/bin/与辅助放同一目录.ttf`

> **禁止** 使用 `/data/local/tmp/` (SELinux `tmpfs:s0` 阻止执行)

### 9.3 运行模式

- **终端模式** (adb shell 启动): 交互式选择驱动(TWT/RT/自动)和模式(隐身/普通)
- **非终端模式** (MT 管理器启动): 自动隐身模式,自动探测驱动,卡密从 `key.txt` 读取

### 9.4 操作方式

| 操作 | 功能 |
|------|------|
| 音量+键 | 展开 UI |
| 音量-键 | 折叠 UI |
| 触摸拖动 | UI 交互、滑动 |
| 悬浮按钮点击 | 功能开关 (红=开启/灰=关闭) |

---

## 10. 安卓 16 UI 卡死问题分析

### 10.1 现象

- 程序启动后 UI 卡住,无法拖动、无法点击
- 只有音量加减键能控制悬浮窗展开/折叠
- **安卓 15 不会发生,安卓 16 会卡住**

### 10.2 根因分析

经过对输入系统的完整代码审查,确认本项目**完全绕过 Android 标准输入系统**,采用 Linux 原生 evdev 机制。卡死的根因在于**触摸读取线程失效**,而音量键线程正常。

#### 两条输入链路对比

**音量键链路** (正常工作):
```
音量() 线程
  ├─ open("/dev/input/event*", O_RDONLY | O_NONBLOCK)  ← 非阻塞
  ├─ select(maxfd+1, &fds, NULL, NULL, {tv_sec=1})     ← 1秒超时
  ├─ read()                                            ← 非阻塞,立即返回
  └─ 处理输入事件(ev) → 仅检查 EV_KEY (KEY_VOLUMEUP/DOWN)
```

**触摸链路** (失效):
```
Touch::Init
  ├─ open("/dev/input/event*", O_RDWR)                 ← ⚠️ 无 O_NONBLOCK
  ├─ (失败回退) open(temp, O_RDONLY)                   ← ⚠️ 仍无 O_NONBLOCK
  ├─ checkDeviceIsTouch(fd)                            ← ⚠️ 可能失败
  │   └─ ioctl(EVIOCGBIT(EV_ABS)) → 检查 ABS_MT_POSITION_X/Y
  └─ Touch::TypeA 线程
      └─ read(device.fd, inputEvent, sizeof)           ← ⚠️ 阻塞式 read!
```

#### 三个可能的失效点 (按概率排序)

**失效点 1 (最高概率): `checkDeviceIsTouch()` 检测失败**

[TouchHelperA.cpp:235-267](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_touch/TouchHelperA.cpp) 的 `checkDeviceIsTouch()` 依赖 `ioctl(fd, EVIOCGBIT(EV_ABS, ...))` 返回触摸屏的 ABS 能力位图,要求同时存在 `ABS_MT_POSITION_X` 和 `ABS_MT_POSITION_Y`。

安卓 16 可能修改了内核触摸驱动的 evdev 协议:
- InputFlinger 重构可能导致 `EVIOCGBIT` 不再返回预期的 ABS 位
- 触摸事件可能改为通过用户态服务转发,不经过标准 evdev

**后果**: 所有设备检测失败 → `devices.empty()` → `Init` 返回 `false` → `MemuSwitch = false` (UI 折叠) → `TypeA` 线程未启动 → 即使音量键展开 UI,触摸也无响应。

**失效点 2 (高概率): 阻塞式 `read()` 永久卡死**

[TouchHelperA.cpp:165](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_touch/TouchHelperA.cpp) 的 `TypeA` 线程使用**阻塞式** `read()` (fd 未设置 `O_NONBLOCK`)。

如果安卓 16 的内核/SELinux 在未 `EVIOCGRAB` 时不向第三方读取者分发 `EV_ABS` 事件:
- `TypeA` 线程的 `read()` 永久阻塞
- `io.MousePos` / `io.MouseDown[0]` 永远不会更新
- ImGui 接收不到任何触摸输入

**对比**: 音量键线程用 `select()` + `O_NONBLOCK`,有 1 秒超时,不会卡死。

**失效点 3 (中概率): SELinux 差异化限制**

安卓 16 可能对 `/dev/input/event*` 节点实施差异化 SELinux 策略:
- **按键设备** (volume rocker): 仍允许读取 `EV_KEY` 事件
- **触摸屏设备**: 新增 `neverallow` 规则,禁止非系统进程读取 `EV_ABS`

这会导致 `open()` 成功但 `read()` 返回 `EACCES` 或永远无数据。

#### 关键代码证据

1. **音量键线程** ([draw_Gui.cpp:855-896](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_draw/draw_Gui.cpp)): 使用 `O_RDONLY | O_NONBLOCK` + `select()` 1 秒超时,只读 `EV_KEY`,**不检查 ABS 能力**

2. **触摸线程** ([TouchHelperA.cpp:289-296](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_touch/TouchHelperA.cpp)): `open(temp, O_RDWR)` **无 `O_NONBLOCK`**;`TypeA` 线程 `read()` 是阻塞调用

3. **ImGui HandleInputEvent 是死代码** ([my_imgui_impl_android.cpp:228](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_my_imgui/my_imgui_impl_android.cpp)): 全项目无任何代码调用 `HandleInputEvent`,所有 ImGui 输入状态由 `Touch::TypeA` 直接写入

4. **窗口无 InputChannel** ([ANativeWindowCreator.h](file:///e:/ImGuiOverlay-User/app/src/main/cpp/include/native_surface/ANativeWindowCreator.h)): 纯 SurfaceFlinger Layer,Android `InputDispatcher` 不向其分发事件

### 10.3 诊断验证方法

在安卓 16 设备上执行以下命令验证:

```bash
# 1. 查看触摸设备 SELinux 标签
adb shell "su -c 'ls -laZ /dev/input/event*'"

# 2. 查看触摸设备是否仍报告 ABS_MT_POSITION_X/Y
adb shell "su -c 'getevent -p'"

# 3. 查看 ImGui 日志是否有 "获取屏幕驱动失败"
adb shell "su -c 'logcat -d | grep ImGui'"

# 4. 查看 SELinux 拒绝日志
adb shell "su -c 'dmesg | grep denied'"

# 5. 直接测试触摸设备读取
adb shell "su -c 'cat /proc/self/fd/0 < /dev/input/event0'"  # 替换 event0 为触摸设备
```

### 10.4 修复方案建议 (待确认)

**方案 A: 修复 `checkDeviceIsTouch()` 检测逻辑**
- 放宽检测条件,增加 `ABS_MT_SLOT` / `BTN_TOUCH` 等多种判定
- 添加设备名匹配回退 (`/proc/bus/input/devices` 中包含 `touch` 关键字)

**方案 B: `TypeA` 线程改为非阻塞 + select**
- `open()` 添加 `O_NONBLOCK`
- `read()` 前用 `select()` 带超时,避免永久阻塞
- 与音量键线程使用相同的健壮模式

**方案 C: 增加 InputFlinger 兼容路径**
- 如果 evdev 失效,回退到通过 `getevent` 命令或 `AccessibilityService` 获取触摸
- 或使用内核驱动 ioctl 直接注入触摸 (已有 `driver_twt.h` 的 touch_down/touch_up)

**方案 D: 综合修复 (推荐)**
- 同时实施方案 A + B
- `Touch::Init` 失败时自动尝试方案 C 的回退路径
- 添加安卓版本检测,16+ 启用兼容模式

---

## 附录

### A. 全局变量命名约定

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
| `wood_` | 自动盖板 |

### B. 版本历史要点

| 版本 | 日期 | 要点 |
|------|------|------|
| v2.45 | 2026-07-06 | 彻底修复跨设备触摸失效 (坐标系统一) |
| v2.46 | 2026-07-06 | 厂长残火 (chuanhuo) 过滤 |
| v2.47 | 2026-07-09 | draw_Gui.cpp 拆分为 10 文件;启动卡顿优化;字体加载修复 |

### C. 关键文件快速索引

| 功能 | 文件 |
|------|------|
| 程序入口 | [main.cpp](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/main.cpp) |
| 渲染主循环 | [draw_LayoutUI.cpp](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_draw/draw_LayoutUI.cpp) |
| 数据读取线程 | [draw_ReadThread.cpp](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_draw/draw_ReadThread.cpp) |
| 触摸处理 | [TouchHelperA.cpp](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_touch/TouchHelperA.cpp) |
| 窗口创建 | [ANativeWindowCreator.h](file:///e:/ImGuiOverlay-User/app/src/main/cpp/include/native_surface/ANativeWindowCreator.h) |
| 驱动接口 | [driver_interface.h](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_draw/driver_interface.h) |
| 安全防护 | [secure_runtime.h](file:///e:/ImGuiOverlay-User/app/src/main/cpp/src/Android_draw/secure_runtime.h) |
| 构建配置 | [CMakeLists.txt](file:///e:/ImGuiOverlay-User/app/src/main/cpp/CMakeLists.txt) |
