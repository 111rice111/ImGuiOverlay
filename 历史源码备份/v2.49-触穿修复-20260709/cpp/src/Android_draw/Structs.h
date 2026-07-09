#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <unordered_map>
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

// ============================================================
// AppContext — 全局状态聚合基础设施 (v2.37)
// 目的: 未来逐步将 g_xxx 全局变量迁移到 ctx().xxx
// 当前: 仅定义结构体, 不修改任何现有代码
// ============================================================
#define MAX_MAP_COUNT 50
#define MAX_FLOOR_COUNT 3

struct AppContext {
    std::vector<std::vector<MapConfig>> all_maps;
    int current_map_index = -1, current_floor_index = 0;
    unsigned int map_textures[MAX_MAP_COUNT][MAX_FLOOR_COUNT] = {{0}};
    int map_texture_w[MAX_MAP_COUNT][MAX_FLOOR_COUNT] = {{0}};
    int map_texture_h[MAX_MAP_COUNT][MAX_FLOOR_COUNT] = {{0}};
    float map_pos_x = 700.0f, map_pos_y = 200.0f;
    float map_display_size = 800.0f;
    float map_scale_x = 1.0f, map_scale_y = 1.0f, map_offset_u = 0.0f, map_offset_v = 0.0f;
    bool map_flip_x = false, map_flip_y = false, map_calibrated = false;
    bool map_enabled = false, map_auto_detect = true;
    bool show_map_status = false, show_nav_line = false, show_big_map = false;
    float map_label_scale = 1.0f, big_map_zoom = 1.0f;
    float treasure_threshold = 5000.0f;
    char texture_status[512] = "等待加载...", map_detect_debug[1024] = "";
    std::vector<MusicboxKey> musicbox_db; std::vector<PianoKey> piano_db; std::vector<ChairKey> chair_db;
    std::vector<MapFingerprint> fingerprint_db;
    std::unordered_map<int,int> mapidx_from_fp_id, fp_id_from_mapidx;
    Vector3A detected_musicbox_pos, detected_piano_pos;
    std::vector<Vector3A> detected_chairs, cached_chairs;
    int detect_phase = 0;
    float detect_best_score = 0.0f; int detect_best_fp_id = -1;
    bool has_cached_musicbox = false, has_cached_piano = false;
    std::vector<std::vector<std::vector<Vector3A>>> exits;
    std::vector<std::vector<std::vector<ImVec2>>> exit_uvs;
    std::vector<std::vector<Vector3A>> saved_paths, current_drawing_path, pending_path, nav_render_path;
    std::vector<char> path_visible; std::vector<unsigned int> path_colors;
    std::vector<std::vector<std::vector<std::vector<Vector3A>>>> saved_paths_by_map;
    std::vector<std::vector<ImVec2>> path_vertex_cache;
    bool path_cache_dirty = true, draw_path_mode = false, pending_save_confirm = false;
    int selected_path_index = -1, path_edit_mode = 0;
    float pending_save_timeout = 0.0f, path_fade_dist = 3000.0f;
    bool dirty_exits = false, dirty_paths = false; int dirty_flush_counter = 0;
    bool dest_select_mode = false;
    float dest_world_x = 0, dest_world_y = 0, dest_world_z = 0;
    float map_opacity = 0.55f, label_opacity = 1.0f, self_opacity = 1.0f, route_opacity = 1.0f;
    float saved_path_opacity = 0.6f;
    bool show_3d_paths = false; float path_3d_height = 10.0f; int path_3d_style = 2;
    float path_3d_fade_dist = 30.0f, line_3d_width = 3.0f, dot_3d_radius = 6.0f, flow_3d_speed = 1.0f;
    int drag_exit_idx = -1, del_exit_idx = -1;
    bool map_drag_blocked = false, path_drawing_active = false;
    Vector3A path_start_pos;
    char save_notification[128] = ""; float notification_timer = 0.0f;
    std::vector<std::vector<Vector3A>> import_preview_paths;
    bool has_import_preview = false; int import_preview_map_idx = -1, import_preview_floor_idx = -1;
    int import_tex_w = 0, import_tex_h = 0; char import_txt_path[256] = {};
    std::string import_status; int import_success_count = 0;
    int last_rendered_map_index = -1, last_rendered_floor_index = -1;
    char map_scores_buf[2048] = "", score_debug_buf[2048] = "";
    char detect_status_text[256] = "等待识别";
    Vector3A prev_player_pos; bool has_prev_pos = false, musicbox_moved = false;
    bool new_map_prompt_shown = false;
    int detect_debounce_frames = 0, low_confidence_counter = 0, locked_stable_frames = 0;
};
static AppContext& ctx() { static AppContext c; return c; }
