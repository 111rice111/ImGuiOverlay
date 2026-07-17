#pragma once

// ============================================================
// draw_Gui_internal.h — draw_Gui 拆分后的内部共享头文件 (v2.47)
// 声明所有跨文件共享的全局变量、函数原型、结构体、命名空间
// 原 draw_Gui.cpp 中的 static 变量/函数在此处改为 extern/非 static
// ============================================================

#include <zlib.h>
#include "Name.h"
#include "ThreadAffinity.h"
#include "draw.h"
#include "千叶.h"
#include "secure_runtime.h"
#include "game_offsets.h"
#include "SoHookIntegration.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <cstring>
#include <mutex>
#include <sched.h>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fstream>
#include <sstream>
#include <random>
#include <filesystem>
#include <map>
#include <cctype>
#include <utility>
#include <cstdarg>
#include <list>
#include <deque>
#include <chrono>
#include <GLES2/gl2.h>
#include "anti_debug.h"
#include <set>
#include <queue>
#include "Structs.h"
#include "DataManager.h"
#include "json.hpp"
#include "ui_resources/ui_bg.h"
#include "ui_resources/ui_avatar.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

#define MAPS_ROOT "/data/local/bin/maps/"
#define IMGUI_DISABLE_DEMO_WINDOWS

// ========== 外部声明（来自 main.cpp / 其他文件）==========
extern char extractedString[64];
extern std::atomic<int> pid;

// ========== 宏常量 ==========
inline constexpr int SWITCH_CONFIRM_REQUIRED = 30;
inline constexpr int DIRTY_FLUSH_INTERVAL = 1800;
inline constexpr int DETECT_DEBOUNCE_FRAMES = 12;
inline constexpr int LOW_CONFIDENCE_TIMEOUT = 180;
inline constexpr float TELEPORT_THRESHOLD_XY = 80.0f;
inline constexpr float TELEPORT_THRESHOLD_Z = 50.0f;
inline constexpr float kPathSnapThreshold = 80.0f;
inline constexpr int TEX_UPLOAD_ROWS_PER_FRAME = 256;
inline constexpr int MAX_HISTORY = 20;
inline const float 距离比例 = 11.886f;

// ========== 枚举 ==========
enum class MapDetectPhase : int {
    LOCKED = 0,
    SWITCH_DETECTED = 1,
    IDENTIFYING = 2,
    LOW_CONFIDENCE = 3,
};

enum class TeleportType : int { NONE = 0, MAP_SWITCH = 1, FLOOR_CHANGE = 2 };

enum class ObjSubClass {
    Unknown = 0, Trap, CipherMachine, Clip, Cat, Lion, Cellar, Box, Chair, Pallet, Prop, Player, Boss, Ghost
};

enum class ButtonVariant { Primary, Success, Danger, Secondary };

// ========== 结构体 ==========
struct Notification {
    std::string text;
    float timer;
    ImVec4 color;
};

struct MapScoreResult {
    int fp_id = -1;
    float score = 0.0f;
    float scores[4] = {};
    bool is_tie = false;
    float second_score = 0.0f;
    std::string debug_text;
};

struct RoleInfo {
    int index = 0;
    int campId = -1;
    int roleId = -1;
    bool isRoleObtained = false;
};

struct DataStruct {
    uintptr_t obj{};
    uintptr_t objcoor{};
    int action{};
    int 阵营{};
    float 状态数值{};
    int 实体特征码{};
    ObjSubClass sub_type{ObjSubClass::Unknown};
    char str[256]{};
    char 类名[256]{};
    char prop_name[64]{};
    bool is_ghost{};
    int meeting_seat = -1;
};

struct ObjLocalCache {
    int 实体特征码;
    char pad1[0x1A0 - 0x70 - 4];
    float 状态数值;
};

struct MirrorInfo {
    Vector3A survivorPos;
    Vector3A mirrorPos;
    char name[256];
};

