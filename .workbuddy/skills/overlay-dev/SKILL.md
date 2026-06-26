---
title: "ImGuiOverlay 项目开发"
summary: "Android ImGui Overlay 项目的开发与维护，包含地图检测、坐标校准、路径绘制等"
read_when:
  - "用户涉及 ImGuiOverlay 项目、地图检测、坐标校准"
  - "用户提到 overlay 编译、ADB 推送、GitHub 同步"
  - "用户要修改 draw_Gui.cpp、map_config.json、校准数据"
  - "用户问及 SaveConfig、LoadConfig、音乐盒匹配"
  - "用户需要编译项目推送到手机"
project: "E:/ImGuiOverlay"
---

# ImGuiOverlay 项目开发技能

## 项目结构
```
E:\ImGuiOverlay/
├── app/src/main/cpp/          # C++ 源码
│   ├── src/Android_draw/       # 主渲染逻辑
│   │   └── draw_Gui.cpp        # 核心文件（6000+行）
│   └── include/                # 头文件
├── build.gradle.kts            # Gradle 构建配置
├── local.properties            # SDK 路径
└── CHANGELOG.md                # 更新日志
```

## 编译与部署流程
```
1. PowerShell: .\gradlew.bat assembleRelease
2. find .build/ -name overlay | adb push to /data/local/tmp/overlay
3. adb shell chmod 777 /data/local/tmp/overlay
```

## 每次修改必须执行的步骤
1. ✅ 修改代码
2. ✅ 编译 (gradlew assembleRelease)
3. ✅ adb push 到手机
4. ✅ 更新 CHANGELOG.md
5. ✅ git commit + git push (GitHub)
6. ✅ 在工作日志中记录变更摘要

## 重要文件路径
- 配置备份: `/sdcard/maps/calib_backup/overlay_config.bak*`
- 地图配置: `/sdcard/maps/map_config.json`
- 路径数据: `/sdcard/maps/map_config.json` → player_paths 字段
- 运行时配置: `/data/local/tmp/overlay_config.txt`
