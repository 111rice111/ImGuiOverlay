#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <ImGui/imgui.h>

// ============================================================
// 核心数据结构（共享定义）
// ============================================================

struct Vector3A {
    float X{}, Y{}, Z{};
    constexpr Vector3A() = default;
    constexpr Vector3A(float x, float y, float z) noexcept : X(x), Y(y), Z(z) {}
    bool IsZero() const { return X == 0.0f && Y == 0.0f && Z == 0.0f; }
};

struct MapConfig {
    float minX, maxX, minY, maxY;
    bool isVerticalMap;
    const char* name;
    int floorIndex;
    const char* texturePath;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    bool flipX = false;
    bool flipY = false;
    bool calibrated = false;
    float floorZThreshold = 250.0f;  // 楼层Z阈值：>此值视为二楼
};

struct MusicboxKey {
    float x, y, z;
    int mapIndex, floorIndex;
    float tolerance = 3.0f;
    float texU, texV;
};

struct PianoKey {
    float x, y, z;
    int mapIndex;
    float tolerance = 5.0f;
};

struct ChairKey {
    float x, y, z;
    int mapIndex;
    float tolerance = 4.0f;
};

struct MapFingerprint {
    int id = -1;
    Vector3A musicBox;
    Vector3A piano;            // ★ 钢琴坐标(用于精确匹配)
    std::vector<Vector3A> stools;
    std::vector<Vector3A> pianos;
    // ★ v2.28 复合指纹: 物体数量用于快速预筛
    int chairCount = 0;
    int coreDoorCount = 0;
    int outdoorDoorCount = 0;
    int propDoorCount = 0;
    int woodplaneCount = 0;
    int mapIndex = -1;         // ★ 关联到 g_all_maps 索引
    bool valid = false;
};

struct GraphNode {
    Vector3A pos;
    int id = -1;
    bool isItem = false;
    bool isExit = false;
    bool isPlayer = false;
};

struct GraphEdge {
    int from, to;
    float weight;
    std::vector<Vector3A> pathPoints;
};

struct PathGraph {
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::vector<std::vector<int>> adj;
    bool dirty = true;

    void clear() { nodes.clear(); edges.clear(); adj.clear(); dirty = true; }
    void buildFromSavedPaths(const std::vector<std::vector<Vector3A>>& paths,
                             const std::vector<Vector3A>& exits);
    std::vector<int> dijkstra(int startNode) const;
};

// ============================================================
// 坐标转换工具（统一管道，杜绝偏移）
// ============================================================
namespace CoordTransform {
    inline float WorldToU(float worldX, const MapConfig& cfg) {
        float u = worldX * cfg.scaleX + cfg.offsetU;
        if (cfg.flipX) u = 1.0f - u;
        return u;
    }
    inline float WorldToV(float worldY, const MapConfig& cfg) {
        float v = worldY * cfg.scaleY + cfg.offsetV;
        if (cfg.flipY) v = 1.0f - v;
        return v;
    }
    inline ImVec2 WorldToUV(const Vector3A& pos, const MapConfig& cfg) {
        return ImVec2(WorldToU(pos.X, cfg), WorldToV(pos.Y, cfg));
    }

    inline float UVToX(float u, const MapConfig& cfg) {
        if (cfg.flipX) u = 1.0f - u;
        return (u - cfg.offsetU) / cfg.scaleX;
    }
    inline float UVToY(float v, const MapConfig& cfg) {
        if (cfg.flipY) v = 1.0f - v;
        return (v - cfg.offsetV) / cfg.scaleY;
    }
    inline Vector3A UVToWorld(float u, float v, float z, const MapConfig& cfg) {
        return Vector3A(UVToX(u, cfg), UVToY(v, cfg), z);
    }

    // 从全局参数构造 fallback config
    MapConfig MakeFallbackConfig(float g_scale_x, float g_scale_y,
                                  float g_offset_u, float g_offset_v,
                                  bool g_flip_x, bool g_flip_y);
}