struct ModuleBssInfo {
    unsigned long addr{};
    unsigned long taddr{};
};

struct PendingTex {
    int map, floor;
    unsigned char* px;
    int w, h;
    bool ready, fail, uploaded;
    GLuint tex;
    int upload_row;
};

struct SyncResult { int musicbox = 0; int piano = 0; int chairs = 0; };

struct UITheme {
    ImVec4 bg_dark, bg_panel, bg_card, bg_card_hover, bg_input;
    ImVec4 bg_hover, bg_active, bg_overlay;
    ImVec4 primary, primary_hover, primary_active, primary_soft;
    ImVec4 success, success_hover, success_active;
    ImVec4 danger, danger_hover, danger_active;
    ImVec4 warning, info;
    ImVec4 text, text_muted, text_title, text_on_primary;
    ImVec4 border, border_strong, border_light;
    ImVec4 check_mark, slider_grab, slider_grab_active;
};

struct StyleBackup {
    float WindowRounding, FrameRounding, ChildRounding, PopupRounding;
    float ScrollbarRounding, GrabRounding, TabRounding;
    ImVec2 WindowPadding, FramePadding, ItemSpacing, ItemInnerSpacing, CellPadding, TouchExtraPadding;
    float IndentSpacing, ScrollbarSize;
    ImVec4 Colors[ImGuiCol_COUNT];
};

struct PlayerInfo {
    std::string name;
    std::vector<std::string> talents;
    std::vector<int> talent_ids;
    std::vector<std::string> skill_names;
    std::vector<int> skill_ids;
    int unit_type = 0;
    std::string camp;
};

struct TalentState {
    std::vector<PlayerInfo> players;
    std::string status = "等待数据...";
    std::chrono::steady_clock::time_point last_check;
};

// ========== 命名空间: GlobalMemory ==========
namespace GlobalMemory {
    extern uintptr_t libbase;
    extern uintptr_t Arrayaddr;
    extern long int Count;
    extern uintptr_t Matrix;
    extern uintptr_t 自身;
    extern int 数量;
    extern long int MatrixOffset;
    extern long int ArrayaddrOffset;
    extern int 状态;
    extern const char *libso;
    extern long int ModulePagesCount;
    extern uintptr_t bss_base;
}

// ========== 命名空间: MjSubsystem ==========
namespace MjSubsystem {
    extern bool draw_props;
    extern bool show_distance;
    extern float high_value_threshold;
    extern bool show_monsters;
    extern bool show_big_chest;
    extern bool show_small_chest;
    extern bool show_traps;
    extern bool show_interactables;
    extern bool show_high_value;
    extern bool show_low_value;
    extern float max_dist_monsters;
    extern float max_dist_big_chest;
    extern float max_dist_small_chest;
    extern float max_dist_traps;
    extern float max_dist_interactables;
    extern float max_dist_high_value;
    extern float max_dist_low_value;
    extern std::unordered_set<std::string> mj_special_classes;

    void Init();
    bool IsMjPropClass(std::string_view cls);
    bool IsMjSpecialClass(std::string_view cls);
    bool ShouldBypassFilter();
    bool ShouldShowGhost();
}

// ========== 命名空间: FastMath (全 inline, 定义在头文件) ==========
namespace FastMath {
    inline float fastDistanceSquared(const Vector3A &a, const Vector3A &b) noexcept {
        float dx = a.X - b.X;
        float dy = a.Y - b.Y;
        float dz = a.Z - b.Z;
        return dx * dx + dy * dy + dz * dz;
    }
    inline float fastDistance(const Vector3A &a, const Vector3A &b) noexcept {
        return std::sqrt(fastDistanceSquared(a, b));
    }
    inline Vector3A CalculateSurvivorMirrorPos(Vector3A survivorPos,
                                               Vector3A mirrorCenter,
                                               Vector3A normalVec) noexcept {
        float len = std::sqrt(normalVec.X * normalVec.X + normalVec.Y * normalVec.Y);
        if (len < 0.001f) return {0, 0, 0};
        float nx = normalVec.X / len;
        float ny = normalVec.Y / len;
        float dx = survivorPos.X - mirrorCenter.X;
        float dy = survivorPos.Y - mirrorCenter.Y;
        float dist = (dx * nx + dy * ny);
        Vector3A reflectedPos;
        reflectedPos.X = survivorPos.X - 2.0f * dist * nx;
        reflectedPos.Y = survivorPos.Y - 2.0f * dist * ny;
        reflectedPos.Z = survivorPos.Z;
        return reflectedPos;
    }
}

