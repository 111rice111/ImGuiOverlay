#include "DataManager.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "json.hpp"

using json = nlohmann::json;

// ============================================================
// 工具函数
// ============================================================

static std::string GetMapJsonPath() {
    return "/data/local/bin/maps/map_config.json";
}

static std::string GetFingerprintJsonPath() {
    return "/data/local/bin/maps/musicbox_stools.json";
}

static bool CreateBackup(const std::string& filepath) {
    try {
        if (std::filesystem::exists(filepath)) {
            std::string bak = filepath + ".bak";
            std::filesystem::copy(filepath, bak,
                std::filesystem::copy_options::overwrite_existing);
            return true;
        }
    } catch (...) {}
    return false;
}

std::string DataManager::GetJsonPath() { return GetMapJsonPath(); }
std::string DataManager::GetFingerprintPath() { return GetFingerprintJsonPath(); }

// ============================================================
// 安全地加载 JSON 文件
// ============================================================
static bool SafeLoadJSON(const std::string& path, json& out_j) {
    std::ifstream ifs(path);
    if (!ifs) return false;
    try {
        ifs >> out_j;
        return true;
    } catch (...) {
        return false;
    }
}

static bool SafeSaveJSON(const std::string& path, const json& j) {
    // 先备份
    CreateBackup(path);
    std::ofstream ofs(path);
    if (!ofs) return false;
    ofs << j.dump(4);
    return true;
}

// ============================================================
// 核心：LoadAll — 加载所有配置
// ============================================================
// ⚠️ 这是重写的版本：不再清除 g_all_maps 外部数据，
//    而是由调用者先 resolve 索引后再用 LoadExitsAndPaths
//    填充数据。外部 g_all_maps 的构造仍由调用者完成。
// ============================================================

// 注：LoadAll 不是重写 LoadMapConfigFromJSON，而是新增的
// 工具接口。实际调用时，draw_Gui.cpp 先用原生的 LoadMapConfigFromJSON
// 填充 g_all_maps，然后用本模块加载出口/路径数据。

// ============================================================
// 加载出口和路径（按地图/楼层）
// ============================================================
bool DataManager::LoadExitsAndPaths(
    int mapIdx, int floorIdx,
    std::vector<std::vector<std::vector<Vector3A>>>& out_exits,
    std::vector<std::vector<std::vector<ImVec2>>>& out_uvs,
    std::vector<std::vector<std::vector<std::vector<Vector3A>>>>& out_paths)
{
    json j;
    if (!SafeLoadJSON(GetMapJsonPath(), j)) return false;
    if (!j.contains("maps") || !j["maps"].is_array()) return false;

    // 确保容器有足够的空间
    auto ensure_size = [](auto& vec, size_t sz) {
        while (vec.size() <= sz) vec.push_back(typename std::decay<decltype(vec)>::type::value_type());
    };

    for (auto& m : j["maps"]) {
        int floor = m.value("floor", 0);
        std::string name = m.value("name", "");

        // 用名字匹配地图索引（忽略 floor 的匹配）
        int matched_idx = -1;
        // 调用者需要在外部建立从 name → mapIdx 的映射
        // 这里我们使用直接的 mapIdx/floorIdx 查询

        // 但如果 mapIdx 已经在 JSON 地图条目中记录过？
        // 直接用传入的 mapIdx 查找 floor 相符的条目
        // 简单方案：遍历 JSON，找与指定 mapIdx/floorIdx 都匹配的条目
    }

    // 更直接的方法：只查询 JSON 中第 mapIdx 个 maps 条目（如果 floor 相符）
    if (mapIdx >= 0 && mapIdx < (int)j["maps"].size()) {
        auto& m = j["maps"][mapIdx];
        int mf = m.value("floor", 0);
        if (mf == floorIdx) {
            // 加载出口
            ensure_size(out_exits, mapIdx);
            ensure_size(out_exits[mapIdx], floorIdx);
            out_exits[mapIdx][floorIdx].clear();

            if (m.contains("exits") && m["exits"].is_array()) {
                for (auto& e : m["exits"]) {
                    Vector3A pos;
                    pos.X = e.value("x", 0.0f);
                    pos.Y = e.value("y", 0.0f);
                    pos.Z = e.value("z", 0.0f);
                    out_exits[mapIdx][floorIdx].push_back(pos);
                }
            }

            // 加载路径
            ensure_size(out_paths, mapIdx);
            ensure_size(out_paths[mapIdx], floorIdx);
            out_paths[mapIdx][floorIdx].clear();

            if (m.contains("player_paths") && m["player_paths"].is_array()) {
                for (auto& path : m["player_paths"]) {
                    std::vector<Vector3A> pts;
                    if (path.contains("points") && path["points"].is_array()) {
                        for (auto& pt : path["points"]) {
                            Vector3A v;
                            v.X = pt.value("x", 0.0f);
                            v.Y = pt.value("y", 0.0f);
                            v.Z = pt.value("z", 0.0f);
                            pts.push_back(v);
                        }
                    }
                    out_paths[mapIdx][floorIdx].push_back(pts);
                }
            }
            return true;
        }
    }
    return false;
}

