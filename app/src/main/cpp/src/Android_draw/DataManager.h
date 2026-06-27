#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <ImGui/imgui.h>
#include "Structs.h"

// ============================================================
// DataManager — 统一数据持久化层
// ============================================================
// 职责：所有 JSON 读取/写入操作，提供统一的 Save()/Load() 接口
// 解决：路径数据在重启后丢失、JSON 被覆盖、多楼层路径混杂等问题
// ============================================================

class DataManager {
public:
    // --- 出口管理 ---
    static bool SaveExits(int mapIdx, int floorIdx,
                          const std::vector<std::vector<std::vector<Vector3A>>>& all_exits,
                          const std::vector<std::vector<std::vector<ImVec2>>>& all_uvs);

    static bool LoadExitsAndPaths(int mapIdx, int floorIdx,
                                  std::vector<std::vector<std::vector<Vector3A>>>& out_exits,
                                  std::vector<std::vector<std::vector<ImVec2>>>& out_uvs,
                                  std::vector<std::vector<std::vector<std::vector<Vector3A>>>>& out_paths);

    // --- 路径管理 ---
    static bool SavePlayerPaths(int mapIdx, int floorIdx,
                                const std::vector<std::vector<Vector3A>>& paths,
                                const std::vector<std::vector<std::vector<std::vector<Vector3A>>>>& all_paths);

    // --- 场景物体同步 ---
    static bool SaveSceneObjects(int mapIdx, int floorIdx,
                                 const Vector3A& piano,
                                 const std::vector<Vector3A>& chairs);

    // --- 数据导出/导入 ---
    static std::string ExportAllData();
    static bool ImportAllData(const std::string& json_content);

    // --- 备份恢复 ---
    static bool RestoreFromBackup();

    // --- 获取数据统计 ---
    static std::string GetDataStats();

    static std::string GetJsonPath();
    static std::string GetFingerprintPath();
};