// ========== inline 函数 (定义在头文件) ==========
inline ImVec2 operator-(const ImVec2 &a, const ImVec2 &b) noexcept {
    return {a.x - b.x, a.y - b.y};
}

template <typename T> inline T SnapToPixel(T v) noexcept {
    return static_cast<T>(std::lrint(v));
}

inline Vector3A getObjectCoordinates(uintptr_t coorBase, bool isProp = false) noexcept {
    Vector3A pos{};
    if (coorBase) {
        pos.X = getFloat(coorBase + GAME_OFFSET(coord_x, 0xA0));
        pos.Y = getFloat(coorBase + GAME_OFFSET(coord_y, 0xA8));
        pos.Z = getFloat(coorBase + GAME_OFFSET(coord_z, 0xA4));
        if (isProp) pos.Z -= 8.5f;
    }
    return pos;
}

inline bool optimizedWorldToScreen(const Vector3A &worldPos,
                                   const float *matrix, float px, float py,
                                   float &screenX, float &screenY,
                                   float &screenW) noexcept {
    const float w = matrix[3] * worldPos.X + matrix[7] * worldPos.Z +
                    matrix[11] * worldPos.Y + matrix[15];
    if (w <= 0.5f) return false;
    const float invW = 1.0f / w;
    screenX = px + (matrix[0] * worldPos.X + matrix[4] * worldPos.Z +
                    matrix[8] * worldPos.Y + matrix[12]) * invW * px;
    screenY = py - (matrix[1] * worldPos.X + matrix[5] * (worldPos.Z + 8.5f) +
                    matrix[9] * worldPos.Y + matrix[13]) * invW * py;
    screenW = py - (matrix[1] * worldPos.X + matrix[5] * (worldPos.Z + 28.5f) +
                    matrix[9] * worldPos.Y + matrix[13]) * invW * py;
    return true;
}

// ========== 全局变量 (extern, 定义在 draw_Gui.cpp) ==========
// --- 调试 ---
extern FILE* g_debug_file;
extern std::mutex g_debug_mutex;
extern std::atomic<bool> g_debug_enabled;
extern char g_debug_file_path[256];
extern std::unordered_set<std::string> g_logged_entities;

// --- 过滤器 ---
extern float Global_Filter_Max_Abs_XY;
extern float Global_Filter_Min_Z;
extern float Global_Filter_Max_Z;
extern float Global_Filter_Max_Distance;

// --- UI 状态 ---
extern bool MemuSwitch;
extern float g_minimized_bar_anim;
extern ImVec2 g_bar_custom_pos;
extern bool voice;
extern bool show_mimic_overlay;
extern bool g_MimicModeEnabled;

// --- 会议 ---
extern bool is_meeting_detected;
extern float meeting_center_x;
extern float meeting_center_y;
extern int meeting_total_seats;
extern float meeting_radius;
extern std::unordered_map<std::string, int> bound_seat_by_class;

// --- 场景物体 ---
extern Vector3A g_detected_musicbox_pos;
extern Vector3A g_detected_piano_pos;
extern int g_last_rendered_map_index;
extern int g_last_rendered_floor_index;
extern char g_map_scores_buf[2048];

// --- 出口 ---
extern std::vector<std::vector<std::vector<Vector3A>>> g_exits;
extern std::vector<std::vector<std::vector<ImVec2>>> g_exit_uvs;