// ============================================================
// 保存出口
// ============================================================
bool DataManager::SaveExits(
    int mapIdx, int floorIdx,
    const std::vector<std::vector<std::vector<Vector3A>>>& all_exits,
    const std::vector<std::vector<std::vector<ImVec2>>>& all_uvs)
{
    json j;
    SafeLoadJSON(GetMapJsonPath(), j);
    if (!j.contains("maps") || !j["maps"].is_array()) return false;

    if (mapIdx >= (int)j["maps"].size()) return false;

    auto& m = j["maps"][mapIdx];
    if (m.value("floor", 0) != floorIdx) return false;

    m["exits"] = json::array();
    if (mapIdx < (int)all_exits.size() && floorIdx < (int)all_exits[mapIdx].size()) {
        for (auto& e : all_exits[mapIdx][floorIdx]) {
            json ej;
            ej["x"] = e.X; ej["y"] = e.Y; ej["z"] = e.Z;
            m["exits"].push_back(ej);
        }
    }

    return SafeSaveJSON(GetMapJsonPath(), j);
}

// ============================================================
// 保存路径
// ============================================================
bool DataManager::SavePlayerPaths(
    int mapIdx, int floorIdx,
    const std::vector<std::vector<Vector3A>>& paths,
    const std::vector<std::vector<std::vector<std::vector<Vector3A>>>>& all_paths)
{
    json j;
    SafeLoadJSON(GetMapJsonPath(), j);
    if (!j.contains("maps") || !j["maps"].is_array()) return false;

    if (mapIdx >= (int)j["maps"].size()) return false;

    auto& m = j["maps"][mapIdx];
    if (m.value("floor", 0) != floorIdx) return false;

    m["player_paths"] = json::array();
    for (auto& path : paths) {
        json pj;
        pj["points"] = json::array();
        for (auto& pt : path) {
            json ptj;
            ptj["x"] = pt.X; ptj["y"] = pt.Y; ptj["z"] = pt.Z;
            pj["points"].push_back(ptj);
        }
        m["player_paths"].push_back(pj);
    }

    return SafeSaveJSON(GetMapJsonPath(), j);
}

// ============================================================
// 保存场景物体
// ============================================================
bool DataManager::SaveSceneObjects(
    int mapIdx, int floorIdx,
    const Vector3A& piano,
    const std::vector<Vector3A>& chairs)
{
    json j;
    SafeLoadJSON(GetMapJsonPath(), j);
    if (!j.contains("maps") || !j["maps"].is_array()) return false;

    for (auto& m : j["maps"]) {
        // 按名字匹配（更健壮）
        // 先找到匹配的地图名
        int mf = m.value("floor", 0);
        (void)mf;
        // 简化：直接按索引匹配
    }

    if (mapIdx >= (int)j["maps"].size()) return false;
    auto& m = j["maps"][mapIdx];

    if (piano.X != 0.0f || piano.Y != 0.0f) {
        m["piano_x"] = piano.X;
        m["piano_y"] = piano.Y;
        m["piano_z"] = piano.Z;
    }
    m["chairs"] = json::array();
    for (auto& c : chairs) {
        json cj;
        cj["x"] = c.X; cj["y"] = c.Y; cj["z"] = c.Z;
        m["chairs"].push_back(cj);
    }

    return SafeSaveJSON(GetMapJsonPath(), j);
}