// --- 地图渲染参数 ---
extern float g_map_opacity;
extern float g_label_opacity;
extern float g_self_opacity;
extern float g_route_opacity;
extern float g_saved_path_opacity;
extern float g_path_fade_dist;

// --- 路径绘制 ---
extern bool g_draw_path_mode;
extern std::vector<Vector3A> g_current_drawing_path;
extern int g_selected_path_index;
extern std::vector<std::vector<Vector3A>> g_saved_paths;
extern std::vector<char> g_path_visible;
extern std::vector<ImU32> g_path_colors;
extern std::vector<std::vector<std::vector<std::vector<Vector3A>>>> g_saved_paths_by_map;
extern int g_last_paths_map_idx;
extern int g_last_paths_floor_idx;
extern std::vector<std::vector<ImVec2>> g_path_vertex_cache;
extern bool g_path_cache_dirty;
extern float g_last_cache_map_pos_x, g_last_cache_map_pos_y, g_last_cache_map_size;
extern int g_path_edit_mode;
extern std::vector<Vector3A> g_pending_path;
extern bool g_pending_save_confirm;
extern float g_pending_save_timeout;
extern float g_draw_map_size_bak;
extern float g_draw_map_posx_bak;
extern float g_draw_map_posy_bak;

// --- 导航 ---
extern bool g_dest_select_mode;
extern float g_dest_world_x, g_dest_world_y, g_dest_world_z;
extern std::vector<Vector3A> g_nav_render_path;

// --- 导入 ---
extern std::vector<std::vector<Vector3A>> g_import_preview_paths;
extern bool g_has_import_preview;
extern int g_import_preview_map_idx;
extern int g_import_preview_floor_idx;
extern int g_import_tex_w, g_import_tex_h;
extern char g_import_txt_path[256];
extern int g_import_success_count;
extern std::string g_import_status;

// --- 大地图 ---
extern bool g_show_big_map;
extern float g_big_map_zoom;

// --- 脏数据 ---
extern bool g_dirty_exits;
extern bool g_dirty_paths;
extern int g_dirty_flush_counter;

// --- 通知 ---
extern std::deque<Notification> g_notifications;

// --- 校准历史 ---
extern std::vector<std::pair<float,float>> g_pt1_history;
extern std::vector<std::pair<float,float>> g_pt2_history;
extern std::vector<std::vector<Vector3A>> g_path_history;
extern float hold_u1l, hold_u1r, hold_v1u, hold_v1d;
extern float hold_u2l, hold_u2r, hold_v2u, hold_v2d;
extern float g_press_timer;
extern ImVec2 g_press_pos;
extern float popup_u, popup_v;
extern int g_del_exit_idx;
extern ImVec2 g_last_exit_screen_pos;
extern ImVec2 g_last_exit_rendered_pos;
extern bool g_show_exit_debug;
extern int g_drag_exit_idx;
extern bool g_map_drag_blocked;
extern bool g_path_drawing_active;
extern ImVec2 g_path_start_pos;

// --- 路径绘制参数 ---
extern float g_smooth_strength;
extern bool g_enable_snap;
extern float g_grid_spacing;
extern float g_snap_distance;
extern bool g_show_grid;
extern float g_grid_alpha;
extern bool g_show_3d_paths;
extern float g_3d_path_height;
extern int g_3d_path_style;
extern float g_3d_path_fade_dist;
extern float g_3d_line_width;
extern float g_3d_dot_radius;
extern float g_3d_flow_speed;
extern bool g_show_saved_paths;
extern bool g_ortho_draw;
extern float g_path_draw_threshold;

// --- 重初始化 ---
extern std::atomic<bool> g_need_reinit;
extern std::atomic<int> g_saved_pid;

// --- 地图纹理 ---
extern GLuint g_map_textures[MAX_MAP_COUNT][MAX_FLOOR_COUNT];
extern int g_map_texture_w[MAX_MAP_COUNT][MAX_FLOOR_COUNT];
extern int g_map_texture_h[MAX_MAP_COUNT][MAX_FLOOR_COUNT];

// --- 指纹 ---
extern std::vector<MapFingerprint> g_fingerprint_db;
extern std::vector<int> g_mapidx_from_fp_id;
extern std::vector<int> g_fp_id_from_mapidx;

// --- 检测状态 ---
extern MapDetectPhase g_detect_phase;
extern int g_detect_debounce_frames;
extern int g_low_confidence_counter;
extern int g_locked_stable_frames;
extern float g_detect_best_score;
extern int g_detect_best_fp_id;
extern char g_detect_status_text[256];
extern Vector3A g_prev_player_pos;
extern bool g_has_prev_pos;
extern bool g_musicbox_moved;
extern Vector3A g_cached_musicbox_pos;
extern bool g_has_cached_musicbox;
extern std::vector<Vector3A> g_cached_chairs;
extern Vector3A g_cached_piano_pos;
extern bool g_has_cached_piano;
extern bool g_new_map_prompt_shown;
extern char g_score_debug_buf[2048];

// --- 地图 UI ---
extern bool g_map_enabled;
extern float g_map_display_size;
extern bool g_use_calib;
extern bool g_show_map_status;
extern std::string g_save_notification;
extern float g_notification_timer;
extern float g_pt1_wx, g_pt1_wy;
extern float g_pt1_tu, g_pt1_tv;
extern float g_pt2_wx, g_pt2_wy;
extern float g_pt2_tu, g_pt2_tv;
extern int g_drag_point;
extern float g_map_pos_x;
extern float g_map_pos_y;
extern bool g_map_auto_detect;
extern float g_treasure_threshold;
extern float g_map_scale_x;
extern float g_map_scale_y;
extern float g_map_offset_u;
extern float g_map_offset_v;
extern bool g_map_flip_x;
extern bool g_map_flip_y;
extern bool g_show_nav_line;
extern float g_map_label_scale;

// --- 地图数据库 ---
extern std::vector<std::vector<MapConfig>> g_all_maps;
extern int g_current_map_index;
extern int g_current_floor_index;
extern std::vector<MusicboxKey> g_musicbox_db;
extern std::vector<PianoKey> g_piano_db;
extern std::vector<ChairKey> g_chair_db;
extern std::vector<Vector3A> g_detected_chairs;

// --- 盖板/触摸 ---
extern bool show_draw_MarktheSoul;
extern bool wood_enabled;
extern ImTextureID g_tex_ui_bg;
extern ImTextureID g_tex_ui_avatar;
extern int g_tex_ui_bg_w, g_tex_ui_bg_h;
extern int g_tex_ui_avatar_w, g_tex_ui_avatar_h;
extern bool g_ui_textures_loaded;
extern float wood_touch_pct_x;
extern float wood_touch_pct_y;
extern int g_last_display_w, g_last_display_h;
extern float wood_touch_x;
extern float wood_touch_y;
extern float wood_offset_x;
extern float wood_offset_y;
extern float wood_length;
extern float wood_width;
extern float wood_trigger_dist;
extern float wood_cooldown_dur;
extern bool show_wood_rect;
extern Vector3A g_wood_viz_pos;
extern float g_wood_viz_angle;
extern bool g_wood_viz_valid;
extern Vector3A g_wood_viz_pos_prev;
extern bool g_was_hunter_inside;
extern float g_wood_cooldown;
extern float g_last_wood_trigger_time;
extern bool show_touch_point;
extern bool g_show_wood_diag;
extern float g_last_touch_x, g_last_touch_y;
extern float g_last_touch_time;
extern float g_wood_popup_time;
extern int g_touch_max_x;
extern int g_touch_max_y;
extern char g_touch_path[128];
extern bool g_touch_ready;
extern int g_calib_step;
extern float g_calib_inj[4][2];
extern float g_calib_obs_buf[4][2];
extern bool g_calib_done;
extern float g_calib_A, g_calib_B, g_calib_C;
extern float g_calib_D, g_calib_E, g_calib_F;