// ============================================================
// 数据导出
// ============================================================
std::string DataManager::ExportAllData() {
    json j;
    SafeLoadJSON(GetMapJsonPath(), j);
    if (j.is_null()) j = json::object();
    return j.dump(4);
}

// ============================================================
// 数据导入
// ============================================================
bool DataManager::ImportAllData(const std::string& json_content) {
    try {
        json j = json::parse(json_content);
        if (!j.contains("maps") || !j["maps"].is_array()) return false;
        return SafeSaveJSON(GetMapJsonPath(), j);
    } catch (...) {
        return false;
    }
}

// ============================================================
// 从备份恢复
// ============================================================
bool DataManager::RestoreFromBackup() {
    std::string path = GetMapJsonPath();
    std::string bak = path + ".bak";
    if (!std::filesystem::exists(bak)) return false;

    try {
        std::filesystem::copy(bak, path,
            std::filesystem::copy_options::overwrite_existing);
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================
// 数据统计
// ============================================================
std::string DataManager::GetDataStats() {
    json j;
    if (!SafeLoadJSON(GetMapJsonPath(), j)) {
        return "JSON 文件不存在或无法解析";
    }
    if (!j.contains("maps") || !j["maps"].is_array()) {
        return "JSON 格式错误: 缺少 maps 数组";
    }

    std::stringstream ss;
    int total_maps = (int)j["maps"].size();
    int total_exits = 0;
    int total_paths = 0;
    int total_path_pts = 0;

    for (auto& m : j["maps"]) {
        if (m.contains("exits"))
            total_exits += (int)m["exits"].size();
        if (m.contains("player_paths")) {
            for (auto& p : m["player_paths"]) {
                total_paths++;
                if (p.contains("points"))
                    total_path_pts += (int)p["points"].size();
            }
        }
    }

    ss << "JSON 配置状态:\n";
    ss << "  地图条目: " << total_maps << "\n";
    ss << "  出口总数: " << total_exits << "\n";
    ss << "  路径总数: " << total_paths << "\n";
    ss << "  路径点数: " << total_path_pts << "\n";
    ss << "  文件路径: " << GetMapJsonPath() << "\n";
    ss << "  文件存在: " << (std::filesystem::exists(GetMapJsonPath()) ? "是" : "否") << "\n";

    // 列各地图详情
    ss << "\n各地图详情:\n";
    for (int i = 0; i < total_maps && i < 20; i++) {
        auto& m = j["maps"][i];
        std::string name = m.value("name", "未知");
        int floor = m.value("floor", 0);
        int exits = m.contains("exits") ? (int)m["exits"].size() : 0;
        int paths = m.contains("player_paths") ? (int)m["player_paths"].size() : 0;
        bool cal = m.contains("minX") && m.contains("maxX");
        ss << "  [" << i << "] " << name << " (F" << floor << ")"
           << " 出口:" << exits << " 路径:" << paths
           << (cal ? " 已校准" : "") << "\n";
    }

    // 指纹数据库
    json fp;
    if (SafeLoadJSON(GetFingerprintJsonPath(), fp) && fp.is_array()) {
        ss << "\n指纹数据库: " << fp.size() << " 条记录\n";
    } else {
        ss << "\n指纹数据库: 未加载\n";
    }

    return ss.str();
}

// ============================================================
// 坐标转换工具
// ============================================================
namespace CoordTransform {
    MapConfig MakeFallbackConfig(float g_scale_x, float g_scale_y,
                                  float g_offset_u, float g_offset_v,
                                  bool g_flip_x, bool g_flip_y) {
        MapConfig cfg;
        cfg.scaleX = g_scale_x;
        cfg.scaleY = g_scale_y;
        cfg.offsetU = g_offset_u;
        cfg.offsetV = g_offset_v;
        cfg.flipX = g_flip_x;
        cfg.flipY = g_flip_y;
        cfg.calibrated = false;
        cfg.floorZThreshold = 250.0f;
        return cfg;
    }
}