// --- 绘制开关 ---
extern ImColor g_BoxColor_Survivor;
extern ImColor g_BoxColor_Hunter;
extern ImColor g_BoxColor_Ghost;
extern bool show_draw_EnhancedFrame;
extern bool show_draw_Line;
extern bool show_draw_QY;
extern bool show_draw_sender;
extern bool show_draw_Animal;
extern bool show_draw_Name;
extern bool show_draw_Distance;
extern bool show_draw_Cellar;
extern bool show_draw_Chair;
extern bool show_draw_BANZI;
extern bool show_draw_BoxItem;
extern bool show_draw_Prop;
extern bool show_draw_prophet;
extern bool show_draw_redqueen;
extern bool Debugging;
extern bool disable_skip_filter;
extern bool inform_ghost;
extern bool g_talent_view;
extern bool g_show_detailed;
extern bool g_draw_bones;
extern bool g_draw_bone_uid;
extern bool g_draw_generators;
extern bool g_draw_exit_gates;
extern bool g_draw_basements;
extern std::string g_ConfigPath;
extern int g_chair_dist;
extern int g_board_dist;
extern int g_box_dist;
extern int g_sender_dist;
extern std::string g_current_game_package;
extern int g_current_game_pid;

// --- 过滤缓存 ---
extern std::unordered_map<std::string, bool> skipClassCache;
extern std::unordered_map<std::string, bool> fakeHunterCache;

// --- 渲染变量 ---
extern ImVec2 circle_pos;
extern ImFont *g_font_ui;
extern float circle_radius;
extern float margin;
extern std::string 过滤类名, 类名;
extern uint32_t orientation;
extern float g_ui_density;
extern ImFont *g_main_font;
extern ImFont *g_ui_font;
extern Vector3A D, Z, M;
extern float z_x, z_y, z_z, d_x, d_y, d_z, camera, r_x, r_y, r_w;
extern float X1, Y1, X2, Y2, W, H;
extern char objtext[256];
extern char 监管者预知[1024];
extern float px, py;

// --- 数据缓冲 ---
extern std::vector<DataStruct> data_buffers[2];
extern std::atomic<int> front_buffer_idx;
extern std::mutex data_mutex;
extern std::unordered_map<uintptr_t, int> g_meeting_seat_map;
extern std::vector<MirrorInfo> g_mirrorList;
extern bool g_holdMirror;
extern Vector3A g_mirrorCenter;
extern Vector3A g_mirrorNormal;
extern std::atomic<int> g_selfAction;

// --- 调试计数 ---
extern int g_debug_scanned_count;
extern int g_debug_player_count;
extern int g_debug_boss_count;
extern bool g_debug_self_found;
extern float g_debug_last_cam_z;
extern char g_debug_self_cls[128];

// --- 字体 ---
extern bool fonts_initialized;

// --- 纹理加载器 ---
extern std::vector<PendingTex> g_pending;
extern std::mutex g_pending_mtx;
extern std::thread g_loader;
extern bool g_loader_on;

// --- UI 主题 ---
extern UITheme g_theme;

// --- 其他全局变量 (供拆分文件使用) ---
extern float matrix[16];
extern char g_texture_status[512];
extern char g_map_detect_debug[1024];

// --- 全局角色 ---
extern std::vector<RoleInfo> global_validRoles;
extern std::mutex mimic_mutex;
extern std::atomic<bool> is_scanning_mimic;
extern bool g_talent_need_refresh;

// ========== 常量映射 (extern, 定义在 draw_Gui.cpp) ==========
extern const std::vector<std::pair<std::string, std::string>> g_prop_name_map;
extern const std::vector<std::string> g_game_packages;
extern const std::map<int, std::string> SURVIVOR_TALENT_MAP;
extern const std::map<int, std::string> BUTCHER_TALENT_MAP;
extern const std::map<int, std::string> SKILL_MAP;

// ========== 函数原型 ==========

// --- 地图配置 ---
void LoadMapConfigFromJSON();
void LoadFingerprintDB();
void RebuildFingerprintMapping();
MapScoreResult ScoreMapFingerprints(const Vector3A&, bool, const Vector3A&, bool, const std::vector<Vector3A>&);
TeleportType DetectPlayerTeleport(const Vector3A&);
void ResetObjectCacheOnMapSwitch();
void ExecuteMapSwitch(int fp_id);
void TryAutoDetectMap(const std::vector<DataStruct>& data);
void UpdateCurrentFloor();
void LoadMapTexture(int mapIdx, int floorIdx);
bool IsPlayerInMapBounds(int mapIndex, const Vector3A& playerPos);
int FindMapByPlayerPos(const Vector3A& playerPos);
void LoaderLoop();
void StartLoader();
void FlushTextures();
int GetFloorFromPlayerZ(const Vector3A& pos);
int SafeClampFloorIdx(int mapIdx, int floorIdx);
const MapConfig& GetActiveMapConfig();
void MarkExitsDirty();
void MarkPathsDirty();
void FlushDirtyData();
void AddNotification(const std::string& text, float duration, ImVec4 color);

// --- 配置 ---
void LoadConfig();
void SaveConfig();
void SaveExitsToJSON(int mapIdx, int floorIdx);
void SavePlayerPathsToJSON(int mapIdx, int floorIdx);
SyncResult SaveSceneObjectsToJSON(int mapIdx, int floorIdx);
int DetectGameProcess(std::string& out_package);

// --- 地图覆盖 ---
void Draw_MapOverlay(ImDrawList* Draw, const std::vector<DataStruct>& data);
void RDPRecursive(const std::vector<ImVec2>& points, int start, int end,
                  std::vector<char>& keep, float epsilon);
std::vector<ImVec2> SimplifyPathRDP(const std::vector<ImVec2>& points, float epsilon);
bool LineSegmentsIntersect2D(const Vector3A& p1, const Vector3A& p2,
                             const Vector3A& p3, const Vector3A& p4);

// --- 实体绘制 ---
void ProcessObjectWithFullDetails(ImDrawList *Draw, const DataStruct &item,
                                  const std::vector<DataStruct>& data, int idx);
void Draw_Main_Optimized(ImDrawList *Draw);
void Draw_Main(ImDrawList *Draw);
void scan_mimic_roles();
bool should_filter_cached(std::string_view cn) noexcept;
bool IsFakeHunter_cached(std::string_view name) noexcept;
std::string RoleIdToChinese(int id);

// --- 读取线程 ---
void read_thread(long int PD1, long int PD2, long int PD3);
void WriteDebugLog(const char* fmt, ...);
void OpenDebugLog();
void CloseDebugLog();

// --- 触摸 ---
bool InitTouch();
void SimulateClick(int x, int y);
void AutoWoodCheck();

// --- 天赋 ---
bool parse_pickle_talents(TalentState& state, const std::string& pickle_path);
void parse_talent_json(TalentState& state, const std::string& json_path, bool detailed);
void show_talent_viewer();
fs::path find_snapshot_file(const fs::path& search_dir);

// --- UI 主题 ---
void InitModernUITheme();
float Lerp(float a, float b, float t);
ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t);
StyleBackup BackupImGuiStyle();
void RestoreImGuiStyle(const StyleBackup& bak);
void ApplyModernUIStyle(float density);
bool StyledButton(const char* label, ButtonVariant variant = ButtonVariant::Primary,
                  const ImVec2& size = ImVec2(0,0), float density = 1.0f);
void StyledSectionHeader(const char* label, const ImVec4& color = ImVec4(0,0,0,0), float density = 1.0f);
void StyledCardBegin(const char* id, const ImVec2& size, float density,
                     ImDrawList* draw_list, ImVec2& out_pos, ImVec2& out_size);
void StyledCardEnd();

// --- 模块查找 ---
ModuleBssInfo get_module_bss(int pid, const char *module_name);
ModuleBssInfo get_module_bssgjf(int pid, const char *module_name);
int get_name_pid1(const char *packageName);
long getModuleBasegjf(int pid, const char *module_name);
int ExtractPrice(const char* prop_name);

// --- UI 纹理 ---
void LoadUITextures();
void InvalidateMapTextures();
void ResetMapState();

// --- inline 辅助函数 (需要全局变量, 延迟到定义后) ---
// 路径简化用的点到线段距离平方（RDP 算法使用）
inline float PointToSegmentDistanceSq(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    ImVec2 ab = {b.x - a.x, b.y - a.y};
    ImVec2 ap = {p.x - a.x, p.y - a.y};
    float t = (ap.x * ab.x + ap.y * ab.y) / (ab.x * ab.x + ab.y * ab.y + 1e-6f);
    t = std::clamp(t, 0.0f, 1.0f);
    ImVec2 closest = {a.x + ab.x * t, a.y + ab.y * t};
    float dx = p.x - closest.x, dy = p.y - closest.y;
    return dx * dx + dy * dy;
}

inline bool isValidCoordinate(const Vector3A &pos) noexcept {
    if (pos.X == 0.0f || pos.Y == 0.0f) return false;
    if (std::isnan(pos.X) || std::isnan(pos.Y) || std::isnan(pos.Z)) return false;
    if (std::isinf(pos.X) || std::isinf(pos.Y) || std::isinf(pos.Z)) return false;
    return !(std::fabs(pos.X) > Global_Filter_Max_Abs_XY || std::fabs(pos.Y) > Global_Filter_Max_Abs_XY);
}

inline bool isValidScreenPosition(float x, float y, float width, float height) noexcept {
    return !(x < -width || x > displayInfo.width + width || y < -height ||
             y > displayInfo.height + height || width <= 0.0f || height <= 0.0f ||
             width > displayInfo.width * 2 || height > displayInfo.height * 2);
}

// 颜色常量
inline constexpr ImColor 红色(255, 50, 50, 255), 绿色(50, 255, 50, 255),
        蓝色(50, 150, 255, 255), 黄色(255, 255, 50, 255), 紫色(200, 100, 255, 255),
        黑色(0, 0, 0, 255), 亮红色(255, 50, 50, 255), 白色(255, 255, 255, 255),
        密码机色(255, 220, 80, 255), 板子色(255, 255, 255, 255),
        箱子色(255, 105, 180, 255), 椅子色(255, 100, 100, 255),
        地窖色(200, 0, 255, 255);

inline void DrawTriangle(ImDrawList *Draw, float centerX, float centerY,
                         ImColor color, int distance, float thickness = 2.0f) noexcept {
    if (color == 白色 && show_draw_MarktheSoul) {
        float size = std::clamp(15.0f - (distance / 20.0f), 5.0f, 15.0f);
        size = std::min(size, W * 0.3f);
        ImVec2 p1 = {centerX, centerY - size};
        ImVec2 p2 = {centerX - size * 0.866f, centerY + size * 0.5f};
        ImVec2 p3 = {centerX + size * 0.866f, centerY + size * 0.5f};
        Draw->AddTriangle(p1, p2, p3, color, thickness);
    }
}

inline void DrawEnhancedFrame(ImDrawList *Draw, float x1, float y1, float x2, float y2,
                              ImColor color, int distance) noexcept {
    const float currentWidth = x2 - x1;
    const float currentHeight = y2 - y1;
    const float cornerRadius = std::clamp(std::min(currentWidth, currentHeight) * 0.15f, 2.0f, 8.0f);
    const float thickness = std::clamp(2.5f - (distance / 100.0f), 1.2f, 2.5f);
    Draw->AddRect({x1, y1}, {x2, y2}, color, cornerRadius, ImDrawFlags_RoundCornersAll, thickness);
}
