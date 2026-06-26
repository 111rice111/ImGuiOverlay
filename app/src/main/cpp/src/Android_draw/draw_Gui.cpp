#include <zlib.h>
#include "Name.h"
#include "ThreadAffinity.h"
#include "draw.h"
#include "千叶.h"
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
#include <cstring>
#include <linux/input.h>
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
#include <unordered_map>
#include <random>
#include <filesystem>
#include <map>
#include <vector>
#include <cctype>
#include <filesystem>
#include <chrono>
#include <map>
#include <cctype>
#include <cstdlib>
#include <utility>
#include "json.hpp"
#include <cstdarg>
#include <list>
#include <deque>
#include <GLES2/gl2.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <set>
#include <queue>

using json = nlohmann::json;

static void LoadMapConfigFromJSON();

struct Vector3A {
    float X{}, Y{}, Z{};
    constexpr Vector3A() = default;
    constexpr Vector3A(float x, float y, float z) noexcept : X(x), Y(y), Z(z) {}
    bool IsZero() const { return X == 0.0f && Y == 0.0f && Z == 0.0f; }
};

inline ImVec2 operator-(const ImVec2 &a, const ImVec2 &b) noexcept {
    return {a.x - b.x, a.y - b.y};
}

bool g_talent_need_refresh = false;

#define IMGUI_DISABLE_DEMO_WINDOWS

extern char extractedString[64];
extern std::atomic<int> pid;
static FILE* g_debug_file = nullptr;
static std::mutex g_debug_mutex;
static std::atomic<bool> g_debug_enabled{false};
static char g_debug_file_path[256] = {0};
static std::unordered_set<std::string> g_logged_entities;

static float Global_Filter_Max_Abs_XY = 10000.0f;
static float Global_Filter_Min_Z = -300.0f;
static float Global_Filter_Max_Z = 300.0f;
static float Global_Filter_Max_Distance = 300.0f;

static bool g_new_map_prompted = false;
static bool MemuSwitch = true;
static bool voice = true;
static bool show_mimic_overlay = false;

static int g_new_map_frame_counter = 0;

static bool is_meeting_detected = false;
static float meeting_center_x = 0.0f;
static float meeting_center_y = 0.0f;
static int meeting_total_seats = 12;
static float meeting_radius = 30.0f;
static std::unordered_map<std::string, int> bound_seat_by_class;

static bool g_new_map_detected = false;
static std::chrono::steady_clock::time_point g_new_map_detect_time;
static Vector3A g_detected_musicbox_pos;
static Vector3A g_detected_piano_pos;

// === 音乐盒拾取/放置容错 ===
static int g_last_valid_map_index = -1;        // 最后已知的有效地图索引
static int g_last_valid_floor_index = 0;       // 最后已知的有效楼层
static int g_frames_since_musicbox_lost = 0;   // 音乐盒消失以来的帧数
static const int MUSICBOX_LOST_GRACE_FRAMES = 600; // 音乐盒消失后保留地图的帧数(10秒@60fps)
static bool g_musicbox_moved_on_same_map = false;  // 同一张图上音乐盒被移动了
static int g_last_rendered_map_index = -1;         // 最后一次渲染的地图索引（用于检测地图切换）
static int g_last_rendered_floor_index = -1;       // 最后一次渲染的楼层索引
static int g_map_stable_frames = 0;                // 地图稳定锁定计数

// === 地图切换冷却：防止音乐盒残留数据导致误切换 ===
static int g_switch_candidate_index = -1;      // 候选切换目标地图
static int g_switch_confirm_frames = 0;        // 候选已连续匹配的帧数
static const int SWITCH_CONFIRM_REQUIRED = 30;  // 需要连续30帧(~0.5秒)才确认切换

// 评分显示缓存（用于界面调试）
static char g_map_scores_buf[2048] = "";          // 最近评分信息的字符串缓存

static std::vector<std::vector<std::vector<Vector3A>>> g_exits;

static float g_map_opacity = 1.0f;
static float g_label_opacity = 1.0f;
static float g_self_opacity = 1.0f;
static float g_route_opacity = 1.0f;
static float g_saved_path_opacity = 0.6f;

static bool g_draw_path_mode = false;
static std::vector<Vector3A> g_current_drawing_path;
static int  g_selected_path_index = -1;          // 当前选中的路径索引 (-1=未选中)
static std::vector<std::vector<Vector3A>> g_saved_paths;
static int g_path_edit_mode = 0;

static bool g_show_big_map = false;
static float g_big_map_zoom = 1.0f;

static int g_route_item_count = 0;
static int g_route_total_value = 0;
static float g_total_route_distance = 0.0f;

static Vector3A g_via_point;
static bool g_has_via_point = false;

static int g_route_min_value = 0;

struct Notification {
    std::string text;
    float timer;
    ImVec4 color;
};
static std::deque<Notification> g_notifications;
static void AddNotification(const std::string& text, float duration, ImVec4 color) {
    g_notifications.push_back({text, duration, color});
    if (g_notifications.size() > 5) g_notifications.pop_front();
}

static std::vector<std::pair<float,float>> g_pt1_history;
static std::vector<std::pair<float,float>> g_pt2_history;
static std::vector<std::vector<Vector3A>> g_path_history;
const int MAX_HISTORY = 20;

static float hold_u1l = 0, hold_u1r = 0, hold_v1u = 0, hold_v1d = 0;
static float hold_u2l = 0, hold_u2r = 0, hold_v2u = 0, hold_v2d = 0;

static float g_press_timer = 0;
static ImVec2 g_press_pos;

static float popup_u = 0, popup_v = 0;
static int g_del_exit_idx = 0;

static std::unordered_set<uintptr_t> g_priority_items;

static bool g_path_drawing_active = false;
static ImVec2 g_path_start_pos;

// ========== 新增全局变量：路径平滑与吸附参数 ==========
static float g_smooth_strength = 4.0f;
static bool  g_enable_snap = false;              // 网格吸附已废弃，默认关闭
static float g_grid_spacing = 100.0f;
static float g_snap_distance = 30.0f;
static bool  g_show_grid = false;
static float g_grid_alpha = 0.3f;
static bool  g_show_saved_paths = true;

// === 正交绘制模式（强制水平/垂直 + 直角转角） ===
static bool  g_ortho_draw = false;          // 正交路径绘制开关（默认关闭，自由绘制）
static float g_path_draw_threshold = 15.0f; // 添加新点的最小距离阈值（越小曲线越平滑）

// ========== 新增：可通行图数据结构 ==========
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

static PathGraph g_pathGraph;
static bool g_show_graph_debug = false;
static bool g_graph_ready = false;

namespace GlobalMemory {
    static uintptr_t libbase{};
    static uintptr_t Arrayaddr{};
    static long int Count{};
    static uintptr_t Matrix{};
    static uintptr_t 自身{};
    static int 数量{};
    static long int MatrixOffset = 0;
    static long int ArrayaddrOffset = 0;
    static int 状态{};
    static const char *libso = "libclient.so";
    static long int ModulePagesCount = 0;
    static uintptr_t bss_base{};
}

struct RoleInfo {
    int index = 0;
    int campId = -1;
    int roleId = -1;
    bool isRoleObtained = false;
};

std::string RoleIdToChinese(int id) {
    static const std::unordered_map<int, std::string> roleMap = {
            {-268186264, "猎人"}, {-268186253, "药剂师"}, {-852360755, "顾问"}, {-268186256, "学徒"},
            {-268186252, "巡林员"}, {-268186254, "掮客"}, {-852491827, "棋手"}, {-268186251, "执灯人"},
            {-268186258, "拳击手"}, {63818956,  "烟火师"}, {-268186266, "哨兵"}, {-852688435, "流浪汉"},
            {-852557363, "流浪汉"}, {-268186259, "演说家"}, {-268186260, "修士"}, {-268186263, "香料师"},
            {-268186262, "锁匠"}, {-268186265, "治安官"}, {63819212,  "怪盗"}, {63818188,  "神偷"},
            {63819468,  "催眠师"}, {63818444,  "千面人"}, {63818700,  "阴谋家"}, {-852295219, "降灵师"},
            {774232427, "银行家"}, {774232430, "拳击手"}, {774232434, "掮客"}, {-651165235, "棋手"},
            {-651099699, "清洁工"}, {635030476, "阴谋家"}, {-651034163, "顾问"}, {-651296307, "送货员"},
            {774232421, "侦探"}, {635031756, "处刑人"}, {635030220, "千面人"}, {635030732, "烟火师"},
            {-651230771, "愚人"}, {774232424, "猎人"}, {635031244, "催眠师"}, {-650968627, "降灵师"},
            {774232426, "锁匠"}, {774232431, "灵媒"}, {774232428, "修士"}, {1, "普通人"},
            {774232429, "演说家"}, {635030988, "怪盗"}, {774232435, "药剂师"}, {774232423, "治安官"},
            {774232436, "巡林员"}, {774232425, "香料师"}, {774232433, "密探"}, {635031500, "地下医生"},
            {635029964, "神偷"}, {774232432, "学徒"}, {635032012, "指挥家"}, {774232438, "评论家"},
            {774232437, "执灯人"}, {-650903091, "导演"}, {0, "狼"}
    };
    auto it = roleMap.find(id);
    return (it != roleMap.end()) ? it->second : "未知角色" + std::to_string(id);
}

std::vector<RoleInfo> global_validRoles;
std::mutex mimic_mutex;
std::atomic<bool> is_scanning_mimic{false};
static std::atomic<bool> g_need_reinit{false};
static std::atomic<int> g_saved_pid{-1};

namespace MjSubsystem {
    bool draw_props = true;
    bool show_distance = false;
    static float high_value_threshold = 5000.0f;
    bool show_monsters      = true;
    bool show_big_chest     = true;
    bool show_small_chest   = true;
    bool show_traps         = true;
    bool show_interactables = true;
    bool show_high_value    = true;
    bool show_low_value     = true;

    static float max_dist_monsters      = 100.0f;
    static float max_dist_big_chest     = 300.0f;
    static float max_dist_small_chest   = 200.0f;
    static float max_dist_traps         = 50.0f;
    static float max_dist_interactables = 100.0f;
    static float max_dist_high_value    = 300.0f;
    static float max_dist_low_value     = 150.0f;

    const std::vector<std::string> allowed_prefixes = {
            "prop_wz_", "rd01_", "mj_", "prop_un_"
    };

    static std::unordered_set<std::string> mj_special_classes;

    void Init() {
        mj_special_classes.insert("random01_in_piano01.gim");
        mj_special_classes.insert("trap.gim");
        mj_special_classes.insert("rd01_prop_dici02.gim");
        mj_special_classes.insert("prop_musicbox");
        mj_special_classes.insert("monster_daozei");
        mj_special_classes.insert("prop_un_zhutai");
        mj_special_classes.insert("monster_daozei");
        mj_special_classes.insert("monster_muchao");
        mj_special_classes.insert("monster_tiao");
        mj_special_classes.insert("monster_miaosha");
        mj_special_classes.insert("monster_qiqiu_blue");
        mj_special_classes.insert("monster_qiqiu_red");
        mj_special_classes.insert("monster_yuan01");
        mj_special_classes.insert("monster_yuan");
    }

    bool IsMjPropClass(std::string_view cls) {
        for (const auto& prefix : allowed_prefixes)
            if (cls.find(prefix) != std::string_view::npos) return true;
        return false;
    }

    bool IsMjSpecialClass(std::string_view cls) {
        std::string cls_str(cls);
        for (const auto& keyword : mj_special_classes)
            if (cls_str.find(keyword) != std::string::npos) return true;
        return false;
    }

    bool ShouldBypassFilter() { return draw_props; }
    bool ShouldShowGhost() { return draw_props; }
}

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
};

std::vector<std::vector<MapConfig>> g_all_maps;
int g_current_map_index = 0;
int g_current_floor_index = 0;

struct MusicboxKey {
    float x, y, z;
    int mapIndex, floorIndex;
    float tolerance = 3.0f;
    float texU, texV;
};
std::vector<MusicboxKey> g_musicbox_db;

// === 钢琴位置数据库（第二信号源）===
// 每张地图的钢琴有固定坐标，作为音乐盒之外的空间参考点
struct PianoKey {
    float x, y, z;
    int mapIndex;
    float tolerance = 5.0f;  // 钢琴比音乐盒大，容差略宽
};
std::vector<PianoKey> g_piano_db;

// === 凳子位置数据库（第三信号源，最可靠）===
struct ChairKey {
    float x, y, z;
    int mapIndex;
    float tolerance = 4.0f;
};
std::vector<ChairKey> g_chair_db;
std::vector<Vector3A> g_detected_chairs;  // 当前帧检测到的凳子位置

#define MAX_MAP_COUNT 50
#define MAX_FLOOR_COUNT 3
GLuint g_map_textures[MAX_MAP_COUNT][MAX_FLOOR_COUNT] = {{0}};

bool g_map_enabled = false;
float g_map_display_size = 800.0f;

static bool  g_use_calib = true;
static std::string g_save_notification;
static float g_notification_timer = 0.0f;

static float g_pt1_wx = 0, g_pt1_wy = 0;
static float g_pt1_tu = 0.6f, g_pt1_tv = 0.55f;
static float g_pt2_wx = 0, g_pt2_wy = 0;
static float g_pt2_tu = 0.4f, g_pt2_tv = 0.45f;
static int g_drag_point = 0;

char g_texture_status[512] = "等待加载...";
float g_map_pos_x = 700.0f;
float g_map_pos_y = 200.0f;
bool g_map_auto_detect = true;
float g_treasure_threshold = 5000.0f;

static float g_map_scale_x = 0.0002f;
static float g_map_scale_y = 0.0002f;
static float g_map_offset_u = 0.5f;
static float g_map_offset_v = 0.5f;
static bool g_map_flip_x = false;
static bool g_map_flip_y = false;
static bool g_show_nav_line = false;
static float g_map_label_scale = 0.45f;

// ========== 道具映射表 ==========
// (完整定义将在后续段中，此处省略以确保连贯，实际文件中包含全部)



// ========== 道具映射表 ==========
static const std::vector<std::pair<std::string, std::string>> g_prop_name_map = {
        {"random01_in_piano01.gim",    "[钢琴]"},
        {"rd01_prop_dici02.gim",       "[陷阱]"},
        {"rd01_prop_dici02a.gim",       "[碎石]"},
        {"rd01_prop_vase01.gim",       "[花瓶]"},
        {"prop_wz_youhua",             "[油画 15000]"},
        {"prop_un_zhutai",             "[夜莺烛台 20000]"},
        {"rd01_in_woodplane01.gim",    "[板]"},
        {"mj_wood",                    "[破裂木板 2000]"},
        {"prop_wz_xinzhidui.gim",      "[信纸堆 800]"},
        {"rd01_prop_chest01.gim",      "[紫宝箱]"},
        {"rd01_prop_box03.gim",        "[小箱子]"},
        {"trap.gim",                   "[夹子]"},
        {"prop_wz_shalou",             "[沙漏 600]"},
        {"prop_wz_menpai",             "[门牌 4200]"},
        {"prop_wz_girl",               "[鬼娃娃 12000]"},
        {"rd01_prop_door04.gim",       "[门]"},
        {"monster_daozei",             "[盗贼]"},
        {"monster_xiaobai",            "[小白]"},
        {"monster_muchao",             "[渡鸦]"},
        {"monster_tiao",               "[跳锤怪]"},
        {"monster_miaosha",            "[绅士]"},
        {"rd01_outdoor_door05.gim",       "[穿梭门]"},
        {"rd01_core_l_01_door01.gim",    "[隐藏开关门]"},
        {"mj_spear",                      "[被遗忘的信仰 20000]"},
        {"rd01_in_pianochair01.gim",     "[凳子]"},
        {"prop_wz_jinbei",              "[金杯 200000]"},
        {"prop_un_diaoxiang", "[雕像 40000]"},
        {"prop_wz_shanhu",    "[珊瑚 150000]"},
        {"monster_yuan",        "[鹿头]"},
        {"monster_yuan01",      "[鹿头 Pro Max.]"},
        {"monster_qiqiu_blue",  "[蓝球怪]"},
        {"monster_qiqiu_red",   "[红球怪]"},
        {"rd01_prop_chest02.gim",      "[金宝箱 50000]"},
        {"rd01_prop_chest02a.gim",     "[隐藏宝箱]"},
        {"prop_un_canye", "[黄金残页 100000]"},
        {"prop_wz_shuzhuang",          "[小梳妆镜 1000]"},
        {"prop_wz_ganhua",             "[干花 300]"},
        {"mj_fazhang",                 "[庇护者之战 15000]"},
        {"prop_wz_zhong",              "[钟 2400]"},
        {"prop_wz_tangshao",           "[金汤勺 8000]"},
        {"prop_wz_tk",                 "[头盔 5000]"},
        {"prop_wz_shoushi",            "[首饰盒 5000]"},
        {"prop_musicbox",              "[缪斯的秘密 10000]"},
        {"prop_wz_youlinsj",           "[幽灵水晶 12000]"},
        {"prop_wz_tangguoguan",        "[糖果罐 1600]"},
        {"prop_wz_xiangkuang",         "[相框 3000]"},
        {"prop_wz_jiezhi",             "[紫宝石戒指 8000]"},
        {"monster_daozei",             "[盗贼]"},
        {"mj_yijia",                   "[旧衣架 1200]"},
        {"prop_wz_sgpz",              "[水果盘 7000]"},
        {"prop_wz_meiyd",              "[煤油灯 1600]"},
        {"h55_pendant_inject",         "[镇静剂]"},
        {"h55_pendant_moshubang",      "[魔术棒]"},
        {"h55_pendant_flaregun",       "[信号枪]"},
        {"h55_pendant_huzhou",         "[护肘]"},
        {"h55_pendant_map",            "[地图]"},
        {"h55_pendant_book",           "[书]"},
        {"h55_pendant_gjx",            "[工具箱]"},
        {"h55_pendant_glim",           "[手电筒]"},
        {"h55_pendant_xiangshuiping",  "[忘忧之香]"},
        {"h55_pendant_controller",     "[遥控器]"},
        {"h55_pendant_football",       "[橄榄球]"},
        {"h55_pendant_huaibiao",       "[怀表]"},
        {"h55_pendant_puppet",         "[厂长傀儡]"},
        {"h55_pendant_tower",          "[窥视者]"},
        {"h55_pendant_banqiu",         "[板球]"},
        {"h55_pendant_pig",            "[野猪or虫群]"},
        {"h55_pendant_maildog",        "哈基汪"},
        {"h55_pendant_patro",          "[巡视者]"},
        {"bianzi",                     "牛鞭"},
        {"h55_pendant_owl",            "鸟"},
        {"h55_pendant_wushu_xiao",     "棍"},
        {"h55_pendant_bow",            "弓"},
        {"h55_prop_tieqiao",           "铁锹"},
        {"h55_pendant_dxzh_toukui",    "头盔"},
        {"h55_pendant_qx",             "气象瓶"},
        {"h55_pendant_gouzhua_e",      "钩爪"},
};

void scan_mimic_roles() {
    if (pid <= 0) return;

    PhysicalAddressCache local_mimic_cache;
    char buffer[4096];
    char filename[256], line[1024];
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid.load());

    FILE *fpp = fopen(filename, "r");
    if (fpp == nullptr) return;

    std::vector<RoleInfo> validRoles;
    validRoles.reserve(12);

    while (fgets(line, sizeof(line), fpp)) {
        if (strstr(line, "rw-p") != nullptr && strchr(line, '/') == nullptr) {
            if (strchr(line, '[') == nullptr || strstr(line, "[anon:") != nullptr) {
                uint64_t addrStart, addrEnd;
                if (sscanf(line, "%lx-%lx", &addrStart, &addrEnd) != 2) continue;
                int pageCount = (addrEnd - addrStart) / 4096;

                for (int j = 0; j < pageCount; j++) {
                    uint64_t currAddr = addrStart + (j * 4096);

                    if (!local_mimic_cache.is_address_valid(currAddr)) continue;
                    if (!vm_readv(currAddr, buffer, 4096)) continue;

                    for (int i = 0; i <= 4096 - 0x20; i++) {
                        if (buffer[i] == 105 && buffer[i + 1] == 100 &&
                            buffer[i + 2] == 120 && buffer[i + 5] == 99 &&
                            buffer[i + 6] == 97 && buffer[i + 14] == 105 &&
                            buffer[i + 23] == 105) {

                            int camp = buffer[i + 0xC];
                            if (camp < 1 || camp > 3) continue;

                            int memIndex = buffer[i + 3];
                            if (memIndex < 0 || memIndex > 11) continue;

                            int roleId = 0;
                            memcpy(&roleId, &buffer[i + 0x19], sizeof(int));

                            auto it = std::find_if(validRoles.begin(), validRoles.end(),
                                                   [memIndex](const RoleInfo &r) {
                                                       return r.index == memIndex;
                                                   });

                            if (it == validRoles.end()) {
                                RoleInfo info;
                                info.index = memIndex;
                                info.campId = camp;
                                info.roleId = roleId;
                                info.isRoleObtained = true;
                                validRoles.push_back(info);
                            }
                        }
                    }
                }
            }
        }
    }
    fclose(fpp);

    std::sort(validRoles.begin(), validRoles.end(),
              [](const RoleInfo &a, const RoleInfo &b) { return a.index < b.index; });

    if (validRoles.size() >= 9) {
        std::lock_guard<std::mutex> lock(mimic_mutex);
        global_validRoles = validRoles;
    } else {
        std::lock_guard<std::mutex> lock(mimic_mutex);
        global_validRoles.clear();
    }
}
float 过滤矩阵[17]{};
float matrix[16]{};
static bool show_draw_MarktheSoul = true;

void AutoWoodCheck();
static bool wood_enabled = false;
static float wood_touch_x = 2450.0f;
static float wood_touch_y = 1050.0f;
static float wood_length = 18.0f;
static float wood_width  = 12.0f;
static float wood_trigger_dist = 25.0f;
static bool show_touch_point = false;
static bool g_MimicModeEnabled = false;

static int   g_touch_max_x = 23999;
static int   g_touch_max_y = 33919;
static char  g_touch_path[128] = {0};
static bool  g_touch_ready = false;

static ImColor g_BoxColor_Survivor = ImColor(50, 255, 50, 255);
static ImColor g_BoxColor_Hunter   = ImColor(255, 50, 50, 255);
static ImColor g_BoxColor_Ghost    = ImColor(255, 255, 255, 255);
static bool show_draw_EnhancedFrame = true;
static bool show_draw_Line = false;
static bool show_draw_QY = true;
static bool show_draw_sender = true;
static bool show_draw_Animal = true;
static bool show_draw_Name = true;
static bool show_draw_Distance = true;
static bool show_draw_Cellar = true;
static bool show_draw_Chair = false;
static bool show_draw_BANZI = false;
static bool show_draw_BoxItem = false;
static bool show_draw_Prop = true;

static bool show_draw_prophet = true;
static bool show_draw_redqueen = true;
static bool Debugging = false;
static bool disable_skip_filter = false;
static bool inform_ghost = true;
static bool g_talent_view = false;
static bool g_show_detailed = false;
static std::string g_ConfigPath = "/data/local/tmp/overlay_config.txt";

static int g_chair_dist = 40;
static int g_board_dist = 40;
static int g_box_dist = 30;

static const std::vector<std::string> g_game_packages = {
        "com.netease.dwrg",
        "com.netease.idv",
        "com.netease.idv.googleplay",
        "com.netease.dwrg.mi",
        "com.netease.dwrg.oppo",
        "com.netease.dwrg.huawei",
        "com.netease.dwrg.aligames",
};

int DetectGameProcess(std::string& out_package) {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;
        char cmdline[256];
        snprintf(cmdline, sizeof(cmdline), "/proc/%d/cmdline", pid);
        FILE* fp = fopen(cmdline, "r");
        if (!fp) continue;
        char buf[256];
        if (fgets(buf, sizeof(buf), fp)) {
            for (const auto& pkg : g_game_packages) {
                if (strstr(buf, pkg.c_str()) && strstr(buf, "com") &&
                    !strstr(buf, "PushService") && !strstr(buf, "gcsdk")) {
                    fclose(fp);
                    closedir(dir);
                    out_package = pkg;
                    return pid;
                }
            }
        }
        fclose(fp);
    }
    closedir(dir);
    return -1;
}

static std::string g_current_game_package;
static int g_current_game_pid = -1;

static void LoadConfig() {
    std::ifstream file(g_ConfigPath);
    if (!file) return;
    std::unordered_map<std::string, std::string> map;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            map[key] = value;
        }
    }
    for (int i = 0; i < (int)g_all_maps.size(); i++) {
        for (int j = 0; j < (int)g_all_maps[i].size(); j++) {
            char key[64];
            snprintf(key, sizeof(key), "map_calib_%d_%d", i, j);
            if (map.count(key)) {
                if (i >= (int)g_all_maps.size() || j >= (int)g_all_maps[i].size())
                    continue;
                std::istringstream iss(map[key]);
                std::string token;
                float vals[10];
                int idx = 0;
                while (std::getline(iss, token, ',') && idx < 10) {
                    vals[idx++] = std::stof(token);
                }
                if (idx == 10) {
                    auto& cfg = g_all_maps[i][j];
                    cfg.minX    = vals[0];
                    cfg.maxX    = vals[1];
                    cfg.minY    = vals[2];
                    cfg.maxY    = vals[3];
                    cfg.scaleX  = vals[4];
                    cfg.scaleY  = vals[5];
                    cfg.offsetU = vals[6];
                    cfg.offsetV = vals[7];
                    cfg.flipX   = (bool)vals[8];
                    cfg.flipY   = (bool)vals[9];
                    cfg.calibrated = true;
                }
            }
        }
    }
    file.close();

    auto getBool = [&](const std::string& key, bool& var) {
        if (map.count(key)) var = (map[key] == "1");
    };
    auto getFloat = [&](const std::string& key, float& var) {
        if (map.count(key)) {
            try { var = std::stof(map[key]); }
            catch (...) { /* 格式错误，保持默认值 */ }
        }
    };
    auto getInt = [&](const std::string& key, int& var) {
        if (map.count(key)) {
            try { var = std::stoi(map[key]); }
            catch (...) { /* 格式错误，保持默认值 */ }
        }
    };

    getBool("show_draw_MarktheSoul", show_draw_MarktheSoul);
    getBool("show_draw_EnhancedFrame", show_draw_EnhancedFrame);
    getBool("show_draw_Line", show_draw_Line);
    getBool("show_draw_QY", show_draw_QY);
    getBool("show_draw_sender", show_draw_sender);
    getBool("show_draw_Animal", show_draw_Animal);
    getBool("show_draw_Name", show_draw_Name);
    getBool("show_draw_Distance", show_draw_Distance);
    getBool("show_draw_Cellar", show_draw_Cellar);
    getBool("show_draw_Chair", show_draw_Chair);
    getBool("show_draw_BANZI", show_draw_BANZI);
    getBool("show_draw_BoxItem", show_draw_BoxItem);
    getBool("show_draw_Prop", show_draw_Prop);
    getBool("show_draw_prophet", show_draw_prophet);
    getBool("show_draw_redqueen", show_draw_redqueen);
    getBool("Debugging", Debugging);
    getBool("disable_skip_filter", disable_skip_filter);
    getBool("inform_ghost", inform_ghost);
    getBool("g_MimicModeEnabled", g_MimicModeEnabled);
    getBool("draw_props", MjSubsystem::draw_props);
    getBool("show_distance", MjSubsystem::show_distance);
    getBool("mj_show_monsters",      MjSubsystem::show_monsters);
    getBool("mj_show_big_chest",     MjSubsystem::show_big_chest);
    getBool("mj_show_small_chest",   MjSubsystem::show_small_chest);
    getBool("mj_show_traps",         MjSubsystem::show_traps);
    getBool("mj_show_interactables", MjSubsystem::show_interactables);
    getBool("mj_show_high_value",    MjSubsystem::show_high_value);
    getBool("mj_show_low_value",     MjSubsystem::show_low_value);
    getFloat("mj_max_dist_monsters",      MjSubsystem::max_dist_monsters);
    getFloat("mj_max_dist_big_chest",     MjSubsystem::max_dist_big_chest);
    getFloat("mj_max_dist_small_chest",   MjSubsystem::max_dist_small_chest);
    getFloat("mj_max_dist_traps",         MjSubsystem::max_dist_traps);
    getFloat("mj_max_dist_interactables", MjSubsystem::max_dist_interactables);
    getFloat("mj_max_dist_high_value",    MjSubsystem::max_dist_high_value);
    getFloat("mj_max_dist_low_value",     MjSubsystem::max_dist_low_value);
    getFloat("mj_high_value_threshold",   MjSubsystem::high_value_threshold);

    getBool("g_map_enabled", g_map_enabled);
    getBool("g_map_auto_detect", g_map_auto_detect);
    if (map.count("g_current_map_index")) g_current_map_index = std::stoi(map["g_current_map_index"]);
    getFloat("g_map_display_size", g_map_display_size);
    getFloat("g_map_pos_x", g_map_pos_x);
    getFloat("g_map_pos_y", g_map_pos_y);
    getFloat("g_treasure_threshold", g_treasure_threshold);

    if (map.count("g_BoxColor_Survivor")) {
        std::istringstream iss(map["g_BoxColor_Survivor"]);
        int r, g, b, a;
        if (iss >> r >> g >> b >> a) {
            g_BoxColor_Survivor = ImColor(r, g, b, a);
        }
    }
    if (map.count("g_BoxColor_Hunter")) {
        std::istringstream iss(map["g_BoxColor_Hunter"]);
        int r, g, b, a;
        if (iss >> r >> g >> b >> a) {
            g_BoxColor_Hunter = ImColor(r, g, b, a);
        }
    }
    if (map.count("g_BoxColor_Ghost")) {
        std::istringstream iss(map["g_BoxColor_Ghost"]);
        int r, g, b, a;
        if (iss >> r >> g >> b >> a) {
            g_BoxColor_Ghost = ImColor(r, g, b, a);
        }
    }

    // 新增配置项读取
    getBool("g_show_saved_paths", g_show_saved_paths);
    getFloat("g_smooth_strength", g_smooth_strength);
    getBool("g_enable_snap", g_enable_snap);
    getBool("g_ortho_draw", g_ortho_draw);
    getFloat("g_path_draw_threshold", g_path_draw_threshold);
    getFloat("g_grid_spacing", g_grid_spacing);
    getFloat("g_snap_distance", g_snap_distance);
    getBool("g_show_grid", g_show_grid);
    getFloat("g_grid_alpha", g_grid_alpha);

    // === 全量持久化补全 ===
    // Tab 1: 功能设置
    getBool("talent_view", g_talent_view);
    getInt("chair_distance", g_chair_dist);
    getInt("board_distance", g_board_dist);
    getInt("box_distance", g_box_dist);

    // Tab 2: 自动盖板
    getBool("wood_enabled", wood_enabled);
    getBool("show_touch_point", show_touch_point);
    getFloat("wood_touch_x", wood_touch_x);
    getFloat("wood_touch_y", wood_touch_y);
    getFloat("wood_trigger_dist", wood_trigger_dist);
    getFloat("wood_length", wood_length);
    getFloat("wood_width", wood_width);

    // Tab 3: 模仿者
    getBool("show_mimic_overlay", show_mimic_overlay);

    // Tab 5: 地图管理
    getBool("g_show_nav_line", g_show_nav_line);
    getFloat("g_map_label_scale", g_map_label_scale);
    getInt("g_route_min_value", g_route_min_value);
    getFloat("g_map_opacity", g_map_opacity);
    getFloat("g_label_opacity", g_label_opacity);
    getFloat("g_self_opacity", g_self_opacity);
    getFloat("g_route_opacity", g_route_opacity);
    getFloat("g_saved_path_opacity", g_saved_path_opacity);
    getBool("g_show_graph_debug", g_show_graph_debug);
    getBool("g_use_calib", g_use_calib);
    getBool("g_map_flip_x", g_map_flip_x);
    getBool("g_map_flip_y", g_map_flip_y);

    // 大图预览
    getFloat("g_big_map_zoom", g_big_map_zoom);

    // g_talent_view独立窗口
    getBool("show_detailed", g_show_detailed);
}



static void SaveConfig() {
    // 保护：如果 g_all_maps 为空且配置文件已存在，跳过保存
    // 防止启动初期未加载地图数据时，std::ofstream 截断文件导致校准数据丢失
    if (g_all_maps.empty()) {
        std::ifstream check(g_ConfigPath);
        if (check) {
            check.close();
            return; // 已有配置文件但地图数据为空 → 不覆盖，保护校准数据
        }
    }

    std::ofstream file(g_ConfigPath);
    if (!file) return;

    file << "# Android Runtime Cache\n";
    file << "# Do not modify\n\n";

    file << "show_draw_MarktheSoul=" << show_draw_MarktheSoul << "\n";
    file << "show_draw_EnhancedFrame=" << show_draw_EnhancedFrame << "\n";
    file << "show_draw_Line=" << show_draw_Line << "\n";
    file << "show_draw_QY=" << show_draw_QY << "\n";
    file << "show_draw_sender=" << show_draw_sender << "\n";
    file << "show_draw_Animal=" << show_draw_Animal << "\n";
    file << "show_draw_Name=" << show_draw_Name << "\n";
    file << "show_draw_Distance=" << show_draw_Distance << "\n";
    file << "show_draw_Cellar=" << show_draw_Cellar << "\n";
    file << "show_draw_Chair=" << show_draw_Chair << "\n";
    file << "show_draw_BANZI=" << show_draw_BANZI << "\n";
    file << "show_draw_BoxItem=" << show_draw_BoxItem << "\n";
    file << "show_draw_Prop=" << show_draw_Prop << "\n";
    file << "show_draw_prophet=" << show_draw_prophet << "\n";
    file << "show_draw_redqueen=" << show_draw_redqueen << "\n";
    file << "Debugging=" << Debugging << "\n";
    file << "disable_skip_filter=" << disable_skip_filter << "\n";
    file << "inform_ghost=" << inform_ghost << "\n";
    file << "g_MimicModeEnabled=" << g_MimicModeEnabled << "\n";
    file << "draw_props=" << MjSubsystem::draw_props << "\n";
    file << "show_distance=" << MjSubsystem::show_distance << "\n";
    file << "g_BoxColor_Survivor=" << (int)(g_BoxColor_Survivor.Value.x * 255.0f) << " "
         << (int)(g_BoxColor_Survivor.Value.y * 255.0f) << " "
         << (int)(g_BoxColor_Survivor.Value.z * 255.0f) << " "
         << (int)(g_BoxColor_Survivor.Value.w * 255.0f) << "\n";
    file << "g_BoxColor_Hunter=" << (int)(g_BoxColor_Hunter.Value.x * 255.0f) << " "
         << (int)(g_BoxColor_Hunter.Value.y * 255.0f) << " "
         << (int)(g_BoxColor_Hunter.Value.z * 255.0f) << " "
         << (int)(g_BoxColor_Hunter.Value.w * 255.0f) << "\n";
    file << "g_BoxColor_Ghost=" << (int)(g_BoxColor_Ghost.Value.x * 255.0f) << " "
         << (int)(g_BoxColor_Ghost.Value.y * 255.0f) << " "
         << (int)(g_BoxColor_Ghost.Value.z * 255.0f) << " "
         << (int)(g_BoxColor_Ghost.Value.w * 255.0f) << "\n";

    file << "mj_show_monsters="      << MjSubsystem::show_monsters      << "\n";
    file << "mj_show_big_chest="     << MjSubsystem::show_big_chest     << "\n";
    file << "mj_show_small_chest="   << MjSubsystem::show_small_chest   << "\n";
    file << "mj_show_traps="         << MjSubsystem::show_traps         << "\n";
    file << "mj_show_interactables=" << MjSubsystem::show_interactables << "\n";
    file << "mj_show_high_value="    << MjSubsystem::show_high_value    << "\n";
    file << "mj_show_low_value="     << MjSubsystem::show_low_value     << "\n";
    file << "mj_max_dist_monsters="      << MjSubsystem::max_dist_monsters      << "\n";
    file << "mj_max_dist_big_chest="     << MjSubsystem::max_dist_big_chest     << "\n";
    file << "mj_max_dist_small_chest="   << MjSubsystem::max_dist_small_chest   << "\n";
    file << "mj_max_dist_traps="         << MjSubsystem::max_dist_traps         << "\n";
    file << "mj_max_dist_interactables=" << MjSubsystem::max_dist_interactables << "\n";
    file << "mj_max_dist_high_value="    << MjSubsystem::max_dist_high_value    << "\n";
    file << "mj_max_dist_low_value="     << MjSubsystem::max_dist_low_value     << "\n";
    file << "mj_high_value_threshold="   << MjSubsystem::high_value_threshold   << "\n";

    file << "g_map_enabled=" << g_map_enabled << "\n";
    file << "g_map_auto_detect=" << g_map_auto_detect << "\n";
    file << "g_current_map_index=" << g_current_map_index << "\n";
    file << "g_map_display_size=" << g_map_display_size << "\n";
    file << "g_map_pos_x=" << g_map_pos_x << "\n";
    file << "g_map_pos_y=" << g_map_pos_y << "\n";
    file << "g_treasure_threshold=" << g_treasure_threshold << "\n";

    // 新增配置项保存
    file << "g_show_saved_paths=" << g_show_saved_paths << "\n";
    file << "g_smooth_strength=" << g_smooth_strength << "\n";
    file << "g_enable_snap=" << g_enable_snap << "\n";
    file << "g_ortho_draw=" << g_ortho_draw << "\n";
    file << "g_path_draw_threshold=" << g_path_draw_threshold << "\n";
    file << "g_grid_spacing=" << g_grid_spacing << "\n";
    file << "g_snap_distance=" << g_snap_distance << "\n";
    file << "g_show_grid=" << g_show_grid << "\n";
    file << "g_grid_alpha=" << g_grid_alpha << "\n";

    // === 全量持久化补全 ===
    // Tab 1: 功能设置
    file << "talent_view=" << g_talent_view << "\n";
    file << "chair_distance=" << g_chair_dist << "\n";
    file << "board_distance=" << g_board_dist << "\n";
    file << "box_distance=" << g_box_dist << "\n";

    // Tab 2: 自动盖板
    file << "wood_enabled=" << wood_enabled << "\n";
    file << "show_touch_point=" << show_touch_point << "\n";
    file << "wood_touch_x=" << wood_touch_x << "\n";
    file << "wood_touch_y=" << wood_touch_y << "\n";
    file << "wood_trigger_dist=" << wood_trigger_dist << "\n";
    file << "wood_length=" << wood_length << "\n";
    file << "wood_width=" << wood_width << "\n";

    // Tab 3: 模仿者
    file << "show_mimic_overlay=" << show_mimic_overlay << "\n";

    // Tab 5: 地图管理
    file << "g_show_nav_line=" << g_show_nav_line << "\n";
    file << "g_map_label_scale=" << g_map_label_scale << "\n";
    file << "g_route_min_value=" << g_route_min_value << "\n";
    file << "g_map_opacity=" << g_map_opacity << "\n";
    file << "g_label_opacity=" << g_label_opacity << "\n";
    file << "g_self_opacity=" << g_self_opacity << "\n";
    file << "g_route_opacity=" << g_route_opacity << "\n";
    file << "g_saved_path_opacity=" << g_saved_path_opacity << "\n";
    file << "g_show_graph_debug=" << g_show_graph_debug << "\n";
    file << "g_use_calib=" << g_use_calib << "\n";
    file << "g_map_flip_x=" << g_map_flip_x << "\n";
    file << "g_map_flip_y=" << g_map_flip_y << "\n";

    // 大图预览
    file << "g_big_map_zoom=" << g_big_map_zoom << "\n";

    // g_talent_view独立窗口
    file << "show_detailed=" << g_show_detailed << "\n";

    for (int i = 0; i < (int)g_all_maps.size(); i++) {
        for (int j = 0; j < (int)g_all_maps[i].size(); j++) {
            auto& cfg = g_all_maps[i][j];
            if (cfg.calibrated) {
                file << "map_calib_" << i << "_" << j << "="
                     << cfg.minX << "," << cfg.maxX << ","
                     << cfg.minY << "," << cfg.maxY << ","
                     << cfg.scaleX << "," << cfg.scaleY << ","
                     << cfg.offsetU << "," << cfg.offsetV << ","
                     << (int)cfg.flipX << "," << (int)cfg.flipY << "\n";
            }
        }
    }

    file.close();

    // === 滚动备份：保留最近 3 个版本 ===
    // 每次保存前，将旧版本往后推：.bak2 → 删除, .bak1 → .bak2, .bak → .bak1, 当前 → .bak
    {
        const char* backup_dir = "/sdcard/maps/calib_backup";
        // 确保目录存在（mkdir -p 等效）
        mkdir("/sdcard/maps", 0755);
        mkdir(backup_dir, 0755);

        std::string bak2 = std::string(backup_dir) + "/overlay_config.bak2";
        std::string bak1 = std::string(backup_dir) + "/overlay_config.bak1";
        std::string bak0 = std::string(backup_dir) + "/overlay_config.bak";

        // 删除最老的 .bak2
        remove(bak2.c_str());
        // .bak1 → .bak2
        rename(bak1.c_str(), bak2.c_str());
        // .bak → .bak1
        rename(bak0.c_str(), bak1.c_str());
        // 当前文件 → .bak
        std::ifstream src(g_ConfigPath, std::ios::binary);
        if (src) {
            std::ofstream dst(bak0, std::ios::binary | std::ios::trunc);
            if (dst) {
                dst << src.rdbuf();
                dst.close();
            }
            src.close();
        }
    }

    // 兼容旧备份路径
    std::ifstream src(g_ConfigPath, std::ios::binary);
    if (src) {
        std::ofstream dst("/sdcard/overlay_config_backup.txt", std::ios::binary | std::ios::trunc);
        if (dst) {
            dst << src.rdbuf();
            dst.close();
        }
        src.close();
    }
}

void SaveExitsToJSON(int mapIdx, int floorIdx) {
    std::string json_path = "/sdcard/maps/map_config.json";
    std::ifstream ifs(json_path);
    json j;
    if (ifs) ifs >> j; else return;

    if (!j.contains("maps")) return;
    std::string expected_name = g_all_maps[mapIdx][floorIdx].name;

    for (auto& m : j["maps"]) {
        if (m.value("floor", 0) == floorIdx && m.value("name", "") == expected_name) {
            m["exits"] = json::array();
            for (auto& e : g_exits[mapIdx][floorIdx]) {
                json exit_json;
                exit_json["x"] = e.X;
                exit_json["y"] = e.Y;
                exit_json["z"] = e.Z;
                m["exits"].push_back(exit_json);
            }
            break;
        }
    }
    std::ofstream ofs(json_path);
    ofs << j.dump(4);
}

// ========== 保存玩家路径到 JSON ==========
void SavePlayerPathsToJSON(int mapIdx, int floorIdx) {
    std::string json_path = "/sdcard/maps/map_config.json";
    std::ifstream ifs(json_path);
    json j;
    if (ifs) ifs >> j; else return;

    if (!j.contains("maps")) return;
    std::string expected_name = g_all_maps[mapIdx][floorIdx].name;

    for (auto& m : j["maps"]) {
        if (m.value("floor", 0) == floorIdx && m.value("name", "") == expected_name) {
            m["player_paths"] = json::array();
            for (auto& path : g_saved_paths) {
                json path_json;
                path_json["points"] = json::array();
                for (auto& pt : path) {
                    json pt_json;
                    pt_json["x"] = pt.X;
                    pt_json["y"] = pt.Y;
                    pt_json["z"] = pt.Z;
                    path_json["points"].push_back(pt_json);
                }
                m["player_paths"].push_back(path_json);
            }
            break;
        }
    }
    std::ofstream ofs(json_path);
    ofs << j.dump(4);
}


// ========== 保存场景物体（钢琴+凳子）到 JSON ==========
void SaveSceneObjectsToJSON(int mapIdx, int floorIdx) {
    std::string json_path = "/sdcard/maps/map_config.json";
    std::ifstream ifs(json_path);
    json j;
    if (ifs) ifs >> j; else return;

    if (!j.contains("maps")) return;

    for (auto& m : j["maps"]) {
        int mf = m.value("floor", 0);
        int mi = -1;
        for (int i = 0; i < (int)g_all_maps.size(); i++) {
            if (!g_all_maps[i].empty() && g_all_maps[i][0].name == m.value("name", "")) {
                mi = i; break;
            }
        }
        if (mi == mapIdx && mf == floorIdx) {
            if (g_detected_piano_pos.X != 0.0f || g_detected_piano_pos.Y != 0.0f) {
                m["piano_x"] = g_detected_piano_pos.X;
                m["piano_y"] = g_detected_piano_pos.Y;
                m["piano_z"] = g_detected_piano_pos.Z;
            }
            m["chairs"] = json::array();
            for (auto& chair : g_detected_chairs) {
                json c;
                c["x"] = chair.X; c["y"] = chair.Y; c["z"] = chair.Z;
                m["chairs"].push_back(c);
            }
            break;
        }
    }
    std::ofstream ofs(json_path);
    ofs << j.dump(4);

    // 重新加载数据库
    g_piano_db.clear();
    g_chair_db.clear();
    for (auto& m : j["maps"]) {
        int mi = -1;
        for (int i = 0; i < (int)g_all_maps.size(); i++) {
            if (!g_all_maps[i].empty() && g_all_maps[i][0].name == m.value("name", "")) {
                mi = i; break;
            }
        }
        if (mi < 0) continue;
        if (m.contains("piano_x") && m.contains("piano_y")) {
            PianoKey pk;
            pk.x = m["piano_x"]; pk.y = m["piano_y"];
            pk.z = m.value("piano_z", 0.0f); pk.mapIndex = mi;
            g_piano_db.push_back(pk);
        }
        if (m.contains("chairs")) {
            for (auto& c : m["chairs"]) {
                ChairKey ck;
                ck.x = c["x"]; ck.y = c["y"];
                ck.z = c.value("z", 0.0f); ck.mapIndex = mi;
                g_chair_db.push_back(ck);
            }
        }
    }
}

static std::unordered_map<std::string, bool> skipClassCache;

inline bool should_filter_cached(std::string_view cn) noexcept {
    if (Debugging) return false;
    if (MjSubsystem::ShouldBypassFilter()) return false;
    if (cn.find("random01_in_piano01.gim") != std::string_view::npos) return false;
    if (cn.find("trap.gim") != std::string_view::npos) return false;
    if (MjSubsystem::IsMjSpecialClass(cn)) return false;
    if (disable_skip_filter || show_draw_redqueen) return false;
    if (cn.find("prop_wz_") != std::string_view::npos ||
        cn.find("rd01_") != std::string_view::npos ||
        cn.find("mj_") != std::string_view::npos) {
        return false;
    }

    std::string cn_str(cn);
    auto it = skipClassCache.find(cn_str);
    if (it != skipClassCache.end()) return it->second;

    static const std::vector<std::string_view> exact_filters = {
            "camera", "shangren_tiaoban", "buzz", "creature", "umbrella", "parasol", "bird",
            "girl_page", "skill_hudie", "burke_console", "heijin_yizi", "qiutu_box", "weapon",
            "detective", "dress_ghost", "part", "effect", "sound", "_lod", "_shadow", "_ui_",
            "_indicator", "collision", "mesh", "_ttds_", "_em_", "phantom", "summon", "decoy",
            "trap", "crow", "butterflyfx.gim", "shuimu", "_box", "haitun", "feie", "huohuli",
            "_ghost.gim", "sender01a.gim", "balloon01", "joan_cat", "nvyyao", "girl_e_sj",
            "zu4oyi", "toufa", "hair", "_page.gim", "_pingzi.gim", "shayu", "hx_bashou.gim",
            "erhuan.gim", "piaodai", "qingren_dress", "_pd.gim", "earring.gim", "_huahuan.gim",
            "_xianglian.gim", "jingti_1.gim", "spirit.gim", "_lace.gim", "_dress.gim", "_pf.gim",
            "_qunbai.gim", "_pifeng.gim", "_elfx.gim", "_maozi.gim", "_wei.gim", "_handpd.gim",
            "_hdj.gim", "_yaogua.gim", "_haima.gim", "_qun.gim", "_qunzi.gim", "changqun.gim",
            "_mawei.gim", "_head.gim", "_weiba.gim", "_foot_l.gim", "_foot_r.gim", "dj.gim",
            "_face.gim", "_erduo.gim", "_hat.gim", "_wing.gim", "_tou.gim", "_xiaomao.gim",
            "_eye.gim", "_mianju.gim", "_yezi.gim", "_qpd.gim", "_gouwei.gim", "_tail.gim",
            "_wb.gim", "_toujin.gim", "_left.gim", "_right.gim", "_tianhe_niao.gim", "_dayi.gim",
            "_belt_l.gim", "_belt_r.gim", "pendant_huojian"};

    for (const auto &keyword : exact_filters) {
        if (cn.find(keyword) != std::string_view::npos) {
            skipClassCache[cn_str] = true;
            return true;
        }
    }
    skipClassCache[cn_str] = false;
    return false;
}

static std::unordered_map<std::string, bool> fakeHunterCache;

inline bool IsFakeHunter_cached(std::string_view name) noexcept {
    std::string name_str(name);
    auto it = fakeHunterCache.find(name_str);
    if (it != fakeHunterCache.end()) return it->second;

    static const std::vector<std::string_view> fakes = {
            "mirror", "crow", "patroller", "peeper", "tentacle", "note", "robot", "phantom",
            "clone", "decoy", "trap", "pet", "em_crow", "em65", "butcher_em",
            "umbrella", "parasol", "buzz", "bird"};

    for (const auto &f : fakes) {
        if (name.find(f) != std::string_view::npos) {
            fakeHunterCache[name_str] = true;
            return true;
        }
    }
    fakeHunterCache[name_str] = false;
    return false;
}

enum class ObjSubClass {
    Unknown = 0, Trap, CipherMachine, Clip, Cat, Lion, Cellar, Box, Chair, Pallet, Prop, Player, Boss, Ghost
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

static ImVec2 circle_pos{-1.0f, -1.0f};
static ImFont *g_font_ui{};
static float circle_radius = 60.0f;
static float margin = 10.0f;
static std::string 过滤类名, 类名;

static const float 距离比例 = 11.886f;

template <typename T> inline T SnapToPixel(T v) noexcept {
    return static_cast<T>(std::lrint(v));
}

static uint32_t orientation = static_cast<uint32_t>(-1);
ANativeWindow *window{};
android::ANativeWindowCreator::DisplayInfo displayInfo{};
ImGuiWindow *g_window{};
int abs_ScreenX{}, abs_ScreenY{};
int native_window_screen_x{}, native_window_screen_y{};
std::unique_ptr<AndroidImgui> graphics{};

static ImFont *g_main_font{};
static ImFont *g_ui_font{};
static Vector3A D, Z, M;
static float z_x{}, z_y{}, z_z{}, d_x{}, d_y{}, d_z{}, camera, r_x{}, r_y{}, r_w{};
static float X1{}, Y1{}, X2{}, Y2{}, W{}, H{};
static char objtext[256]{};
static char 监管者预知[1024]{};
static float px{}, py{};

std::vector<DataStruct> data_buffers[2];
std::atomic<int> front_buffer_idx = 0;
std::mutex data_mutex;

static std::unordered_map<uintptr_t, int> g_meeting_seat_map;

struct MirrorInfo {
    Vector3A survivorPos;
    Vector3A mirrorPos;
    char name[256];
};
static std::vector<MirrorInfo> g_mirrorList;
static bool g_holdMirror = false;
static Vector3A g_mirrorCenter;
static Vector3A g_mirrorNormal;
static std::atomic<int> g_selfAction{0};

inline Vector3A getObjectCoordinates(uintptr_t coorBase, bool isProp = false) noexcept {
    Vector3A pos{};
    if (coorBase) {
        pos.X = getFloat(coorBase + 0xA0);
        pos.Y = getFloat(coorBase + 0xA8);
        pos.Z = getFloat(coorBase + 0xA4);
        if (isProp) pos.Z -= 8.5f;
    }
    return pos;
}

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

inline bool optimizedWorldToScreen(const Vector3A &worldPos,
                                   const float *matrix, float px, float py,
                                   float &screenX, float &screenY,
                                   float &screenW) noexcept {
    const float w = matrix[3] * worldPos.X + matrix[7] * worldPos.Z +
                    matrix[11] * worldPos.Y + matrix[15];
    if (w <= 0.1f) return false;
    const float invW = 1.0f / w;
    screenX = px + (matrix[0] * worldPos.X + matrix[4] * worldPos.Z +
                    matrix[8] * worldPos.Y + matrix[12]) * invW * px;
    screenY = py - (matrix[1] * worldPos.X + matrix[5] * (worldPos.Z + 8.5f) +
                    matrix[9] * worldPos.Y + matrix[13]) * invW * py;
    screenW = py - (matrix[1] * worldPos.X + matrix[5] * (worldPos.Z + 28.5f) +
                    matrix[9] * worldPos.Y + matrix[13]) * invW * py;
    return true;
}

static bool fonts_initialized = false;

void init_My_drawdata() {
    LoadMapConfigFromJSON();
    LoadConfig();
    MjSubsystem::Init();
    if (wood_touch_x <= 0.0f || wood_touch_x > displayInfo.width) {
        wood_touch_x = displayInfo.width * 0.85f;
    }
    if (wood_touch_y <= 0.0f || wood_touch_y > displayInfo.height) {
        wood_touch_y = displayInfo.height * 0.55f;
    }
    if (fonts_initialized) return;
    ImGuiIO &io = ImGui::GetIO();
    const float base = std::min(abs_ScreenX, abs_ScreenY);
    const float fontSize = std::sqrt(base) * 0.91f;
    const float uiFontSize = fontSize * 1.32f;
    if (!g_main_font) {
        ImGui::My_Android_LoadSystemFont(fontSize);
        g_main_font = io.Fonts->Fonts.back();
    }
    if (!g_ui_font) {
        ImGui::My_Android_LoadSystemFont(uiFontSize);
        g_ui_font = io.Fonts->Fonts.back();
    }
    g_font_ui = g_ui_font;
    fonts_initialized = true;
}

void screen_config() {
    static android::ANativeWindowCreator::DisplayInfo lastDisplayInfo{};
    displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
    if (lastDisplayInfo.width != displayInfo.width ||
        lastDisplayInfo.height != displayInfo.height ||
        lastDisplayInfo.orientation != displayInfo.orientation) {
        lastDisplayInfo = displayInfo;
        fonts_initialized = false;
    }
}

// ========== 地图状态管理 ==========
// ResetMapState: 重置全部地图状态变量，在游戏重连/切场景时调用
// InvalidateMapTextures: 使所有纹理缓存失效，在 EGL 上下文丢失时调用
void ResetMapState() {
    memset(g_map_textures, 0, sizeof(g_map_textures));
    g_current_map_index = -1;
    g_current_floor_index = 0;
    g_last_valid_map_index = -1;
    g_last_valid_floor_index = 0;
    g_switch_candidate_index = -1;
    g_switch_confirm_frames = 0;
    g_musicbox_moved_on_same_map = false;
    g_new_map_detected = false;
    g_new_map_prompted = false;
    g_new_map_frame_counter = 0;
    g_detected_musicbox_pos = Vector3A{};
    g_detected_piano_pos = Vector3A{};
    g_frames_since_musicbox_lost = 0;
    g_last_rendered_map_index = -1;
    g_last_rendered_floor_index = -1;
    g_selected_path_index = -1;
}

void InvalidateMapTextures() {
    memset(g_map_textures, 0, sizeof(g_map_textures));
    g_last_rendered_map_index = -1;
    g_last_rendered_floor_index = -1;
}

// ========== GL 上下文丢失检测 ==========
// Android 切后台→前台时 EGL 上下文可能被回收，所有 GL 纹理 ID 失效
// 每帧检测：尝试用 glIsTexture 验证当前地图纹理是否仍然有效
static bool g_gl_context_lost = false;
static int  g_gl_check_counter = 0;

static void CheckGLContextLoss() {
    // 每 60 帧（~1秒）检测一次，避免性能开销
    if (++g_gl_check_counter < 60) return;
    g_gl_check_counter = 0;

    if (g_current_map_index < 0 || g_current_map_index >= MAX_MAP_COUNT) return;
    if (g_current_floor_index < 0 || g_current_floor_index >= MAX_FLOOR_COUNT) return;

    GLuint tex = g_map_textures[g_current_map_index][g_current_floor_index];
    if (tex != 0) {
        // 验证纹理 ID 是否仍然有效
        if (!glIsTexture(tex)) {
            // 纹理 ID 失效 → GL 上下文已丢失
            printf("\033[31m[GL] 上下文丢失检测：纹理 %u 失效，清空缓存\033[0m\n", tex);
            InvalidateMapTextures();
            g_gl_context_lost = true;
        }
    }
}

// 从 GL 上下文丢失中恢复（在主渲染线程调用）
static void RecoverFromGLContextLoss() {
    if (!g_gl_context_lost) return;
    g_gl_context_lost = false;
    printf("\033[32m[GL] 上下文恢复：重新加载地图纹理\033[0m\n");
    // 纹理缓存已清空，下一帧 Draw_MapOverlay 会自动重新加载
}

void drawBegin() {
    if (orientation != displayInfo.orientation) {
        orientation = displayInfo.orientation;
        Touch::setOrientation(displayInfo.orientation);
        if (g_window) {
            const ImVec2 halfSize(g_window->Size.x * 0.5f, g_window->Size.y * 0.5f);
            g_window->Pos.x = displayInfo.width * 0.5f - halfSize.x;
            g_window->Pos.y = displayInfo.height * 0.5f - halfSize.y;
            circle_pos.x = displayInfo.width * 0.5f;
            circle_pos.y = displayInfo.height * 0.5f;
        }
    }
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

inline constexpr ImColor 红色(255, 50, 50, 255), 绿色(50, 255, 50, 255),
        蓝色(50, 150, 255, 255), 黄色(255, 255, 50, 255), 紫色(200, 100, 255, 255),
        黑色(0, 0, 0, 255), 亮红色(255, 50, 50, 255), 白色(255, 255, 255, 255),
        密码机色(255, 255, 100, 255), 板子色(255, 255, 255, 255),
        箱子色(255, 255, 255, 255), 椅子色(255, 100, 100, 255),
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

struct ModuleBssInfo {
    unsigned long addr{};
    unsigned long taddr{};
};

ModuleBssInfo get_module_bss(int pid, const char *module_name) {
    ModuleBssInfo info{};
    char filename[64];
    std::snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(filename, "r"), &fclose);
    if (!fp) return info;
    char line[1024]{};
    bool found_module = false;
    while (std::fgets(line, sizeof(line), fp.get())) {
        if (std::strstr(line, module_name)) found_module = true;
        if (found_module) {
            long addr{}, taddr{};
            if (std::sscanf(line, "%lx-%lx", &addr, &taddr) != 2) continue;
            if (std::strstr(line, "rw") && std::strlen(line) < 86 && (taddr - addr) / 4096 >= 2800) {
                char *words[10]{};
                int numWords = 0;
                char *tok = std::strtok(line, " ");
                while (tok && numWords < 10) {
                    words[numWords++] = tok;
                    tok = std::strtok(nullptr, " ");
                }
                for (int i = 0; i < numWords; ++i) {
                    if (std::sscanf(words[i], "%lx-%lx", &info.addr, &info.taddr) == 2) {
                        return info;
                    }
                }
                return {};
            }
        }
    }
    return {};
}

ModuleBssInfo get_module_bssgjf(int pid, const char *module_name) {
    ModuleBssInfo info{};
    char filename[64];
    std::snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(filename, "r"), &fclose);
    if (!fp) return info;
    char line[1024]{};
    bool is = false;
    while (std::fgets(line, sizeof(line), fp.get())) {
        long addr{}, taddr{};
        if (std::sscanf(line, "%lx-%lx", &addr, &taddr) != 2) continue;
        if (std::strstr(line, module_name) && std::strstr(line, "r-xp") && (taddr - addr) == 114982912) {
            is = true;
        }
        if (is && std::strstr(line, "rw") && !std::feof(fp.get()) && std::strlen(line) < 86) {
            if ((taddr - addr) / 4096 <= 3000) continue;
            if (std::sscanf(line, "%lx-%lx", &info.addr, &info.taddr) == 2) {
                break;
            }
            return {};
        }
    }
    return info;
}

int get_name_pid1(const char *packageName) {
    int id = -1;
    std::unique_ptr<DIR, decltype(&closedir)> dir(::opendir("/proc"), &closedir);
    if (!dir) return -1;
    struct dirent *entry;
    char filename[64], cmdline[64];
    while ((entry = ::readdir(dir.get()))) {
        id = std::atoi(entry->d_name);
        if (id <= 0) continue;
        std::snprintf(filename, sizeof(filename), "/proc/%d/cmdline", id);
        std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(filename, "r"), &fclose);
        if (!fp) continue;
        if (std::fgets(cmdline, sizeof(cmdline), fp.get())) {
            if ((std::strstr(cmdline, packageName) || std::strstr(cmdline, "com.netease.idv")) &&
                std::strstr(cmdline, "com") && !std::strstr(cmdline, "PushService") &&
                !std::strstr(cmdline, "gcsdk")) {
                std::snprintf(extractedString, sizeof(extractedString), "%s", cmdline);
                return id;
            }
        }
    }
    return -1;
}

long getModuleBasegjf(int pid, const char *module_name) {
    char filename[64];
    std::snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(filename, "r"), &fclose);
    if (!fp) return 0;
    char line[1024]{};
    while (std::fgets(line, sizeof(line), fp.get())) {
        if (std::strstr(line, "r-xp") && std::strstr(line, module_name)) {
            long addr{}, taddr{};
            if (std::sscanf(line, "%lx-%lx", &addr, &taddr) == 2 && (taddr - addr) == 114982912) {
                return addr;
            }
        }
    }
    return 0;
}

int ExtractPrice(const char* prop_name) {
    if (!prop_name) return -1;
    const char* p = prop_name;
    while (*p && *p != ']') p++;
    if (*p != ']' || p == prop_name) return -1;
    while (p > prop_name && *(p-1) != ' ') p--;
    return atoi(p);
}

// ========== 导航系统功能函数 ==========

// 楼层判断：Z > 190 为二楼
static inline int GetFloorFromPlayerZ(const Vector3A& pos) {
    return (pos.Z > 190.0f) ? 1 : 0;
}

// 安全钳制楼层索引到当前地图的有效范围内（防止越界导致显示错误地图）
static inline int SafeClampFloorIdx(int mapIdx, int floorIdx) {
    if (mapIdx < 0 || mapIdx >= (int)g_all_maps.size()) return 0;
    if (g_all_maps[mapIdx].empty()) return 0;
    int maxFloor = (int)g_all_maps[mapIdx].size() - 1;
    if (floorIdx > maxFloor) return maxFloor;
    if (floorIdx < 0) return 0;
    return floorIdx;
}

void LoadMapTexture(int mapIdx, int floorIdx);

void UpdateCurrentFloor() {
    if (g_current_map_index < 0 || g_current_map_index >= (int)g_all_maps.size()) return;
    auto& floors = g_all_maps[g_current_map_index];
    if (floors.empty()) return;

    int targetFloor = GetFloorFromPlayerZ(Z);

    if (targetFloor >= (int)floors.size()) return;

    if (targetFloor != g_current_floor_index) {
        g_current_floor_index = targetFloor;

        g_pt1_wx = 0.0f; g_pt1_wy = 0.0f;
        g_pt2_wx = 0.0f; g_pt2_wy = 0.0f;

        g_pt1_tu = 0.4f; g_pt1_tv = 0.45f;
        g_pt2_tu = 0.6f; g_pt2_tv = 0.55f;

        LoadMapTexture(g_current_map_index, targetFloor);
    }
}

void LoadMapTexture(int mapIdx, int floorIdx) {
    if (mapIdx < 0 || mapIdx >= MAX_MAP_COUNT || floorIdx < 0 || floorIdx >= MAX_FLOOR_COUNT) {
        snprintf(g_texture_status, sizeof(g_texture_status), "参数错误 (map=%d, floor=%d)", mapIdx, floorIdx);
        return;
    }

    while (glGetError() != GL_NO_ERROR) {}

    // 删除旧纹理（如果已经有）
    if (g_map_textures[mapIdx][floorIdx] != 0) {
        glDeleteTextures(1, &g_map_textures[mapIdx][floorIdx]);
        g_map_textures[mapIdx][floorIdx] = 0;
    }

    int safeFloor = SafeClampFloorIdx(mapIdx, floorIdx);
    const char* path = g_all_maps[mapIdx][safeFloor].texturePath;
    int w, h, n;
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) {
        const char* reason = stbi_failure_reason();
        if (!reason) reason = "未知原因";
        snprintf(g_texture_status, sizeof(g_texture_status), "图片加载失败: %s (%s)", path, reason);
        return;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR || tex == 0) {
        snprintf(g_texture_status, sizeof(g_texture_status),
                 "纹理生成失败 (ID=%u, 错误码:0x%x)", tex, err);
        stbi_image_free(data);
        return;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        snprintf(g_texture_status, sizeof(g_texture_status),
                 "纹理上传失败 (尺寸:%dx%d, 错误码:0x%x)", w, h, err);
        glDeleteTextures(1, &tex);
        stbi_image_free(data);
        return;
    }

    g_map_textures[mapIdx][floorIdx] = tex;
    stbi_image_free(data);
    snprintf(g_texture_status, sizeof(g_texture_status),
             "加载成功 (ID=%u, %dx%d)", tex, w, h);
}

// 辅助函数：检查玩家坐标是否在指定地图的任意楼层范围内
static bool IsPlayerInMapBounds(int mapIndex, const Vector3A& playerPos) {
    if (mapIndex < 0 || mapIndex >= (int)g_all_maps.size()) return false;
    for (auto& cfg : g_all_maps[mapIndex]) {
        if (playerPos.X >= cfg.minX && playerPos.X <= cfg.maxX &&
            playerPos.Y >= cfg.minY && playerPos.Y <= cfg.maxY) {
            return true;
        }
    }
    return false;
}

// 辅助函数：在所有已知地图中查找玩家所在的地图
static int FindMapByPlayerPos(const Vector3A& playerPos) {
    for (int i = 0; i < (int)g_all_maps.size(); i++) {
        if (IsPlayerInMapBounds(i, playerPos)) return i;
    }
    return -1;
}

void TryAutoDetectMap(const std::vector<DataStruct>& data) {
    if (!g_map_auto_detect || !g_map_enabled) {
        g_switch_candidate_index = -1;
        g_switch_confirm_frames = 0;
        return;
    }

    // === 第一步：扫描场景中的信号源（音乐盒 + 钢琴 + 凳子）===
    Vector3A musicbox_pos{};
    Vector3A piano_pos{};
    bool musicbox_found = false;
    bool piano_found = false;
    g_detected_chairs.clear();
    for (const auto& item : data) {
        if (item.阵营 == 6 || item.阵营 == 4) {
            if (strstr(item.类名, "prop_musicbox")) {
                musicbox_pos = getObjectCoordinates(item.objcoor, false);
                if (isValidCoordinate(musicbox_pos) &&
                    (fabsf(musicbox_pos.X) > 1.0f || fabsf(musicbox_pos.Y) > 1.0f)) {
                    musicbox_found = true;
                }
            }
            if (strstr(item.类名, "random01_in_piano01.gim")) {
                piano_pos = getObjectCoordinates(item.objcoor, false);
                if (isValidCoordinate(piano_pos) &&
                    (fabsf(piano_pos.X) > 1.0f || fabsf(piano_pos.Y) > 1.0f)) {
                    piano_found = true;
                    g_detected_piano_pos = piano_pos;
                }
            }
            // 扫描凳子（固定物体，最可靠的信号源）
            if (strstr(item.类名, "rd01_in_pianochair01.gim")) {
                Vector3A chair_pos = getObjectCoordinates(item.objcoor, false);
                if (isValidCoordinate(chair_pos) &&
                    (fabsf(chair_pos.X) > 1.0f || fabsf(chair_pos.Y) > 1.0f)) {
                    g_detected_chairs.push_back(chair_pos);
                }
            }
        }
    }

    // === 第二步：所有信号源都没找到——玩家坐标兜底 + 宽限期 ===
    if (!musicbox_found && !piano_found && g_detected_chairs.empty()) {
        g_frames_since_musicbox_lost++;
        g_switch_candidate_index = -1;
        g_switch_confirm_frames = 0;

        if (g_last_valid_map_index >= 0) {
            if (IsPlayerInMapBounds(g_last_valid_map_index, Z)) {
                if (g_current_map_index != g_last_valid_map_index) {
                    g_current_map_index = g_last_valid_map_index;
                    g_current_floor_index = g_last_valid_floor_index;
                    LoadMapTexture(g_current_map_index, g_current_floor_index);
                }
                // 在宽限期内重置帧计数器，避免"累积→超时"误判
                g_frames_since_musicbox_lost = 0;
                return;
            }
            int playerMap = FindMapByPlayerPos(Z);
            if (playerMap >= 0) {
                g_current_map_index = playerMap;
                g_current_floor_index = SafeClampFloorIdx(g_current_map_index, GetFloorFromPlayerZ(Z));
                g_last_valid_map_index = playerMap;
                g_last_valid_floor_index = g_current_floor_index;
                g_frames_since_musicbox_lost = 0;
                LoadMapTexture(g_current_map_index, g_current_floor_index);
                return;
            }
        }

        if (g_frames_since_musicbox_lost > MUSICBOX_LOST_GRACE_FRAMES) {
            g_current_map_index = -1;
            g_last_valid_map_index = -1;
            g_frames_since_musicbox_lost = 0;
        }
        return;
    }

    // === 第三步：120分制多信号评分 ===
    g_frames_since_musicbox_lost = 0;

    // 收集各地图的评分
    struct Map120Score {
        int   mapIndex = -1;
        float musicBoxScore = 0.0f;   // 0~40
        float stoolCountScore = 0.0f; // 0~20
        float stoolPosScore = 0.0f;   // 0~30
        float pianoScore = 0.0f;      // 0~30
        float totalScore = 0.0f;      // 0~120
    };
    std::vector<Map120Score> scores;

    // 遍历所有地图计算评分
    int num_maps = (int)g_all_maps.size();
    for (int mi = 0; mi < num_maps; mi++) {
        if (g_all_maps[mi].empty()) continue;

        Map120Score s;
        s.mapIndex = mi;

        // ---- 1) 音乐盒匹配 (0~40) ----
        if (musicbox_found) {
            for (auto& entry : g_musicbox_db) {
                if (entry.mapIndex != mi) continue;
                float dx = musicbox_pos.X - entry.x;
                float dy = musicbox_pos.Y - entry.y;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist <= 1.0f) {
                    s.musicBoxScore = 40.0f;
                } else {
                    s.musicBoxScore = 40.0f / dist;
                }
                break; // 每个地图只有一个音乐盒
            }
        }

        // ---- 2) 凳子数量匹配 (0~20) ----
        int known_stool_count = 0;
        for (auto& entry : g_chair_db) {
            if (entry.mapIndex == mi) known_stool_count++;
        }
        int detected_stool_count = (int)g_detected_chairs.size();
        if (known_stool_count == detected_stool_count) {
            s.stoolCountScore = 20.0f;
        } else if (abs(known_stool_count - detected_stool_count) == 1) {
            s.stoolCountScore = 10.0f;
        } else {
            s.stoolCountScore = 0.0f;
        }

        // ---- 3) 凳子位置匹配 (0~30) ----
        // 收集该地图的所有已知凳子
        std::vector<Vector3A> known_stools;
        for (auto& entry : g_chair_db) {
            if (entry.mapIndex == mi) {
                known_stools.push_back({entry.x, entry.y, entry.z});
            }
        }
        if (known_stools.empty() && detected_stool_count == 0) {
            s.stoolPosScore = 30.0f; // 都没凳子 → 满分
        } else if (known_stools.empty() && detected_stool_count > 0) {
            s.stoolPosScore = 0.0f;  // 已知无但当前有 → 0
        } else if (!known_stools.empty() && detected_stool_count == 0) {
            s.stoolPosScore = 0.0f;  // 已知有但当前无 → 0
        } else {
            // 最近邻匹配：每个检测到的凳子找最近的已知凳子，距离<2.0算成功
            int matched = 0;
            for (auto& detected : g_detected_chairs) {
                float best_dist = 1e10f;
                for (auto& known : known_stools) {
                    float dx = detected.X - known.X;
                    float dy = detected.Y - known.Y;
                    float dz = detected.Z - known.Z;
                    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                    if (dist < best_dist) best_dist = dist;
                }
                if (best_dist < 2.0f) matched++;
            }
            s.stoolPosScore = ((float)matched / (float)known_stools.size()) * 30.0f;
        }

        // ---- 4) 钢琴位置匹配 (0~30) ----
        bool hasKnownPiano = false;
        Vector3A known_piano_pos;
        for (auto& entry : g_piano_db) {
            if (entry.mapIndex == mi) {
                hasKnownPiano = true;
                known_piano_pos = {entry.x, entry.y, entry.z};
                break;
            }
        }
        bool hasDetectedPiano = (piano_found && (g_detected_piano_pos.X != 0.0f || g_detected_piano_pos.Y != 0.0f));

        if (!hasDetectedPiano && !hasKnownPiano) {
            s.pianoScore = 30.0f; // 都没钢琴 → 满分
        } else if (hasDetectedPiano && hasKnownPiano) {
            float dx = g_detected_piano_pos.X - known_piano_pos.X;
            float dy = g_detected_piano_pos.Y - known_piano_pos.Y;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist < 3.0f) s.pianoScore = 30.0f;
            else if (dist < 5.0f) s.pianoScore = 15.0f;
            else s.pianoScore = 0.0f;
        } else {
            s.pianoScore = 0.0f; // 一个有但另一个没有 → 0
        }

        // ---- 总分 (0~120) ----
        s.totalScore = s.musicBoxScore + s.stoolCountScore + s.stoolPosScore + s.pianoScore;
        scores.push_back(s);
    }

    // ---- 生成评分显示文本（取前三名，用于调试）----
    {
        // 按总分排序（降序）
        std::vector<int> sorted_idx(scores.size());
        for (int i = 0; i < (int)scores.size(); i++) sorted_idx[i] = i;
        std::sort(sorted_idx.begin(), sorted_idx.end(), [&](int a, int b) {
            return scores[a].totalScore > scores[b].totalScore;
        });

        char buf[2048];
        int pos = 0;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "识别评分:");
        int show = std::min(3, (int)sorted_idx.size());
        for (int si = 0; si < show; si++) {
            auto& s = scores[sorted_idx[si]];
            if (s.totalScore < 0.01f) continue;
            const char* name = "?";
            if (s.mapIndex >= 0 && s.mapIndex < (int)g_all_maps.size() && !g_all_maps[s.mapIndex].empty())
                name = g_all_maps[s.mapIndex][0].name;
            int chars = snprintf(buf + pos, sizeof(buf) - pos,
                "\n #%d [%d]%s: %.0f/120 [M%.0f+C%d+P%.0f+S%.0f]",
                si + 1, s.mapIndex, name, s.totalScore,
                s.musicBoxScore, (int)s.stoolCountScore,
                s.stoolPosScore, s.pianoScore);
            if (chars > 0) pos += chars;
            if (pos >= (int)sizeof(buf) - 10) break;
        }
        if (show == 0) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "\n 无匹配 (均<65)");
        } else {
            // 显示阈值的参考提示
            int topIdx = sorted_idx[0];
            if (topIdx < (int)scores.size() && scores[topIdx].totalScore < 65.0f && scores[topIdx].totalScore >= 40.0f) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, " (差距兜底)");
            }
        }
        snprintf(g_map_scores_buf, sizeof(g_map_scores_buf), "%s", buf);
    }

    // ---- 选出最佳地图（阈值65，并列处理）----
    int bestMapIndex = -1;
    float bestTotalScore = -1.0f;
    std::vector<int> tiedMaps; // 并列的地图

    for (auto& s : scores) {
        int idx = s.mapIndex;
        float total = s.totalScore;

        if (total < 65.0f) continue; // 低于阈值，忽略

        if (total > bestTotalScore + 0.001f) {
            bestTotalScore = total;
            bestMapIndex = idx;
            tiedMaps.clear();
            tiedMaps.push_back(idx);
        } else if (fabsf(total - bestTotalScore) < 0.001f) {
            tiedMaps.push_back(idx); // 并列
        }
    }

    // ---- 阈值不足时的相对差距兜底 ----
    // 如果没地图达到 65，但 #1 分数 ≥ 40 且与 #2 差距 ≥ 20 → 视为可信识别
    if (bestMapIndex < 0 && scores.size() >= 2) {
        std::sort(scores.begin(), scores.end(), [](const Map120Score& a, const Map120Score& b) {
            return a.totalScore > b.totalScore;
        });
        float top1 = scores[0].totalScore;
        float top2 = scores[1].totalScore;
        if (top1 >= 40.0f && (top1 - top2) >= 20.0f) {
            bestMapIndex = scores[0].mapIndex;
            bestTotalScore = top1;
            tiedMaps.clear();
            tiedMaps.push_back(scores[0].mapIndex);
        }
    }

    // ---- 根据最佳结果决定地图切换 ----
    // 用新评分系统：总分 >= 65 且唯一最佳 → 切换
    if (bestMapIndex >= 0) {
        // 若有多张地图并列最高分 → 歧义，不切换
        if (tiedMaps.size() > 1) {
            // 太多地图分数相同 → 保持当前地图
            g_switch_candidate_index = -1;
            g_switch_confirm_frames = 0;
            g_musicbox_moved_on_same_map = true;
            if (g_current_map_index != g_last_valid_map_index && g_last_valid_map_index >= 0) {
                g_current_map_index = g_last_valid_map_index;
                g_current_floor_index = g_last_valid_floor_index;
                LoadMapTexture(g_current_map_index, g_current_floor_index);
            }
            return;
        }

        // 唯一最佳 → 切换（或确认当前地图）
        if (bestMapIndex == g_current_map_index) {
            // 跟当前地图一致 → 锁定
            g_last_valid_map_index = bestMapIndex;
            g_last_valid_floor_index = 0;
            g_new_map_detected = false;
            g_new_map_frame_counter = 0;
            g_new_map_prompted = false;
            g_musicbox_moved_on_same_map = false;
            g_switch_candidate_index = -1;
            g_switch_confirm_frames = 0;
        } else {
            // 切换地图
            g_switch_candidate_index = -1;
            g_switch_confirm_frames = 0;
            g_current_map_index = bestMapIndex;
            g_current_floor_index = SafeClampFloorIdx(g_current_map_index, GetFloorFromPlayerZ(Z));
            g_last_valid_map_index = bestMapIndex;
            g_last_valid_floor_index = g_current_floor_index;
            g_new_map_detected = false;
            g_new_map_frame_counter = 0;
            g_new_map_prompted = false;
            g_musicbox_moved_on_same_map = false;
            g_map_stable_frames = 0;
            LoadMapTexture(g_current_map_index, g_current_floor_index);
        }
        return;
    }

    // === 第四步：所有信号源都不在数据库中 ===
    g_switch_candidate_index = -1;
    g_switch_confirm_frames = 0;

    // 情况A：玩家还在之前的地图 → 音乐盒被移动了
    if (g_last_valid_map_index >= 0 && IsPlayerInMapBounds(g_last_valid_map_index, Z)) {
        g_musicbox_moved_on_same_map = true;
        g_detected_musicbox_pos = musicbox_pos;
        if (g_current_map_index != g_last_valid_map_index) {
            g_current_map_index = g_last_valid_map_index;
            g_current_floor_index = g_last_valid_floor_index;
            LoadMapTexture(g_current_map_index, g_current_floor_index);
        }
        return;
    }

    // 情况B：玩家在某个已知地图中
    int playerMap = FindMapByPlayerPos(Z);
    if (playerMap >= 0) {
        g_current_map_index = playerMap;
        g_current_floor_index = SafeClampFloorIdx(g_current_map_index, GetFloorFromPlayerZ(Z));
        g_last_valid_map_index = playerMap;
        g_last_valid_floor_index = g_current_floor_index;
        g_detected_musicbox_pos = musicbox_pos;
        return;
    }

    // 情况C：真正的新地图
    g_current_map_index = -1;
    g_last_valid_map_index = -1;
    if (!g_new_map_detected && !g_new_map_prompted) {
        g_new_map_detected = true;
        g_new_map_prompted = true;
        g_new_map_frame_counter = 150;
        g_detected_musicbox_pos = musicbox_pos;
    }
}

static void LoadMapConfigFromJSON() {
    const std::string json_path = "/sdcard/maps/map_config.json";
    std::ifstream ifs(json_path);
    json j;

    bool json_loaded = false;
    if (ifs) {
        try {
            ifs >> j;
            if (j.contains("maps") && j["maps"].is_array()) {
                json_loaded = true;
            }
        } catch (...) {
            json_loaded = false;
        }
    }

    if (!json_loaded) {
        j = json::object();
        j["maps"] = json::array();
    }

    std::set<int> map_ids;
    for (int i = 1; i <= 100; ++i) {
        std::string path = "/sdcard/maps/map" + std::to_string(i) + "_floor1.png";
        if (std::filesystem::exists(path)) {
            map_ids.insert(i);
        } else if (i > 1 && map_ids.find(i - 1) == map_ids.end()) {
            break;
        }
    }

    bool json_modified = false;
    for (int id : map_ids) {
        std::string expected_name = "地图" + std::to_string(id) + " 一楼";
        std::string expected_path = "/sdcard/maps/map" + std::to_string(id) + "_floor1.png";

        bool found = false;
        for (auto& m : j["maps"]) {
            if (m.value("floor", 0) == 0) {
                std::string name = m.value("name", "");
                std::string tex = m.value("texture", "");
                if (name == expected_name || tex == expected_path) {
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            json new_map;
            new_map["name"] = expected_name;
            new_map["floor"] = 0;
            new_map["texture"] = expected_path;
            new_map["minX"] = -500.0;
            new_map["maxX"] = 5000.0;
            new_map["minY"] = -3000.0;
            new_map["maxY"] = 1500.0;
            new_map["music_x"] = 0.0;
            new_map["music_y"] = 0.0;
            new_map["music_z"] = 0.0;
            new_map["music_texU"] = 0.5;
            new_map["music_texV"] = 0.5;
            j["maps"].push_back(new_map);
            json_modified = true;
        }
    }

    if (json_modified) {
        std::ofstream ofs(json_path);
        if (ofs) ofs << j.dump(4);
    }

    g_all_maps.clear();
    g_musicbox_db.clear();
    g_piano_db.clear();
    g_chair_db.clear();
    g_exits.clear();

    static std::list<std::string> dynamic_names;
    static std::list<std::string> dynamic_paths;

    for (auto& m : j["maps"]) {
        int idx = static_cast<int>(g_all_maps.size());

        MapConfig cfg;
        std::string map_name = m.value("name", "未知地图");
        std::string map_tex = m.value("texture", "");
        dynamic_names.push_back(map_name);
        dynamic_paths.push_back(map_tex);
        cfg.name = dynamic_names.back().c_str();
        cfg.texturePath = dynamic_paths.back().c_str();

        cfg.floorIndex = m.value("floor", 0);
        cfg.minX = m.value("minX", -500.0);
        cfg.maxX = m.value("maxX", 5000.0);
        cfg.minY = m.value("minY", -3000.0);
        cfg.maxY = m.value("maxY", 1500.0);
        cfg.isVerticalMap = false;
        cfg.calibrated = false;

        std::vector<MapConfig> floor_vec;
        floor_vec.push_back(cfg);
        g_all_maps.push_back(floor_vec);

        if (m.contains("music_x")) {
            MusicboxKey mb;
            mb.x = m["music_x"];
            mb.y = m["music_y"];
            mb.z = m.value("music_z", 0.0);
            mb.mapIndex = idx;
            mb.floorIndex = cfg.floorIndex;
            mb.tolerance = 3.0f;
            mb.texU = m.value("music_texU", 0.5);
            mb.texV = m.value("music_texV", 0.5);
            g_musicbox_db.push_back(mb);
        }

        // 钢琴位置（第二信号源）
        if (m.contains("piano_x")) {
            PianoKey pk;
            pk.x = m["piano_x"];
            pk.y = m.value("piano_y", 0.0);
            pk.z = m.value("piano_z", 0.0);
            pk.mapIndex = idx;
            pk.tolerance = m.value("piano_tolerance", 5.0f);
            g_piano_db.push_back(pk);
        }

        // 凳子位置（第三信号源，最可靠）
        if (m.contains("chairs") && m["chairs"].is_array()) {
            for (auto& c : m["chairs"]) {
                if (c.contains("x") && c.contains("y")) {
                    ChairKey ck;
                    ck.x = c["x"]; ck.y = c["y"];
                    ck.z = c.value("z", 0.0f);
                    ck.mapIndex = idx;
                    ck.tolerance = c.value("tolerance", 4.0f);
                    g_chair_db.push_back(ck);
                }
            }
        }

        while (g_exits.size() <= idx) g_exits.push_back({});
        while (g_exits[idx].size() <= cfg.floorIndex) g_exits[idx].push_back({});
        g_exits[idx][cfg.floorIndex].clear();
        if (m.contains("exits") && m["exits"].is_array()) {
            for (auto& exit_json : m["exits"]) {
                if (exit_json.contains("x") && exit_json.contains("y")) {
                    Vector3A exitPos;
                    exitPos.X = exit_json["x"];
                    exitPos.Y = exit_json["y"];
                    exitPos.Z = exit_json.value("z", 0.0f);
                    g_exits[idx][cfg.floorIndex].push_back(exitPos);
                }
            }
        }

        g_saved_paths.clear();
        if (m.contains("player_paths") && m["player_paths"].is_array()) {
            for (auto& path_json : m["player_paths"]) {
                std::vector<Vector3A> path;
                if (path_json.contains("points") && path_json["points"].is_array()) {
                    for (auto& pt_json : path_json["points"]) {
                        Vector3A pt;
                        pt.X = pt_json.value("x", 0.0f);
                        pt.Y = pt_json.value("y", 0.0f);
                        pt.Z = pt_json.value("z", 0.0f);
                        path.push_back(pt);
                    }
                }
                g_saved_paths.push_back(path);
            }
        }

        bool has_floor1 = false;
        for (auto& m2 : j["maps"]) {
            if (m2.value("floor", 0) == 1 && m2.value("name", "") == map_name) {
                has_floor1 = true;
                break;
            }
        }

        if (!has_floor1) {
            MapConfig cfg2 = cfg;
            cfg2.floorIndex = 1;

            std::string name2 = map_name;
            size_t pos = name2.find("一楼");
            if (pos != std::string::npos)
                name2.replace(pos, strlen("一楼"), "二楼");
            else
                name2 += " 二楼";
            dynamic_names.push_back(name2);
            cfg2.name = dynamic_names.back().c_str();

            std::string path2 = map_tex;
            size_t pos2 = path2.find("floor1");
            if (pos2 != std::string::npos)
                path2.replace(pos2, 6, "floor2");
            else {
                size_t dot = path2.rfind('.');
                if (dot != std::string::npos)
                    path2.insert(dot, "_floor2");
                else
                    path2 += "_floor2.png";
            }
            dynamic_paths.push_back(path2);
            cfg2.texturePath = dynamic_paths.back().c_str();
            cfg2.calibrated = false;

            g_all_maps.back().push_back(cfg2);
        }
    }
}

// ======================================================================
// 新增工具函数：RDP 简化、线段相交、投影、图构建、Dijkstra
// ======================================================================

inline float PointToSegmentDistanceSq(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    ImVec2 ab = {b.x - a.x, b.y - a.y};
    ImVec2 ap = {p.x - a.x, p.y - a.y};
    float t = (ap.x * ab.x + ap.y * ab.y) / (ab.x * ab.x + ab.y * ab.y + 1e-6f);
    t = std::clamp(t, 0.0f, 1.0f);
    ImVec2 closest = {a.x + ab.x * t, a.y + ab.y * t};
    float dx = p.x - closest.x, dy = p.y - closest.y;
    return dx * dx + dy * dy;
}

void RDPRecursive(const std::vector<ImVec2>& points, int start, int end,
                  float epsilonSq, std::vector<bool>& keep) {
    if (end <= start + 1) return;
    float maxDistSq = 0;
    int maxIdx = start;
    for (int i = start + 1; i < end; ++i) {
        float d = PointToSegmentDistanceSq(points[i], points[start], points[end]);
        if (d > maxDistSq) {
            maxDistSq = d;
            maxIdx = i;
        }
    }
    if (maxDistSq > epsilonSq) {
        keep[maxIdx] = true;
        RDPRecursive(points, start, maxIdx, epsilonSq, keep);
        RDPRecursive(points, maxIdx, end, epsilonSq, keep);
    }
}

std::vector<ImVec2> SimplifyPathRDP(const std::vector<ImVec2>& points, float epsilon) {
    if (points.size() <= 2) return points;
    std::vector<bool> keep(points.size(), false);
    keep.front() = keep.back() = true;
    float epsilonSq = epsilon * epsilon;
    RDPRecursive(points, 0, (int)points.size() - 1, epsilonSq, keep);
    std::vector<ImVec2> result;
    for (size_t i = 0; i < points.size(); ++i)
        if (keep[i]) result.push_back(points[i]);
    return result;
}

bool LineSegmentsIntersect2D(const Vector3A& p1, const Vector3A& p2,
                             const Vector3A& q1, const Vector3A& q2,
                             Vector3A& intersection) {
    float d1x = p2.X - p1.X, d1y = p2.Y - p1.Y;
    float d2x = q2.X - q1.X, d2y = q2.Y - q1.Y;
    float cross = d1x * d2y - d1y * d2x;
    if (fabsf(cross) < 1e-6f) return false;
    float t = ((q1.X - p1.X) * d2y - (q1.Y - p1.Y) * d2x) / cross;
    float u = ((q1.X - p1.X) * d1y - (q1.Y - p1.Y) * d1x) / cross;

    // 修复：排除线段共享端点导致的无限循环
    if (t > 1e-4f && t < 1.0f - 1e-4f && u > 1e-4f && u < 1.0f - 1e-4f) {
        intersection.X = p1.X + t * d1x;
        intersection.Y = p1.Y + t * d1y;
        intersection.Z = p1.Z;
        return true;
    }
    return false;
}

Vector3A ProjectPointOnSegment(const Vector3A& pt, const Vector3A& a, const Vector3A& b) {
    Vector3A ab = {b.X - a.X, b.Y - a.Y, b.Z - a.Z};
    float lenSq = ab.X*ab.X + ab.Y*ab.Y + ab.Z*ab.Z;
    if (lenSq < 1e-6f) return a;
    float t = ((pt.X - a.X)*ab.X + (pt.Y - a.Y)*ab.Y + (pt.Z - a.Z)*ab.Z) / lenSq;
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.X + ab.X * t, a.Y + ab.Y * t, a.Z + ab.Z * t};
}

void PathGraph::buildFromSavedPaths(const std::vector<std::vector<Vector3A>>& paths,
                                    const std::vector<Vector3A>& exits) {
    nodes.clear();
    edges.clear();

    struct Segment {
        Vector3A start, end;
        std::vector<Vector3A> intermediatePoints;
    };
    std::vector<Segment> segments;

    auto addNode = [&](const Vector3A& pos) -> int {
        for (size_t i = 0; i < nodes.size(); ++i) {
            float dx = nodes[i].pos.X - pos.X;
            float dy = nodes[i].pos.Y - pos.Y;
            float dz = nodes[i].pos.Z - pos.Z;
            if (dx*dx + dy*dy + dz*dz < 1.0f) return (int)i;
        }
        GraphNode node;
        node.pos = pos;
        node.id = (int)nodes.size();
        nodes.push_back(node);
        return node.id;
    };

    for (const auto& path : paths) {
        if (path.size() < 2) continue;
        for (size_t i = 0; i < path.size() - 1; ++i) {
            Segment seg;
            seg.start = path[i];
            seg.end = path[i+1];
            seg.intermediatePoints.push_back(path[i]);
            seg.intermediatePoints.push_back(path[i+1]);
            segments.push_back(seg);
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < segments.size(); ++i) {
            for (size_t j = i + 1; j < segments.size(); ++j) {
                Vector3A inter;
                if (LineSegmentsIntersect2D(segments[i].start, segments[i].end,
                                            segments[j].start, segments[j].end, inter)) {
                    Segment segI1 = segments[i]; segI1.end = inter;
                    segI1.intermediatePoints = {segments[i].start, inter};
                    Segment segI2 = segments[i]; segI2.start = inter;
                    segI2.intermediatePoints = {inter, segments[i].end};
                    Segment segJ1 = segments[j]; segJ1.end = inter;
                    segJ1.intermediatePoints = {segments[j].start, inter};
                    Segment segJ2 = segments[j]; segJ2.start = inter;
                    segJ2.intermediatePoints = {inter, segments[j].end};

                    segments.erase(segments.begin() + std::max(i, j));
                    segments.erase(segments.begin() + std::min(i, j));
                    segments.push_back(segI1);
                    segments.push_back(segI2);
                    segments.push_back(segJ1);
                    segments.push_back(segJ2);
                    changed = true;
                    break;
                }
            }
            if (changed) break;
        }
    }

    for (auto& seg : segments) {
        int from = addNode(seg.start);
        int to   = addNode(seg.end);
        if (from == to) continue;
        GraphEdge edge;
        edge.from = from;
        edge.to = to;
        edge.weight = sqrtf((seg.end.X-seg.start.X)*(seg.end.X-seg.start.X) +
                            (seg.end.Y-seg.start.Y)*(seg.end.Y-seg.start.Y) +
                            (seg.end.Z-seg.start.Z)*(seg.end.Z-seg.start.Z));
        edge.pathPoints = seg.intermediatePoints;
        edges.push_back(edge);
    }

    for (auto& e : exits) {
        int idx = addNode(e);
        nodes[idx].isExit = true;
    }

    adj.assign(nodes.size(), std::vector<int>());
    for (size_t i = 0; i < edges.size(); ++i) {
        adj[edges[i].from].push_back((int)i);
        adj[edges[i].to].push_back((int)i);
    }

    dirty = false;
}

std::vector<int> PathGraph::dijkstra(int startNode) const {
    const int n = nodes.size();
    std::vector<float> dist(n, 1e30f);
    std::vector<int> prevEdge(n, -1);
    using P = std::pair<float, int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    dist[startNode] = 0;
    pq.push({0, startNode});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d != dist[u]) continue;
        for (int ei : adj[u]) {
            int v = (edges[ei].from == u) ? edges[ei].to : edges[ei].from;
            float nd = d + edges[ei].weight;
            if (nd < dist[v]) {
                dist[v] = nd;
                prevEdge[v] = ei;
                pq.push({nd, v});
            }
        }
    }
    return prevEdge;
}


void Draw_MapOverlay(ImDrawList* Draw, const std::vector<DataStruct>& data) {
    if (!g_map_enabled) return;
    if (g_current_map_index < 0 || g_current_map_index >= (int)g_all_maps.size()) return;
    if (g_all_maps[g_current_map_index].empty()) return;

    // 安全钳制层数索引，防止越界读取导致显示错误地图
    g_current_floor_index = SafeClampFloorIdx(g_current_map_index, g_current_floor_index);

    // 检测地图或楼层变化 → 需要重新加载纹理
    if (g_last_rendered_map_index != g_current_map_index ||
        g_last_rendered_floor_index != g_current_floor_index) {
        g_last_rendered_map_index = g_current_map_index;
        g_last_rendered_floor_index = g_current_floor_index;
        // 强制重新加载纹理（不管之前是否有缓存）
        LoadMapTexture(g_current_map_index, g_current_floor_index);
    }

    UpdateCurrentFloor();

    MapConfig cfg = g_all_maps[g_current_map_index][g_current_floor_index];

    // 地图已校准 → 自动关闭校准模式（避免坐标标记遮挡视野）
    if (cfg.calibrated && g_use_calib) {
        g_use_calib = false;
    }

    float map_h = g_map_display_size;
    float worldW = cfg.maxX - cfg.minX;
    float worldH = cfg.maxY - cfg.minY;
    float map_w = cfg.isVerticalMap ? (map_h * worldH / worldW) : (map_h * worldW / worldH);
    ImVec2 map_pos(g_map_pos_x, g_map_pos_y);
    ImVec2 map_end(map_pos.x + map_w, map_pos.y + map_h);
    Draw->AddRect(map_pos, map_end, ImColor(255, 0, 0, 255), 0, 0, 3.0f);

    // 绘制底图
    GLuint tex = g_map_textures[g_current_map_index][g_current_floor_index];
    if (tex) {
        Draw->AddImage((void*)(intptr_t)tex, map_pos, map_end, ImVec2(0,0), ImVec2(1,1),
                       IM_COL32(255, 255, 255, (int)(255 * g_map_opacity)));
    } else {
        Draw->AddRectFilled(map_pos, map_end, IM_COL32(30, 30, 30, 250), 5.0f);
        Draw->AddText(ImVec2(map_pos.x + 10, map_pos.y + 10), ImColor(255, 200, 200, 255), "纹理未加载:");
        Draw->AddText(ImVec2(map_pos.x + 10, map_pos.y + 30), ImColor(255, 150, 150, 255), g_texture_status);
    }
    Draw->AddRect(map_pos, map_end, ImColor(255, 255, 255, 100), 5.0f, 0, 1.5f);

    auto ToMap = [&](const Vector3A& pos) -> ImVec2 {
        float sx, sy, ou, ov;
        bool fx, fy;
        if (cfg.calibrated) {
            sx = cfg.scaleX; sy = cfg.scaleY;
            ou = cfg.offsetU; ov = cfg.offsetV;
            fx = cfg.flipX; fy = cfg.flipY;
        } else {
            sx = g_map_scale_x; sy = g_map_scale_y;
            ou = g_map_offset_u; ov = g_map_offset_v;
            fx = g_map_flip_x; fy = g_map_flip_y;
        }
        float u = pos.X * sx + ou;
        float v = pos.Y * sy + ov;
        if (fx) u = 1.0f - u;
        if (fy) v = 1.0f - v;
        return ImVec2(map_pos.x + u * map_w, map_pos.y + v * map_h);
    };

    // ========== 绘制网格参考线 ==========
    if (g_show_grid && cfg.calibrated) {
        float worldLeft = cfg.minX, worldRight = cfg.maxX;
        float worldTop = cfg.minY, worldBottom = cfg.maxY;
        float spacing = g_grid_spacing;
        int startX = (int)floorf(worldLeft / spacing);
        int endX   = (int)ceilf(worldRight / spacing);
        int startY = (int)floorf(worldTop / spacing);
        int endY   = (int)ceilf(worldBottom / spacing);

        ImU32 gridColor = IM_COL32(255, 255, 255, (int)(g_grid_alpha * 255));
        for (int i = startX; i <= endX; ++i) {
            float wx = i * spacing;
            ImVec2 p1 = ToMap(Vector3A(wx, worldTop, 0));
            ImVec2 p2 = ToMap(Vector3A(wx, worldBottom, 0));
            Draw->AddLine(p1, p2, gridColor, 1.0f);
        }
        for (int j = startY; j <= endY; ++j) {
            float wy = j * spacing;
            ImVec2 p1 = ToMap(Vector3A(worldLeft, wy, 0));
            ImVec2 p2 = ToMap(Vector3A(worldRight, wy, 0));
            Draw->AddLine(p1, p2, gridColor, 1.0f);
        }
    }

    // ========== 通行图构建检查（只要有已保存路径就构建，不依赖校准） ==========
    if (g_pathGraph.dirty && !g_saved_paths.empty()) {
        g_pathGraph.clear();
        if (g_current_map_index < g_exits.size() && g_current_floor_index < g_exits[g_current_map_index].size()) {
            g_pathGraph.buildFromSavedPaths(g_saved_paths, g_exits[g_current_map_index][g_current_floor_index]);
        } else {
            g_pathGraph.buildFromSavedPaths(g_saved_paths, {});
        }
        g_graph_ready = !g_pathGraph.nodes.empty();
    }

    // 绘制自身位置
    if (Z.X != 0 || Z.Y != 0) {
        ImVec2 self = ToMap(Z);
        Draw->AddCircleFilled(self, 7.0f, ImColor(0, 255, 0, (int)(200 * g_self_opacity)));
        float angle = atan2f(matrix[10], matrix[8]) + (cfg.isVerticalMap ? 1.57f : 3.14f);
        ImVec2 arrow(self.x + cosf(angle) * 15, self.y + sinf(angle) * 15);
        Draw->AddLine(self, arrow, IM_COL32(255, 255, 255, (int)(200 * g_self_opacity)), 2.0f);
    }

    auto GetOneCharLabel = [](const char* prop_name) -> const char* {
        if (strstr(prop_name, "紫宝箱"))   return "紫";
        if (strstr(prop_name, "金宝箱"))   return "金";
        if (strstr(prop_name, "隐藏宝箱")) return "藏";
        if (strstr(prop_name, "小箱子"))   return "小";
        if (strstr(prop_name, "陷阱") || strstr(prop_name, "夹子") || strstr(prop_name, "碎石")) return "阱";
        if (strstr(prop_name, "穿梭门"))   return "门";
        if (strstr(prop_name, "钢琴"))     return "琴";
        if (strstr(prop_name, "凳子"))     return "凳";
        if (strstr(prop_name, "板"))       return "板";
        if (strstr(prop_name, "花瓶"))     return "瓶";
        return "·";
    };

    g_route_item_count = 0;
    g_route_total_value = 0;

    // ========== 物品循环 ==========
    for (const auto& item : data) {
        if (item.阵营 != 6 && item.阵营 != 4) continue;
        bool isChest = (strstr(item.prop_name, "[紫宝箱]") || strstr(item.prop_name, "[金宝箱]") ||
                        strstr(item.prop_name, "[隐藏宝箱]"));
        int price = ExtractPrice(item.prop_name);
        bool isMonster = (strstr(item.类名, "monster") != nullptr);
        bool isHiddenDoor = (strcmp(item.prop_name, "[隐藏开关门]") == 0);

        if (strcmp(item.prop_name, "[被遗忘的信仰 20000]") == 0 ||
            strcmp(item.prop_name, "[庇护者之战 15000]") == 0) {
            continue;
        }

        if (!isHiddenDoor) {
            if (!isChest && !isMonster && price < g_treasure_threshold) continue;
            if (isMonster && !MjSubsystem::show_monsters) continue;
            if (price < g_route_min_value) continue;
        }

        Vector3A pos = getObjectCoordinates(item.objcoor, true);
        if (!isValidCoordinate(pos)) continue;

        if (g_current_floor_index == 0) { if (pos.Z > 175.0f) continue; }
        else if (g_current_floor_index == 1) { if (pos.Z <= 175.0f) continue; }

        if (isMonster) {
            float dist = FastMath::fastDistance(pos, Z) / 距离比例;
            if (dist > MjSubsystem::max_dist_monsters) continue;
        }

        if (price > 0) {
            g_route_item_count++;
            g_route_total_value += price;
        }

        ImVec2 p = ToMap(pos);

        // 优先级标记
        if (g_show_nav_line && g_priority_items.count(item.obj)) {
            Draw->AddCircle(p, 8.0f, IM_COL32(255, 215, 0, 200), 0, 2.5f);
        }

        ImColor color;
        if (isMonster) color = ImColor(255, 0, 0, 255);
        else if (isHiddenDoor) color = ImColor(0, 255, 255, 255);
        else if (strstr(item.prop_name, "[紫宝箱]")) color = ImColor(180, 0, 255, 255);
        else if (isChest) color = ImColor(255, 215, 0, 255);
        else color = ImColor(255, 0, 255, 255);

        Draw->AddCircleFilled(p, isMonster ? 4.0f : 5.0f, color);

        float label_font = ImGui::GetFontSize() * g_map_label_scale;
        if (isMonster) {
            Draw->AddText(ImGui::GetFont(), label_font * 0.85f,
                          ImVec2(p.x - 6, p.y - 6), IM_COL32(255, 200, 200, (int)(255 * g_label_opacity)), "怪");
        } else if (isHiddenDoor) {
            Draw->AddText(ImGui::GetFont(), label_font * 0.85f,
                          ImVec2(p.x - 6, p.y - 6), IM_COL32(0, 255, 255, (int)(255 * g_label_opacity)), "隐");
        } else {
            const char* onechar = GetOneCharLabel(item.prop_name);
            Draw->AddText(ImGui::GetFont(), label_font,
                          ImVec2(p.x - 6, p.y - 6), IM_COL32(255, 255, 255, (int)(255 * g_label_opacity)), onechar);
        }

        // 点击物品标记优先级
        if (g_show_nav_line && !g_use_calib && g_path_edit_mode == 0) {
            ImVec2 mouse = ImGui::GetMousePos();
            float dx = mouse.x - p.x;
            float dy = mouse.y - p.y;
            if (dx*dx + dy*dy < 100.0f && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (g_priority_items.count(item.obj)) {
                    g_priority_items.erase(item.obj);
                } else {
                    g_priority_items.insert(item.obj);
                }
            }
        }
    }

    // ========== 智能路线规划（自动识别贵重物品 + 出口，无需手动标记） ==========
    if (g_show_nav_line && g_graph_ready && !g_pathGraph.nodes.empty()) {
        auto findNearestNode = [&](const Vector3A& worldPos) -> int {
            int best = -1;
            float bestDist = 1e30f;
            for (int i = 0; i < (int)g_pathGraph.nodes.size(); i++) {
                float dx = g_pathGraph.nodes[i].pos.X - worldPos.X;
                float dy = g_pathGraph.nodes[i].pos.Y - worldPos.Y;
                float dz = g_pathGraph.nodes[i].pos.Z - worldPos.Z;
                float d = dx*dx + dy*dy + dz*dz;
                if (d < bestDist) { bestDist = d; best = i; }
            }
            return best;
        };

        // 1. 收集全部"有价值"目标坐标（贵重物品节点 + 出口节点）
        std::vector<Vector3A> autoTargets;
        std::vector<const char*> autoTargetNames;

        // 1a. 扫描场景中所有物品，筛选出贵重物品
        for (const auto& item : data) {
            if (item.阵营 != 6 && item.阵营 != 4) continue;
            bool isHidden = (strstr(item.prop_name, "[隐藏宝箱]") != nullptr);
            if (isHidden) continue; // 明确排除隐藏宝箱

            bool isPurple = (strstr(item.prop_name, "[紫宝箱]") != nullptr);
            bool isGold   = (strstr(item.prop_name, "[金宝箱]") != nullptr);
            bool isMusicBox = (strstr(item.类名, "prop_musicbox") != nullptr);
            int price = ExtractPrice(item.prop_name);

            bool isHighValue = (price >= (int)g_treasure_threshold);
            bool isTarget = isPurple || isGold || isMusicBox || isHighValue;
            if (!isTarget) continue;

            Vector3A pos = getObjectCoordinates(item.objcoor, true);
            if (!isValidCoordinate(pos)) continue;

            // 楼层过滤
            if (g_current_floor_index == 0) { if (pos.Z > 175.0f) continue; }
            else if (g_current_floor_index == 1) { if (pos.Z <= 175.0f) continue; }

            // 检查是否能映射到路径网络
            int node = findNearestNode(pos);
            if (node < 0) continue;

            autoTargets.push_back(pos);
            autoTargetNames.push_back(item.prop_name[0] ? item.prop_name : "宝藏");
        }

        // 1b. 出口节点单独保存（不混入物品目标，规划完成后再走最优出口）
        std::vector<Vector3A> exitTargets;
        if (g_current_map_index < (int)g_exits.size() &&
            g_current_floor_index < (int)g_exits[g_current_map_index].size()) {
            for (auto& e : g_exits[g_current_map_index][g_current_floor_index]) {
                int node = findNearestNode(e);
                if (node >= 0) {
                    exitTargets.push_back(e);
                }
            }
        }

        // 2. 如果存在自动识别的目标，且没有手动标记时，自动规划
        if (!autoTargets.empty() && g_priority_items.empty()) {
            int playerNode = findNearestNode(Z);
            if (playerNode >= 0) {
                // 贪心最近邻 TSP：从玩家出发→遍历所有物品→最后选最近出口
                std::vector<int> visitOrder;
                std::vector<Vector3A> visitedPoses;
                std::vector<const char*> visitedNames;
                std::vector<bool> visited(autoTargets.size(), false);

                Vector3A currentPos = Z;
                int visitedCount = 0;

                // 2a. 先走所有物品节点
                while (visitedCount < (int)autoTargets.size()) {
                    int bestIdx = -1;
                    float bestDist2 = 1e30f;
                    for (size_t i = 0; i < autoTargets.size(); i++) {
                        if (visited[i]) continue;
                        float dx = autoTargets[i].X - currentPos.X;
                        float dy = autoTargets[i].Y - currentPos.Y;
                        float d2 = dx*dx + dy*dy;
                        if (d2 < bestDist2) { bestDist2 = d2; bestIdx = (int)i; }
                    }
                    if (bestIdx < 0) break;

                    visited[bestIdx] = true;
                    visitedCount++;
                    visitOrder.push_back(findNearestNode(autoTargets[bestIdx]));
                    visitedPoses.push_back(autoTargets[bestIdx]);
                    visitedNames.push_back(autoTargetNames[bestIdx]);
                    currentPos = autoTargets[bestIdx];
                }

                // 2b. 物品走完后→选最近出口作为最终目的地
                Vector3A finalExitPos{};
                int finalExitNode = -1;
                float bestExitDist2 = 1e30f;
                for (auto& epos : exitTargets) {
                    float dx = epos.X - currentPos.X;
                    float dy = epos.Y - currentPos.Y;
                    float d2 = dx*dx + dy*dy;
                    if (d2 < bestExitDist2) {
                        bestExitDist2 = d2;
                        finalExitPos = epos;
                        finalExitNode = findNearestNode(epos);
                    }
                }
                bool hasExitSegment = (finalExitNode >= 0 && finalExitNode != (visitOrder.empty() ? playerNode : visitOrder.back()));

                // 3. 渲染自动规划的路线
                float totalMetersAuto = 0.0f;
                int curNode = playerNode;
                for (size_t t = 0; t < visitOrder.size(); t++) {
                    int targetNode = visitOrder[t];
                    if (targetNode == curNode) continue;

                    auto prevEdge = g_pathGraph.dijkstra(curNode);
                    if (prevEdge[targetNode] < 0) continue;

                    std::vector<int> nodeSeq;
                    for (int c = targetNode; c != curNode; ) {
                        nodeSeq.push_back(c);
                        int ei = prevEdge[c];
                        int prev = (g_pathGraph.edges[ei].from == c)
                                  ? g_pathGraph.edges[ei].to
                                  : g_pathGraph.edges[ei].from;
                        c = prev;
                    }
                    nodeSeq.push_back(curNode);
                    std::reverse(nodeSeq.begin(), nodeSeq.end());

                    // 绘制物品段路径（绿色双线，区别于蓝色已保存路径和橙色手动规划）
                    bool isExitSegment = false;
                    for (size_t i = 1; i < nodeSeq.size(); i++) {
                        ImVec2 p1 = ToMap(g_pathGraph.nodes[nodeSeq[i-1]].pos);
                        ImVec2 p2 = ToMap(g_pathGraph.nodes[nodeSeq[i]].pos);

                        float dx = p2.x - p1.x, dy = p2.y - p1.y;
                        float segLen = sqrtf(dx*dx + dy*dy);
                        totalMetersAuto += segLen;

                        ImU32 colOuter, colInner, colArrow;
                        if (isExitSegment) {
                            colOuter = IM_COL32(255, 200, 50, 60);
                            colInner = IM_COL32(255, 220, 80, 220);
                            colArrow = IM_COL32(255, 230, 100, 200);
                        } else {
                            colOuter = IM_COL32(50, 220, 80, 60);
                            colInner = IM_COL32(50, 255, 80, 220);
                            colArrow = IM_COL32(100, 255, 130, 200);
                        }
                        Draw->AddLine(p1, p2, colOuter, 8.0f);
                        Draw->AddLine(p1, p2, colInner, 3.0f);

                        // 增大箭头（上限从12→20）
                        float arrowScale = std::min(segLen * 0.2f, 20.0f);
                        if (arrowScale > 6.0f) {
                            ImVec2 mid((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
                            float angle = atan2f(dy, dx);
                            ImVec2 tip(mid.x + cosf(angle) * arrowScale, mid.y + sinf(angle) * arrowScale);
                            ImVec2 left(mid.x + cosf(angle + 2.5f) * arrowScale * 0.6f, mid.y + sinf(angle + 2.5f) * arrowScale * 0.6f);
                            ImVec2 right(mid.x + cosf(angle - 2.5f) * arrowScale * 0.6f, mid.y + sinf(angle - 2.5f) * arrowScale * 0.6f);
                            Draw->AddTriangleFilled(tip, left, right, colArrow);
                        }
                    }

                    // 目标节点标记
                    ImVec2 targetScreen = ToMap(g_pathGraph.nodes[targetNode].pos);
                    ImVec2 itemScreen = ToMap(visitedPoses[t]);
                    char seqLabel[8];
                    snprintf(seqLabel, sizeof(seqLabel), "%d★", (int)t + 1);
                    ImVec2 lblSize = ImGui::CalcTextSize(seqLabel);
                    Draw->AddRectFilled(ImVec2(itemScreen.x - lblSize.x * 0.5f - 5, itemScreen.y - 20),
                                        ImVec2(itemScreen.x + lblSize.x * 0.5f + 5, itemScreen.y),
                                        IM_COL32(0, 0, 0, 180), 4.0f);
                    Draw->AddText(g_font_ui, ImGui::GetFontSize() * 1.1f,
                                  ImVec2(itemScreen.x - lblSize.x * 0.5f, itemScreen.y - 18),
                                  IM_COL32(255, 215, 0, 255), seqLabel);
                    Draw->AddLine(targetScreen, itemScreen, IM_COL32(50, 220, 80, 150), 2.0f);
                    Draw->AddCircle(itemScreen, 10.0f, IM_COL32(255, 215, 0, 255), 0, 2.0f);

                    curNode = targetNode;
                }

                // 3b. 最终段：当前物品→最优出口
                if (hasExitSegment) {
                    auto prevEdgeExit = g_pathGraph.dijkstra(curNode);
                    if (prevEdgeExit[finalExitNode] >= 0) {
                        std::vector<int> exitSeq;
                        for (int c = finalExitNode; c != curNode; ) {
                            exitSeq.push_back(c);
                            int ei = prevEdgeExit[c];
                            int prev = (g_pathGraph.edges[ei].from == c)
                                      ? g_pathGraph.edges[ei].to
                                      : g_pathGraph.edges[ei].from;
                            c = prev;
                        }
                        exitSeq.push_back(curNode);
                        std::reverse(exitSeq.begin(), exitSeq.end());

                        for (size_t i = 1; i < exitSeq.size(); i++) {
                            ImVec2 p1 = ToMap(g_pathGraph.nodes[exitSeq[i-1]].pos);
                            ImVec2 p2 = ToMap(g_pathGraph.nodes[exitSeq[i]].pos);
                            float dx = p2.x - p1.x, dy = p2.y - p1.y;
                            float segLen = sqrtf(dx*dx + dy*dy);
                            totalMetersAuto += segLen;

                            // 出口段用金色
                            Draw->AddLine(p1, p2, IM_COL32(255, 200, 50, 60), 8.0f);
                            Draw->AddLine(p1, p2, IM_COL32(255, 220, 80, 220), 3.0f);

                            float arrowScale = std::min(segLen * 0.2f, 20.0f);
                            if (arrowScale > 6.0f) {
                                ImVec2 mid((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
                                float angle = atan2f(dy, dx);
                                ImVec2 tip(mid.x + cosf(angle) * arrowScale, mid.y + sinf(angle) * arrowScale);
                                ImVec2 left(mid.x + cosf(angle + 2.5f) * arrowScale * 0.6f, mid.y + sinf(angle + 2.5f) * arrowScale * 0.6f);
                                ImVec2 right(mid.x + cosf(angle - 2.5f) * arrowScale * 0.6f, mid.y + sinf(angle - 2.5f) * arrowScale * 0.6f);
                                Draw->AddTriangleFilled(tip, left, right, IM_COL32(255, 230, 100, 200));
                            }
                        }

                        // 出口标记
                        ImVec2 exitScreen = ToMap(finalExitPos);
                        Draw->AddRectFilled(ImVec2(exitScreen.x - 8, exitScreen.y - 8),
                                            ImVec2(exitScreen.x + 8, exitScreen.y + 8),
                                            IM_COL32(255, 220, 50, 220), 3.0f);
                        Draw->AddText(ImGui::GetFont(), 14.0f,
                                      ImVec2(exitScreen.x + 10, exitScreen.y - 8),
                                      IM_COL32(255, 220, 50, 220), "▶ 出口");
                    }
                }

                // 左上角绿色距离汇总
                if (totalMetersAuto > 0.0f) {
                    char distInfo[64];
                    snprintf(distInfo, sizeof(distInfo), "路线总距离: %.0fm", totalMetersAuto);
                    // 半透明背景 + 更大字体
                    ImVec2 ts = ImGui::CalcTextSize(distInfo);
                    float pad = 6.0f;
                    Draw->AddRectFilled(ImVec2(map_pos.x + 2, map_pos.y + 2),
                                        ImVec2(map_pos.x + ts.x + pad * 2 + 4, map_pos.y + ts.y + pad * 2 + 4),
                                        IM_COL32(0, 0, 0, 160), 6.0f);
                    Draw->AddText(ImGui::GetFont(), 18.0f,
                                  ImVec2(map_pos.x + 4 + pad, map_pos.y + 4 + pad),
                                  IM_COL32(50, 255, 50, 240), distInfo);
                }
            }
        }
    }

    // ========== 手动标记路线规划（原逻辑保留） ==========
    if (g_show_nav_line && g_graph_ready && !g_pathGraph.nodes.empty() && !g_priority_items.empty()) {
        // 辅助：找到离世界坐标最近的图节点
        auto findNearestNode = [&](const Vector3A& worldPos) -> int {
            int best = -1;
            float bestDist = 1e30f;
            for (int i = 0; i < (int)g_pathGraph.nodes.size(); i++) {
                float dx = g_pathGraph.nodes[i].pos.X - worldPos.X;
                float dy = g_pathGraph.nodes[i].pos.Y - worldPos.Y;
                float dz = g_pathGraph.nodes[i].pos.Z - worldPos.Z;
                float d = dx*dx + dy*dy + dz*dz;
                if (d < bestDist) { bestDist = d; best = i; }
            }
            return best;
        };

        // 找到玩家的最近路径节点
        int playerNode = findNearestNode(Z);
        if (playerNode >= 0) {
            // 为每个优先物品规划路线
            for (const auto& item : data) {
                if (!g_priority_items.count(item.obj)) continue;
                Vector3A itemPos = getObjectCoordinates(item.objcoor, false);
                if (!isValidCoordinate(itemPos)) continue;

                int targetNode = findNearestNode(itemPos);
                if (targetNode < 0 || targetNode == playerNode) continue;

                // Dijkstra 最短路径
                auto prevEdge = g_pathGraph.dijkstra(playerNode);
                if (prevEdge[targetNode] < 0) continue; // 不可达

                // 回溯构建路径节点序列
                std::vector<int> nodeSeq;
                for (int cur = targetNode; cur != playerNode; ) {
                    nodeSeq.push_back(cur);
                    int ei = prevEdge[cur];
                    int prev = (g_pathGraph.edges[ei].from == cur)
                              ? g_pathGraph.edges[ei].to
                              : g_pathGraph.edges[ei].from;
                    cur = prev;
                }
                nodeSeq.push_back(playerNode);
                std::reverse(nodeSeq.begin(), nodeSeq.end());

                // 在路径网络内绘制规划路线
                float totalSegDist = 0.0f;
                for (size_t i = 1; i < nodeSeq.size(); i++) {
                    ImVec2 p1 = ToMap(g_pathGraph.nodes[nodeSeq[i-1]].pos);
                    ImVec2 p2 = ToMap(g_pathGraph.nodes[nodeSeq[i]].pos);

                    float dx = p2.x - p1.x, dy = p2.y - p1.y;
                    float segLen = sqrtf(dx*dx + dy*dy);
                    totalSegDist += segLen;

                    // 橙色双线：外层粗半透明 + 内层细亮色
                    Draw->AddLine(p1, p2, IM_COL32(255, 140, 0, 60), 8.0f);
                    Draw->AddLine(p1, p2, IM_COL32(255, 165, 0, 220), 3.0f);

                    // === 方向箭头指示（每段中点画一个箭头） ===
                    float arrowScale = std::min(segLen * 0.15f, 12.0f);
                    if (arrowScale > 4.0f) {
                        ImVec2 mid((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
                        float angle = atan2f(dy, dx);
                        ImVec2 tip = ImVec2(mid.x + cosf(angle) * arrowScale,
                                             mid.y + sinf(angle) * arrowScale);
                        ImVec2 left = ImVec2(mid.x + cosf(angle + 2.5f) * arrowScale * 0.6f,
                                              mid.y + sinf(angle + 2.5f) * arrowScale * 0.6f);
                        ImVec2 right = ImVec2(mid.x + cosf(angle - 2.5f) * arrowScale * 0.6f,
                                               mid.y + sinf(angle - 2.5f) * arrowScale * 0.6f);
                        Draw->AddTriangleFilled(tip, left, right, IM_COL32(255, 200, 50, 200));
                    }
                }

                // 计算总距离（世界单位转米）
                float totalWorldDist = 0.0f;
                for (size_t i = 1; i < nodeSeq.size(); i++) {
                    float wx = g_pathGraph.nodes[nodeSeq[i]].pos.X - g_pathGraph.nodes[nodeSeq[i-1]].pos.X;
                    float wy = g_pathGraph.nodes[nodeSeq[i]].pos.Y - g_pathGraph.nodes[nodeSeq[i-1]].pos.Y;
                    totalWorldDist += sqrtf(wx*wx + wy*wy);
                }
                float totalMeters = totalWorldDist / 距离比例;
                g_total_route_distance = totalMeters;

                // 绘制目标物品到最近节点的"最后一米"连接线（虚线效果）
                ImVec2 nodePos = ToMap(g_pathGraph.nodes[targetNode].pos);
                ImVec2 itemScreen = ToMap(itemPos);
                Draw->AddLine(nodePos, itemScreen, IM_COL32(255, 200, 50, 150), 2.0f);
                // 物品位置高亮
                Draw->AddCircle(itemScreen, 10.0f, IM_COL32(255, 215, 0, 255), 0, 2.0f);

                // === 目标物品信息标注 ===
                float itemWorldDist = sqrtf((itemPos.X - g_pathGraph.nodes[targetNode].pos.X) *
                                              (itemPos.X - g_pathGraph.nodes[targetNode].pos.X) +
                                              (itemPos.Y - g_pathGraph.nodes[targetNode].pos.Y) *
                                              (itemPos.Y - g_pathGraph.nodes[targetNode].pos.Y)) / 距离比例;
                char itemInfo[64];
                const char* itemN = item.prop_name[0] ? item.prop_name : "";
                if (itemN[0] == '[') itemN++;  // 去掉 '[' 前缀用于显示
                snprintf(itemInfo, sizeof(itemInfo), "%s 总程:%.0fm 末段:%.0fm",
                         strlen(itemN) > 0 ? itemN : "目标",
                         totalMeters, itemWorldDist);
                ImVec2 infoPos(itemScreen.x - 30, itemScreen.y - 28);
                ImVec2 infoSize = ImGui::CalcTextSize(itemInfo);
                Draw->AddRectFilled(ImVec2(infoPos.x - 4, infoPos.y - 3),
                                    ImVec2(infoPos.x + infoSize.x + 4, infoPos.y + infoSize.y + 3),
                                    IM_COL32(0, 0, 0, 170), 4.0f);
                Draw->AddText(infoPos, IM_COL32(255, 230, 120, 255), itemInfo);
            }
        }
    }

    // ========== 路线总距离（小地图左上角绿色文字） ==========
    if (g_show_nav_line && g_graph_ready && (g_route_total_value > 0 || g_total_route_distance > 0.0f)) {
        char distInfo[64];
        snprintf(distInfo, sizeof(distInfo), "总距离: %.0fm  总价值: %d", g_total_route_distance, g_route_total_value);
        ImVec2 ts = ImGui::CalcTextSize(distInfo);
        float pad = 6.0f;
        Draw->AddRectFilled(ImVec2(map_pos.x + 2, map_pos.y + 2),
                            ImVec2(map_pos.x + ts.x + pad * 2 + 4, map_pos.y + ts.y + pad * 2 + 4),
                            IM_COL32(0, 0, 0, 160), 6.0f);
        Draw->AddText(ImGui::GetFont(), 18.0f,
                      ImVec2(map_pos.x + 4 + pad, map_pos.y + 4 + pad),
                      IM_COL32(50, 255, 50, 240), distInfo);
    }

    // ========== 已保存路径绘制（带开关控制 + 选中高亮） ==========
    if (g_show_saved_paths) {
        for (size_t pi = 0; pi < g_saved_paths.size(); pi++) {
            auto& path = g_saved_paths[pi];
            if (path.size() < 2) continue;

            bool isSelected = (pi == g_selected_path_index);

            // 选中路径使用亮黄色 + 加粗，未选中使用标准蓝色
            ImU32 color_outer = isSelected
                ? IM_COL32(255, 255, 0, (int)(120 * g_saved_path_opacity))
                : IM_COL32(0, 180, 255, (int)(60 * g_saved_path_opacity));
            ImU32 color_inner = isSelected
                ? IM_COL32(255, 255, 100, (int)(255 * g_saved_path_opacity))
                : IM_COL32(0, 220, 255, (int)(200 * g_saved_path_opacity));
            float outer_width = isSelected ? 18.0f : 12.0f;
            float inner_width = isSelected ? 5.0f : 3.0f;

            for (size_t i = 1; i < path.size(); i++) {
                ImVec2 p1 = ToMap(path[i-1]);
                ImVec2 p2 = ToMap(path[i]);
                Draw->AddLine(p1, p2, color_outer, outer_width);
                Draw->AddLine(p1, p2, color_inner, inner_width);
            }
            ImVec2 start = ToMap(path.front());
            ImVec2 end = ToMap(path.back());
            Draw->AddCircleFilled(start, isSelected ? 7.0f : 5.0f,
                isSelected ? IM_COL32(255, 255, 0, 255) : IM_COL32(0, 255, 0, 200));
            Draw->AddCircleFilled(end, isSelected ? 7.0f : 5.0f,
                isSelected ? IM_COL32(255, 200, 0, 255) : IM_COL32(255, 0, 0, 200));
        }
    }

    // ========== 调试显示通行网络 ==========
    if (g_show_graph_debug && g_graph_ready) {
        for (auto& edge : g_pathGraph.edges) {
            ImVec2 p1 = ToMap(g_pathGraph.nodes[edge.from].pos);
            ImVec2 p2 = ToMap(g_pathGraph.nodes[edge.to].pos);
            Draw->AddLine(p1, p2, IM_COL32(0, 255, 255, 100), 1.5f);
        }
        for (auto& node : g_pathGraph.nodes) {
            ImVec2 p = ToMap(node.pos);
            if (node.isExit) Draw->AddCircleFilled(p, 5.0f, IM_COL32(0, 255, 0, 150));
            else Draw->AddCircleFilled(p, 3.0f, IM_COL32(255, 255, 255, 100));
        }
    }

    // ========== 正在绘制的路径 ==========
    if (!g_current_drawing_path.empty()) {
        for (size_t i = 0; i < g_current_drawing_path.size(); i++) {
            ImVec2 p = ToMap(g_current_drawing_path[i]);
            Draw->AddCircleFilled(p, 5.0f, IM_COL32(255, 255, 0, (int)(200 * g_route_opacity)));
            if (i > 0) {
                ImVec2 prev_p = ToMap(g_current_drawing_path[i-1]);
                Draw->AddLine(prev_p, p, IM_COL32(255, 255, 0, (int)(180 * g_route_opacity)), 4.0f);
            }
        }
    }

    // ========== 出口标记（点击可删除） ==========
    if (g_current_map_index < g_exits.size() && g_current_floor_index < g_exits[g_current_map_index].size()) {
        auto& exits = g_exits[g_current_map_index][g_current_floor_index];
        for (size_t ei = 0; ei < exits.size(); ei++) {
            auto& e = exits[ei];
            ImVec2 p = ToMap(e);
            // 出口方块加编号
            Draw->AddRectFilled(ImVec2(p.x-6, p.y-6), ImVec2(p.x+6, p.y+6), IM_COL32(0, 255, 80, (int)(220 * g_label_opacity)), 2.0f);
            char eLabel[16];
            snprintf(eLabel, sizeof(eLabel), "出口%zu", ei+1);
            Draw->AddText(ImGui::GetFont(), 12.0f, ImVec2(p.x + 8, p.y - 8), IM_COL32(0, 255, 80, (int)(200 * g_label_opacity)), eLabel);

            // 点击出口弹出删除确认
            ImVec2 ms = ImGui::GetMousePos();
            float d = sqrtf((ms.x - p.x)*(ms.x - p.x) + (ms.y - p.y)*(ms.y - p.y));
            if (d < 15.0f && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                g_del_exit_idx = (int)ei;
                ImGui::OpenPopup("ExitDeletePopup");
            }
        }
    }

    // 出口删除确认弹窗
    if (ImGui::BeginPopup("ExitDeletePopup")) {
        ImGui::Text("删除此出口?");
        if (ImGui::Button("确定删除")) {
            g_exits[g_current_map_index][g_current_floor_index].erase(
                g_exits[g_current_map_index][g_current_floor_index].begin() + g_del_exit_idx);
            SaveExitsToJSON(g_current_map_index, g_current_floor_index);
            g_pathGraph.dirty = true;
            AddNotification("出口已删除", 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ========== 校准标记绘制与交互 ==========
    if (g_use_calib) {
        auto TexToScreen = [&](float u, float v) -> ImVec2 {
            return ImVec2(map_pos.x + u * map_w, map_pos.y + v * map_h);
        };

        ImVec2 p1 = TexToScreen(g_pt1_tu, g_pt1_tv);
        Draw->AddLine(ImVec2(p1.x - 15, p1.y), ImVec2(p1.x + 15, p1.y), ImColor(255, 165, 0, 220), 2.0f);
        Draw->AddLine(ImVec2(p1.x, p1.y - 15), ImVec2(p1.x, p1.y + 15), ImColor(255, 165, 0, 220), 2.0f);
        Draw->AddCircle(p1, 8.0f, ImColor(255, 165, 0, 255), 0, 2.0f);
        float cal_font = ImGui::GetFontSize() * g_map_label_scale;
        Draw->AddText(ImGui::GetFont(), cal_font, ImVec2(p1.x + 12, p1.y - 12), ImColor(255, 200, 100, 255), "音乐盒");

        ImVec2 p2 = TexToScreen(g_pt2_tu, g_pt2_tv);
        Draw->AddLine(ImVec2(p2.x - 15, p2.y), ImVec2(p2.x + 15, p2.y), ImColor(0, 150, 255, 220), 2.0f);
        Draw->AddLine(ImVec2(p2.x, p2.y - 15), ImVec2(p2.x, p2.y + 15), ImColor(0, 150, 255, 220), 2.0f);
        Draw->AddCircle(p2, 8.0f, ImColor(0, 150, 255, 255), 0, 2.0f);
        Draw->AddText(ImGui::GetFont(), cal_font, ImVec2(p2.x + 12, p2.y - 12), ImColor(100, 200, 255, 255), "大门");

        ImVec2 mouse_pos = ImGui::GetMousePos();
        bool mouse_in_map = (mouse_pos.x >= map_pos.x && mouse_pos.x <= map_end.x &&
                             mouse_pos.y >= map_pos.y && mouse_pos.y <= map_end.y);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse_in_map) {
            float dist1 = sqrtf((mouse_pos.x - p1.x) * (mouse_pos.x - p1.x) + (mouse_pos.y - p1.y) * (mouse_pos.y - p1.y));
            float dist2 = sqrtf((mouse_pos.x - p2.x) * (mouse_pos.x - p2.x) + (mouse_pos.y - p2.y) * (mouse_pos.y - p2.y));
            if (dist1 < 20.0f && dist1 <= dist2) g_drag_point = 1;
            else if (dist2 < 20.0f) g_drag_point = 2;
            else g_drag_point = 0;
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && g_drag_point != 0 && mouse_in_map) {
            float u = (mouse_pos.x - map_pos.x) / map_w;
            float v = (mouse_pos.y - map_pos.y) / map_h;
            u = std::clamp(u, 0.0f, 1.0f);
            v = std::clamp(v, 0.0f, 1.0f);
            if (g_drag_point == 1) {
                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                }
                g_pt1_tu = u; g_pt1_tv = v;
            } else if (g_drag_point == 2) {
                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                }
                g_pt2_tu = u; g_pt2_tv = v;
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) g_drag_point = 0;

        if (mouse_in_map) {
            float hint_font = ImGui::GetFontSize() * g_map_label_scale * 0.9f;
            Draw->AddText(ImGui::GetFont(), hint_font,
                          ImVec2(map_pos.x + 5, map_end.y - 22), ImColor(255, 255, 255, 200),
                          g_drag_point == 0 ? "拖拽标记点定位" : (g_drag_point == 1 ? "移动音乐盒..." : "移动大门..."));
        }
    }

    // ========== 长按菜单（替代右键） ==========
    ImVec2 mouse = ImGui::GetMousePos();
    bool hover_map = (mouse.x >= map_pos.x && mouse.x <= map_end.x && mouse.y >= map_pos.y && mouse.y <= map_end.y);

    if (hover_map && cfg.calibrated && g_path_edit_mode == 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (g_press_timer == 0) g_press_pos = mouse;
            g_press_timer += ImGui::GetIO().DeltaTime;
            if (fabsf(mouse.x - g_press_pos.x) > 5 || fabsf(mouse.y - g_press_pos.y) > 5) {
                g_press_timer = 0;
            }
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (g_press_timer > 0.5f) {
                ImGui::OpenPopup("MapLongPressPopup");
            }
            g_press_timer = 0;
        }
    }

    if (ImGui::BeginPopup("MapLongPressPopup")) {
        ImGui::Text("选择操作:");
        ImGui::Separator();

        if (ImGui::Button("标记为出口")) {
            float u_click = (mouse.x - map_pos.x) / map_w;
            float v_click = (mouse.y - map_pos.y) / map_h;
            float worldX = (u_click - cfg.offsetU) / cfg.scaleX;
            float worldY = (v_click - cfg.offsetV) / cfg.scaleY;

            while (g_exits.size() <= g_current_map_index) g_exits.push_back({});
            while (g_exits[g_current_map_index].size() <= g_current_floor_index) g_exits[g_current_map_index].push_back({});
            g_exits[g_current_map_index][g_current_floor_index].push_back(Vector3A(worldX, worldY, Z.Z));
            SaveExitsToJSON(g_current_map_index, g_current_floor_index);
            g_pathGraph.dirty = true;
            AddNotification("出口已标记", 2.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("设为途经点")) {
            float u_click = (mouse.x - map_pos.x) / map_w;
            float v_click = (mouse.y - map_pos.y) / map_h;
            g_via_point = Vector3A((u_click - cfg.offsetU) / cfg.scaleX, (v_click - cfg.offsetV) / cfg.scaleY, Z.Z);
            g_has_via_point = true;
            AddNotification("途经点已设置", 2.0f, ImVec4(1.0f, 0.5f, 1.0f, 1.0f));
            ImGui::CloseCurrentPopup();
        }

        if (g_has_via_point) {
            if (ImGui::Button("清除途经点")) {
                g_has_via_point = false;
                AddNotification("途经点已清除", 2.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                ImGui::CloseCurrentPopup();
            }
        }

        int nearby_exit = -1;
        if (g_current_map_index < g_exits.size() && g_current_floor_index < g_exits[g_current_map_index].size()) {
            for (size_t i = 0; i < g_exits[g_current_map_index][g_current_floor_index].size(); i++) {
                auto& e = g_exits[g_current_map_index][g_current_floor_index][i];
                ImVec2 ep = ToMap(e);
                float dist = sqrtf((mouse.x - ep.x)*(mouse.x - ep.x) + (mouse.y - ep.y)*(mouse.y - ep.y));
                if (dist < 12.0f) { nearby_exit = i; break; }
            }
        }
        if (nearby_exit >= 0) {
            ImGui::Separator();
            if (ImGui::Button("删除此出口")) {
                g_exits[g_current_map_index][g_current_floor_index].erase(
                        g_exits[g_current_map_index][g_current_floor_index].begin() + nearby_exit);
                SaveExitsToJSON(g_current_map_index, g_current_floor_index);
                g_pathGraph.dirty = true;
                AddNotification("出口已删除", 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::CloseCurrentPopup();
            }
        }

        int nearby_path = -1;
        for (size_t i = 0; i < g_saved_paths.size(); i++) {
            for (size_t j = 0; j < g_saved_paths[i].size(); j++) {
                ImVec2 pp = ToMap(g_saved_paths[i][j]);
                float dist = sqrtf((mouse.x - pp.x)*(mouse.x - pp.x) + (mouse.y - pp.y)*(mouse.y - pp.y));
                if (dist < 10.0f) { nearby_path = i; break; }
            }
            if (nearby_path >= 0) break;
        }
        if (nearby_path >= 0) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // 左键点击路径 → 选中该路径（高亮）
                g_selected_path_index = nearby_path;
            }
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "选中: 路径 #%d", nearby_path);
            if (ImGui::Button("删除此路径") || ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                g_saved_paths.erase(g_saved_paths.begin() + nearby_path);
                if (g_selected_path_index == nearby_path) g_selected_path_index = -1;
                else if (g_selected_path_index > nearby_path) g_selected_path_index--;
                SavePlayerPathsToJSON(g_current_map_index, g_current_floor_index);
                g_pathGraph.dirty = true;
                AddNotification("路径已删除", 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    // ========== 全局路径操作：Delete键删除 + 点击空白取消选中 ==========
    if (g_selected_path_index >= 0 && g_selected_path_index < (int)g_saved_paths.size()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            g_saved_paths.erase(g_saved_paths.begin() + g_selected_path_index);
            g_selected_path_index = -1;
            SavePlayerPathsToJSON(g_current_map_index, g_current_floor_index);
            g_pathGraph.dirty = true;
            AddNotification("路径已删除", 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        }
        // 点击地图空白区域取消选中
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hover_map) {
            // 检查是否点击在路径上
            bool clicked_on_path = false;
            for (size_t i = 0; i < g_saved_paths.size() && !clicked_on_path; i++) {
                for (size_t j = 0; j < g_saved_paths[i].size() && !clicked_on_path; j++) {
                    ImVec2 pp = ToMap(g_saved_paths[i][j]);
                    float dist = sqrtf((mouse.x - pp.x)*(mouse.x - pp.x) + (mouse.y - pp.y)*(mouse.y - pp.y));
                    if (dist < 10.0f) clicked_on_path = true;
                }
            }
            if (!clicked_on_path) g_selected_path_index = -1;
        }
    }

    // ========== 触摸拖动绘制路径（正交模式：水平/垂直 + 直角转角） ==========
    if (g_path_edit_mode == 1) {
        // 辅助：获取鼠标对应的世界坐标（不吸附——移除网格吸附功能）
        // 用户点击的精确位置即是路径点，不再自动吸附到网格交叉点
        auto GetMouseWorldWithSnap = [&](const ImVec2& mpos) -> Vector3A {
            float u = (mpos.x - map_pos.x) / map_w;
            float v = (mpos.y - map_pos.y) / map_h;
            float worldX = (u - cfg.offsetU) / cfg.scaleX;
            float worldY = (v - cfg.offsetV) / cfg.scaleY;
            return Vector3A(worldX, worldY, Z.Z);
        };

        // 辅助：将正交约束应用到目标点（相对于参考点强制水平/垂直）
        auto ApplyOrthoConstraint = [&](const Vector3A& target, const Vector3A& ref) -> Vector3A {
            if (!g_ortho_draw) return target;
            float dx = fabsf(target.X - ref.X);
            float dy = fabsf(target.Y - ref.Y);
            Vector3A constrained = target;
            // 偏置：水平方向乘1.5，让水平拖动更容易触发水平线段
            if (dx * 1.5f > dy)
                constrained.Y = ref.Y;  // 水平线段：Y锁定
            else
                constrained.X = ref.X;  // 垂直线段：X锁定
            return constrained;
        };

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && hover_map) {
            if (g_press_timer == 0) g_press_pos = mouse;
            g_press_timer += ImGui::GetIO().DeltaTime;

            // 长按0.3秒开始绘制
            if (g_press_timer > 0.3f && !g_path_drawing_active) {
                g_path_drawing_active = true;
                g_current_drawing_path.clear();
                g_path_start_pos = mouse;
                Vector3A firstPt = GetMouseWorldWithSnap(g_press_pos);
                g_current_drawing_path.push_back(firstPt);
            }

            if (g_path_drawing_active) {
                Vector3A rawPt = GetMouseWorldWithSnap(mouse);
                Vector3A lastPt = g_current_drawing_path.back();
                Vector3A constrained;

                if (g_current_drawing_path.size() == 1) {
                    // 只有起点，根据移动方向确定先走水平还是垂直
                    constrained = ApplyOrthoConstraint(rawPt, lastPt);
                } else {
                    // 每段独立：始终根据鼠标相对于上一点的方向决定横/竖
                    // 这样用户可以自由切换方向，不被上一段锁定
                    if (g_ortho_draw) {
                        constrained = ApplyOrthoConstraint(rawPt, lastPt);
                    } else {
                        constrained = rawPt;
                    }
                }

                float dist = sqrtf((constrained.X - lastPt.X)*(constrained.X - lastPt.X) +
                                   (constrained.Y - lastPt.Y)*(constrained.Y - lastPt.Y));
                if (dist > g_path_draw_threshold) {
                    // 正交模式下检测方向变化，插入直角拐角点
                    if (g_ortho_draw && g_current_drawing_path.size() >= 2) {
                        Vector3A prevPrevPt = g_current_drawing_path[g_current_drawing_path.size() - 2];
                        bool oldHorizontal = (fabsf(lastPt.Y - prevPrevPt.Y) < 0.01f);
                        bool newHorizontal = (fabsf(constrained.Y - lastPt.Y) < 0.01f);
                        if (oldHorizontal != newHorizontal) {
                            // 方向变化 → 在转折处插入直角拐角
                            Vector3A corner;
                            if (oldHorizontal) {
                                // 旧段水平、新段垂直 → 拐角在 (constrained.X, lastPt.Y)
                                corner = {constrained.X, lastPt.Y, lastPt.Z};
                            } else {
                                // 旧段垂直、新段水平 → 拐角在 (lastPt.X, constrained.Y)
                                corner = {lastPt.X, constrained.Y, lastPt.Z};
                            }
                            g_current_drawing_path.push_back(corner);
                        }
                    }
                    g_current_drawing_path.push_back(constrained);
                    g_path_start_pos = mouse;
                }
            }
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (g_path_drawing_active && !g_current_drawing_path.empty()) {
                // 使用RDP简化（保留端点，仅去除共线中间点）
                std::vector<ImVec2> screenPts;
                for (auto& wp : g_current_drawing_path)
                    screenPts.push_back(ToMap(wp));

                float tolerance = g_smooth_strength;
                auto simplifiedScreen = SimplifyPathRDP(screenPts, tolerance);
                std::vector<Vector3A> simplifiedWorld;
                for (auto& sp : simplifiedScreen) {
                    float u = (sp.x - map_pos.x) / map_w;
                    float v = (sp.y - map_pos.y) / map_h;
                    float worldX = (u - cfg.offsetU) / cfg.scaleX;
                    float worldY = (v - cfg.offsetV) / cfg.scaleY;
                    simplifiedWorld.push_back(Vector3A(worldX, worldY, Z.Z));
                }
                g_current_drawing_path = simplifiedWorld;

                if (g_current_drawing_path.size() >= 2) {
                    g_saved_paths.push_back(g_current_drawing_path);
                    SavePlayerPathsToJSON(g_current_map_index, g_current_floor_index);
                    g_pathGraph.dirty = true;
                    AddNotification("路径已保存 ✓", 2.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                } else {
                    AddNotification("路径太短，已丢弃", 2.0f, ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
                }
                g_current_drawing_path.clear();
                g_path_edit_mode = 0;
            }
            g_path_drawing_active = false;
            g_press_timer = 0;
        }
    }

    // ========== 地图拖动 ==========
    static bool dragging_map = false;
    static ImVec2 drag_offset;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hover_map && g_drag_point == 0 && g_path_edit_mode == 0) {
        dragging_map = true;
        drag_offset = ImVec2(mouse.x - map_pos.x, mouse.y - map_pos.y);
    }
    if (dragging_map && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        g_map_pos_x = mouse.x - drag_offset.x;
        g_map_pos_y = mouse.y - drag_offset.y;
        // 地图拖动边界限制
        g_map_pos_x = std::clamp(g_map_pos_x, -map_w * 0.5f, displayInfo.width - map_w * 0.5f);
        g_map_pos_y = std::clamp(g_map_pos_y, -map_h * 0.5f, displayInfo.height - map_h * 0.5f);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) dragging_map = false;

    // ========== 双指缩放 + 鼠标滚轮缩放（仅地图悬停区域生效）==========
    {
        static float g_pinch_last_dist = 0.0f;
        static bool  g_pinch_active  = false;

        auto ClampMapPos = [&]() {
            g_map_pos_x = std::clamp(g_map_pos_x, -map_w * 0.5f, displayInfo.width - map_w * 0.5f);
            g_map_pos_y = std::clamp(g_map_pos_y, -map_h * 0.5f, displayInfo.height - map_h * 0.5f);
        };

        if (hover_map) {
            int fc = Touch::GetFingerCount();
            if (fc >= 2) {
                float x1, y1, x2, y2;
                if (Touch::GetFinger(0, x1, y1) && Touch::GetFinger(1, x2, y2)) {
                    float dx = x1 - x2, dy = y1 - y2;
                    float dist = sqrtf(dx*dx + dy*dy);
                    if (!g_pinch_active) {
                        g_pinch_last_dist = dist;
                        g_pinch_active = true;
                    } else if (g_pinch_last_dist > 1.0f) {
                        float scale = dist / g_pinch_last_dist;
                        float newSz = g_map_display_size * scale;
                        newSz = std::clamp(newSz, 100.0f, 2500.0f);
                        float ratio = newSz / g_map_display_size;
                        float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;
                        g_map_pos_x = cx - (cx - g_map_pos_x) * ratio;
                        g_map_pos_y = cy - (cy - g_map_pos_y) * ratio;
                        g_map_display_size = newSz;
                        g_pinch_last_dist = dist;
                        ClampMapPos();
                    }
                }
            } else {
                g_pinch_active = false;
            }

            if (hover_map && ImGui::GetIO().MouseWheel != 0.0f) {
                float oldSz = g_map_display_size;
                float newSz = g_map_display_size * (1.0f + ImGui::GetIO().MouseWheel * 0.1f);
                newSz = std::clamp(newSz, 100.0f, 2500.0f);
                float ratio = newSz / oldSz;
                ImVec2 ms = ImGui::GetMousePos();
                g_map_pos_x = ms.x - (ms.x - g_map_pos_x) * ratio;
                g_map_pos_y = ms.y - (ms.y - g_map_pos_y) * ratio;
                g_map_display_size = newSz;
                ClampMapPos();
                ImGui::GetIO().MouseWheel = 0.0f;
            }
        } else {
            g_pinch_active = false;
        }
    }

    // ========== 缩放按钮 ==========
    ImVec2 zoom_btn_pos(map_end.x - 35.0f, map_pos.y + 10.0f);
    ImVec2 zoom_btn_size(35.0f, 35.0f);
    ImU32 zoom_btn_color = IM_COL32(255, 255, 255, 80);
    ImU32 zoom_text_color = IM_COL32(255, 255, 255, 200);

    Draw->AddRectFilled(zoom_btn_pos, ImVec2(zoom_btn_pos.x + zoom_btn_size.x, zoom_btn_pos.y + zoom_btn_size.y), zoom_btn_color, 5.0f);
    Draw->AddText(ImVec2(zoom_btn_pos.x + 8, zoom_btn_pos.y + 2), zoom_text_color, "+");
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        ImGui::GetMousePos().x >= zoom_btn_pos.x && ImGui::GetMousePos().x <= zoom_btn_pos.x + zoom_btn_size.x &&
        ImGui::GetMousePos().y >= zoom_btn_pos.y && ImGui::GetMousePos().y <= zoom_btn_pos.y + zoom_btn_size.y) {
        g_map_display_size = std::min(g_map_display_size + 20.0f, 1800.0f);
    }

    ImVec2 zoom_btn2_pos(map_end.x - 35.0f, map_pos.y + 55.0f);
    Draw->AddRectFilled(zoom_btn2_pos, ImVec2(zoom_btn2_pos.x + zoom_btn_size.x, zoom_btn2_pos.y + zoom_btn_size.y), zoom_btn_color, 5.0f);
    Draw->AddText(ImVec2(zoom_btn2_pos.x + 11, zoom_btn2_pos.y + 2), zoom_text_color, "-");
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        ImGui::GetMousePos().x >= zoom_btn2_pos.x && ImGui::GetMousePos().x <= zoom_btn2_pos.x + zoom_btn_size.x &&
        ImGui::GetMousePos().y >= zoom_btn2_pos.y && ImGui::GetMousePos().y <= zoom_btn2_pos.y + zoom_btn_size.y) {
        g_map_display_size = std::max(g_map_display_size - 20.0f, 100.0f);
    }
}

void ProcessObjectWithFullDetails(ImDrawList *Draw, const DataStruct &item,
                                  const Vector3A &raw, float r_x, float r_y,
                                  float W, float H, int distance,
                                  const std::vector<RoleInfo>& validRoles,
                                  const std::unordered_map<std::string, int>& boundSeats) {
    if (Debugging && distance <= 30) {
        char l1[256], l2[256];
        std::snprintf(l1, sizeof(l1), "[%d m] 阵:%d 动:%d 码:%x 值:%.1f", distance,
                      item.阵营, item.action, item.实体特征码, item.状态数值);
        std::snprintf(l2, sizeof(l2), "Ptr:%lx 类名:%s", item.obj, item.类名);
        auto s1 = ImGui::CalcTextSize(l1);
        auto s2 = ImGui::CalcTextSize(l2);
        Draw->AddText({r_x - s1.x * 0.5f, r_y - s1.y - 42.0f}, ImColor(255, 255, 255), l1);
        Draw->AddText({r_x - s2.x * 0.5f, r_y - s2.y - 22.0f}, ImColor(255, 255, 255), l2);
    }

    auto optimizedDrawText = [&](const char *label, ImColor color, float offsetY = 0) {
        const auto sz = ImGui::CalcTextSize(label, nullptr, 20);
        const float txtX = SnapToPixel(r_x - sz.x * 0.5f);
        const float txtY = SnapToPixel(r_y + offsetY);
        Draw->AddText({txtX, txtY}, color, label);
    };

    if (item.阵营 == 3) {
        switch (item.sub_type) {
            case ObjSubClass::Trap:
                if (show_draw_QY && item.实体特征码 != 0) optimizedDrawText("[陷阱]", ImColor(255, 50, 50));
                break;
            case ObjSubClass::CipherMachine:
                if (show_draw_sender && distance <= 100) {
                    optimizedDrawText("[密码机]", 密码机色);
                    if (distance >= 0) {
                        char distText[32];
                        std::snprintf(distText, sizeof(distText), "%d m", distance);
                        optimizedDrawText(distText, 密码机色, ImGui::GetTextLineHeight());
                    }
                    float rawVal = item.状态数值;
                    if (rawVal >= 0.0f && rawVal <= 100.0f) {
                        char progressText[16];
                        if (rawVal <= 1.0f) {
                            int percent = static_cast<int>(rawVal * 100.0f + 0.5f);
                            if (percent >= 0) {
                                std::snprintf(progressText, sizeof(progressText), "%d%%", percent);
                                optimizedDrawText(progressText, 密码机色, ImGui::GetTextLineHeight() * 2);
                            }
                        } else {
                            std::snprintf(progressText, sizeof(progressText), "%.0f%%", rawVal);
                            optimizedDrawText(progressText, 密码机色, ImGui::GetTextLineHeight() * 2);
                        }
                    }
                    if (Debugging) {
                        char debugInfo[64];
                        std::snprintf(debugInfo, sizeof(debugInfo), "raw=%.3f code=%x", rawVal, item.实体特征码);
                        optimizedDrawText(debugInfo, ImColor(200, 200, 200), ImGui::GetTextLineHeight() * 3);
                    }
                }
                break;
            case ObjSubClass::Clip:
                if (show_draw_QY && item.实体特征码 != 0) optimizedDrawText("[夹子]", ImColor(255, 50, 50));
                break;
            case ObjSubClass::Cat:
                if (distance < 38 && show_draw_Animal) optimizedDrawText("哈基米", ImColor(255, 50, 50));
                break;
            case ObjSubClass::Lion:
                if (distance < 38 && show_draw_Animal) optimizedDrawText("狮子", ImColor(255, 50, 50));
                break;
            case ObjSubClass::Cellar:
                if (show_draw_Cellar) {
                    if (distance >= 0) {
                        char cellarText[32];
                        std::snprintf(cellarText, sizeof(cellarText), "[地窖] %d m", distance);
                        optimizedDrawText(cellarText, 地窖色);
                    } else {
                        optimizedDrawText("[地窖]", 地窖色);
                    }
                }
                break;
            case ObjSubClass::Box:
                if (show_draw_BoxItem && distance <= g_box_dist) {
                    if (distance >= 0) {
                        char boxText[32];
                        std::snprintf(boxText, sizeof(boxText), "[道具箱] %d m", distance);
                        optimizedDrawText(boxText, 箱子色);
                    } else {
                        optimizedDrawText("[道具箱]", 箱子色);
                    }
                }
                break;
            case ObjSubClass::Chair:
                if (show_draw_Chair && distance <= g_chair_dist) {
                    if (distance >= 0) {
                        char chairText[32];
                        std::snprintf(chairText, sizeof(chairText), "[椅子] %d m", distance);
                        optimizedDrawText(chairText, 椅子色);
                    } else {
                        optimizedDrawText("[椅子]", 椅子色);
                    }
                }
                break;
            case ObjSubClass::Pallet:
                if (show_draw_BANZI && distance <= g_board_dist) {
                    if (distance >= 0) {
                        char banziText[32];
                        std::snprintf(banziText, sizeof(banziText), "[板子] %d m", distance);
                        optimizedDrawText(banziText, 板子色);
                    } else {
                        optimizedDrawText("[板子]", 板子色);
                    }
                }
                break;
            default:
                break;
        }
    } else if (item.阵营 == 6 && MjSubsystem::draw_props) {
        const char* display_name = item.prop_name[0] != '\0' ? item.prop_name : item.类名;
        ImColor prop_color = 白色;
        bool should_draw = true;

        if (std::strcmp(item.prop_name, "[紫宝箱]") == 0) {
            prop_color = ImColor(255, 0, 255);
            if (!MjSubsystem::show_big_chest) should_draw = false;
        } else if (std::strcmp(item.prop_name, "[小箱子]") == 0) {
            prop_color = ImColor(255, 165, 0);
            if (!MjSubsystem::show_small_chest) should_draw = false;
        } else if (std::strcmp(item.prop_name, "[花瓶]") == 0) {
            prop_color = 白色;
            if (!MjSubsystem::show_interactables) should_draw = false;
        } else if (std::strcmp(item.prop_name, "[隐藏宝箱]") == 0) {
            prop_color = ImColor(0, 128, 255, 255);
            if (!MjSubsystem::show_big_chest) should_draw = false;
        } else if (std::strstr(item.类名, "monster")) {
            prop_color = ImColor(255, 0, 0);
            if (!MjSubsystem::show_monsters) should_draw = false;
        } else if (std::strcmp(item.prop_name, "[板]") == 0) {
            prop_color = ImColor(139, 69, 19);
            if (!MjSubsystem::show_interactables) should_draw = false;
        } else if (std::strcmp(item.prop_name, "[穿梭门]") == 0) {
            prop_color = ImColor(0, 0, 255);
            if (!MjSubsystem::show_interactables) should_draw = false;
        } else if (std::strcmp(item.prop_name, "[隐藏开关门]") == 0) {
            prop_color = ImColor(0, 255, 255, 255);
            if (!MjSubsystem::show_interactables) should_draw = false;
        } else if (std::strcmp(item.prop_name, "[陷阱]") == 0 ||
                   std::strcmp(item.prop_name, "[夹子]") == 0 ||
                   std::strcmp(item.prop_name, "[碎石]") == 0) {
            prop_color = ImColor(255, 50, 50);
            if (!MjSubsystem::show_traps) should_draw = false;
        } else if (std::strcmp(item.prop_name, "[钢琴]") == 0 ||
                   std::strcmp(item.prop_name, "[凳子]") == 0 ||
                   std::strcmp(item.prop_name, "[破裂木板 2000]") == 0 ||
                   std::strcmp(item.prop_name, "[门]") == 0) {
            prop_color = 白色;
            if (!MjSubsystem::show_interactables) should_draw = false;
        } else {
            int price = ExtractPrice(item.prop_name);
            if (price >= 0) {
                if (price < 1000)
                    prop_color = ImColor(100, 100, 100, 255);
                else if (price <= 2000)
                    prop_color = ImColor(180, 150, 120, 255);
                else if (price < MjSubsystem::high_value_threshold)
                    prop_color = ImColor(100, 180, 210, 255);
                else if (price < 10000)
                    prop_color = ImColor(255, 140, 0, 255);
                else if (price < 50000)
                    prop_color = ImColor(255, 0, 255, 255);
                else if (price < 100000)
                    prop_color = ImColor(255, 215, 0, 255);
                else if (price < 200000)
                    prop_color = ImColor(255, 255, 0, 255);
                else if (price < 400000)
                    prop_color = ImColor(0, 255, 0, 255);
                else if (price < 1000000)
                    prop_color = ImColor(0, 128, 255, 255);
                else {
                    float t = ImGui::GetTime();
                    float brightness = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t * 3.0f));
                    prop_color = ImColor(
                            static_cast<int>(255 * brightness),
                            static_cast<int>(215 * brightness),
                            0, 255
                    );
                }

                if (price >= MjSubsystem::high_value_threshold) {
                    if (!MjSubsystem::show_high_value) should_draw = false;
                } else {
                    if (!MjSubsystem::show_low_value)  should_draw = false;
                }
            } else {
                if (!MjSubsystem::show_interactables) should_draw = false;
            }
        }

        if (should_draw) {
            if (std::strstr(item.类名, "monster")) {
                if (distance > MjSubsystem::max_dist_monsters) should_draw = false;
            }
            else if (std::strcmp(item.prop_name, "[紫宝箱]") == 0 ||
                     std::strcmp(item.prop_name, "[金宝箱]") == 0 ||
                     std::strcmp(item.prop_name, "[隐藏宝箱]") == 0) {
                if (distance > MjSubsystem::max_dist_big_chest) should_draw = false;
            }
            else if (std::strcmp(item.prop_name, "[小箱子]") == 0) {
                if (distance > MjSubsystem::max_dist_small_chest) should_draw = false;
            }
            else if (std::strcmp(item.prop_name, "[陷阱]") == 0 ||
                     std::strcmp(item.prop_name, "[夹子]") == 0 ||
                     std::strcmp(item.prop_name, "[碎石]") == 0) {
                if (distance > MjSubsystem::max_dist_traps) should_draw = false;
            }
            else if (std::strcmp(item.prop_name, "[花瓶]") == 0 ||
                     std::strcmp(item.prop_name, "[板]") == 0 ||
                     std::strcmp(item.prop_name, "[穿梭门]") == 0 ||
                     std::strcmp(item.prop_name, "[隐藏开关门]") == 0 ||
                     std::strcmp(item.prop_name, "[钢琴]") == 0 ||
                     std::strcmp(item.prop_name, "[凳子]") == 0 ||
                     std::strcmp(item.prop_name, "[门]") == 0) {
                if (distance > MjSubsystem::max_dist_interactables) should_draw = false;
            }
            else {
                int price = ExtractPrice(item.prop_name);
                if (price >= MjSubsystem::high_value_threshold) {
                    if (distance > MjSubsystem::max_dist_high_value) should_draw = false;
                } else {
                    if (distance > MjSubsystem::max_dist_low_value) should_draw = false;
                }
            }
        }

        if (should_draw) {
            if (std::strstr(item.类名, "monster")) {
                ImColor frame_color = prop_color;
                if (std::strcmp(item.prop_name, "[鹿头]") == 0 ||
                    std::strcmp(item.prop_name, "[鹿头 Pro Max.]") == 0) {
                    frame_color = ImColor(0, 0, 0, 255);
                }
                float x1 = r_x - W * 0.5f;
                float y1 = r_y - H * 0.5f;
                float x2 = x1 + W;
                float y2 = y1 + H;
                DrawEnhancedFrame(Draw, x1, y1, x2, y2, frame_color, distance);
            }
            optimizedDrawText(display_name, prop_color);
            if (MjSubsystem::show_distance && distance >= 0) {
                char dist_buf[32];
                std::snprintf(dist_buf, sizeof(dist_buf), "%d m", distance);
                optimizedDrawText(dist_buf, ImColor(235, 235, 235, 255), ImGui::GetTextLineHeight());
            }
        }
    }

    if (item.阵营 == 1 || item.阵营 == 2) {
        if (GlobalMemory::自身 == item.obj) return;
        bool mimic_active = !validRoles.empty();
        char s_txt[32] = "", i_txt[32] = "", n_txt[128] = "";
        ImColor identity_clr = 白色;
        ImColor f_clr = ((item.阵营 == 1) ? 亮红色 : 绿色);

        if (mimic_active && item.is_ghost) strcpy(s_txt, "[死亡]");

        strcpy(n_txt, item.str);

        if (GlobalMemory::自身 && item.阵营 == 1 &&
            std::strstr(item.类名, "redqueen.gim") && item.obj != GlobalMemory::自身) {
            strcpy(n_txt, "夫人假身(镜像)");
            f_clr = ImColor(255, 100, 255, 255);
        }

        int seat = -1;
        if (mimic_active && is_meeting_detected) {
            auto it = g_meeting_seat_map.find(item.obj);
            if (it != g_meeting_seat_map.end()) seat = it->second;
        }
        if (seat == -1) {
            auto it = boundSeats.find(std::string(item.类名));
            if (it != boundSeats.end()) seat = it->second;
        }

        if (mimic_active && seat != -1) {
            for (const auto &r : validRoles) {
                if (r.index == seat - 1) {
                    sprintf(i_txt, "[%02d号] ", seat);
                    if (r.campId == 1) identity_clr = ImColor(50, 204, 204, 255);
                    else if (r.campId == 2) identity_clr = ImColor(255, 50, 50, 255);
                    else if (r.campId == 3) identity_clr = ImColor(255, 204, 50, 255);
                    strcpy(n_txt, RoleIdToChinese(r.roleId).c_str());
                    f_clr = identity_clr;
                    break;
                }
            }
        }

        if (show_draw_Name) {
            float bottom_tw = ImGui::CalcTextSize(i_txt).x + ImGui::CalcTextSize(n_txt).x;
            float bottom_cx = X1 + W * 0.5f - bottom_tw * 0.5f;
            if (s_txt[0]) {
                float top_tw = ImGui::CalcTextSize(s_txt).x;
                float top_cx = X1 + W * 0.5f - top_tw * 0.5f;
                Draw->AddText({top_cx, Y1 - 55}, ImColor(255, 255, 255, 255), s_txt);
            }
            if (i_txt[0]) {
                Draw->AddText({bottom_cx, Y1 - 35}, 白色, i_txt);
                bottom_cx += ImGui::CalcTextSize(i_txt).x;
            }
            Draw->AddText({bottom_cx, Y1 - 35}, identity_clr, n_txt);
        }

        ImColor frame_color = g_BoxColor_Survivor;
        bool mimic_colored = false;
        if (mimic_active && seat != -1) {
            for (const auto &r : validRoles) {
                if (r.index == seat - 1) {
                    if (r.campId == 1) frame_color = ImColor(50, 204, 204, 255);
                    else if (r.campId == 2) frame_color = ImColor(255, 50, 50, 255);
                    else if (r.campId == 3) frame_color = ImColor(255, 204, 50, 255);
                    mimic_colored = true;
                    break;
                }
            }
        }
        if (!mimic_colored) {
            if (item.is_ghost) frame_color = g_BoxColor_Ghost;
            else if (item.阵营 == 1) frame_color = g_BoxColor_Hunter;
            else frame_color = g_BoxColor_Survivor;
        }

        if (show_draw_EnhancedFrame) {
            DrawEnhancedFrame(Draw, X1, Y1, X2, Y2, frame_color, distance);
            if (item.is_ghost && show_draw_MarktheSoul) {
                float s = std::clamp(15.0f - (distance / 20.0f), 5.0f, 15.0f);
                s = std::min(s, W * 0.3f);
                Draw->AddTriangle(
                        {X1 + W * 0.5f, Y1 + H * 0.5f - s},
                        {X1 + W * 0.5f - s * 0.866f, Y1 + H * 0.5f + s * 0.5f},
                        {X1 + W * 0.5f + s * 0.866f, Y1 + H * 0.5f + s * 0.5f}, 白色, 2.0f);
            } else if (!item.is_ghost) {
                DrawTriangle(Draw, X1 + W * 0.5f, Y1 + H * 0.5f, frame_color, distance);
            }
        }
        if (show_draw_Distance && distance >= 0) {
            char d_t[32];
            sprintf(d_t, "%d m", distance);
            auto sz = ImGui::CalcTextSize(d_t);
            Draw->AddText({X1 + W * 0.5f - sz.x * 0.5f, Y2 + 2}, ImColor(235, 235, 235, 255), d_t);
        }
        if (show_draw_Line) {
            Draw->AddLine({px, 160.0f}, {X1 + W * 0.5f, Y1}, frame_color, 2);
        }
    }
}

void Draw_Main_Optimized(ImDrawList *Draw) {
    uintptr_t Step1_Addr = GlobalMemory::libbase + GlobalMemory::MatrixOffset;
    uintptr_t Ptr1 = getPtr64(Step1_Addr);
    if (!Ptr1) return;
    uintptr_t Step2_Addr = Ptr1 + 0xA58;
    uintptr_t Ptr2 = getPtr64(Step2_Addr);
    if (!Ptr2) return;
    GlobalMemory::Matrix = Ptr2 + 0x2C0;
    memset(matrix, 0, sizeof(matrix));
    vm_readv(GlobalMemory::Matrix, matrix, 16 * sizeof(float));
    if (std::abs(matrix[0]) < 0.0001f && std::abs(matrix[1]) < 0.0001f) return;

    if (show_draw_prophet && 监管者预知[0] != '\0') {
        const auto textSize = ImGui::CalcTextSize(监管者预知, nullptr, 25);
        Draw->AddText({px - textSize.x * 0.5f, 130}, 红色, 监管者预知);
    }

    const auto &current_data = data_buffers[front_buffer_idx.load(std::memory_order_acquire)];
    const int maxDrawCount = static_cast<int>(current_data.size());

    std::vector<RoleInfo> local_validRoles;
    std::unordered_map<std::string, int> local_bound_seats;
    {
        std::lock_guard<std::mutex> lock(mimic_mutex);
        local_validRoles = global_validRoles;
        local_bound_seats = bound_seat_by_class;
    }
    bool has_valid_roles = !local_validRoles.empty();

    struct MeetingConfig {
        int seats;
        float radius;
        float step_deg;
        float ox[12];
        float oy[12];
    };
    static bool config_init = false;
    static MeetingConfig configs[2] = {{12, 30.0f, 30.0f}, {10, 26.0f, 36.0f}};
    if (!config_init) {
        for (auto &cfg : configs) {
            for (int i = 0; i < cfg.seats; ++i) {
                float theta = i * cfg.step_deg * 3.14159265f / 180.0f;
                cfg.ox[i] = cfg.radius * std::sin(theta);
                cfg.oy[i] = cfg.radius * std::cos(theta);
            }
        }
        config_init = true;
    }

    int alive_players = 0;
    for (int i = 0; i < maxDrawCount; ++i) {
        if (current_data[i].阵营 == 1 || current_data[i].阵营 == 2) {
            if (!current_data[i].is_ghost) alive_players++;
        }
    }

    if (g_MimicModeEnabled && !has_valid_roles && alive_players >= 4 && !is_scanning_mimic.load()) {
        is_scanning_mimic.store(true);
        std::thread([]() {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(7, &cpuset);
            sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
            scan_mimic_roles();
            is_scanning_mimic.store(false);
        }).detach();
    }

    if (alive_players == 0 && has_valid_roles) {
        std::lock_guard<std::mutex> lock(mimic_mutex);
        global_validRoles.clear();
        bound_seat_by_class.clear();
        local_validRoles.clear();
        local_bound_seats.clear();
        has_valid_roles = false;
    }

    is_meeting_detected = false;
    if (has_valid_roles) {
        std::vector<Vector3A> players;
        for (int i = 0; i < maxDrawCount; ++i) {
            if (current_data[i].阵营 == 1 || current_data[i].阵营 == 2) {
                Vector3A pos = getObjectCoordinates(current_data[i].objcoor, false);
                if (isValidCoordinate(pos) && pos.Z >= Global_Filter_Min_Z && pos.Z <= Global_Filter_Max_Z)
                    players.push_back(pos);
            }
        }

        int best_votes = 0;
        float best_cx = 0.0f, best_cy = 0.0f;
        int best_seats = 12;
        float best_radius = 30.0f;

        for (const auto &cfg : configs) {
            for (const auto &p : players) {
                for (int seat = 1; seat <= cfg.seats; ++seat) {
                    float cx = p.X - cfg.ox[seat - 1];
                    float cy = p.Y - cfg.oy[seat - 1];
                    int votes = 0;
                    for (const auto &other_p : players) {
                        for (int o_seat = 1; o_seat <= cfg.seats; ++o_seat) {
                            float ex = cx + cfg.ox[o_seat - 1];
                            float ey = cy + cfg.oy[o_seat - 1];
                            float dx = other_p.X - ex;
                            float dy = other_p.Y - ey;
                            if (dx * dx + dy * dy < 16.0f) {
                                votes++;
                                break;
                            }
                        }
                    }
                    if (votes > best_votes) {
                        best_votes = votes;
                        best_cx = cx;
                        best_cy = cy;
                        best_seats = cfg.seats;
                        best_radius = cfg.radius;
                    }
                }
            }
        }

        if (best_votes >= 3) {
            is_meeting_detected = true;
            meeting_center_x = best_cx;
            meeting_center_y = best_cy;
            meeting_total_seats = best_seats;
            meeting_radius = best_radius;
        }
    }

    g_meeting_seat_map.clear();
    if (has_valid_roles && is_meeting_detected) {
        for (int i = 0; i < maxDrawCount; ++i) {
            const auto &item = current_data[i];
            if (item.阵营 != 1 && item.阵营 != 2) continue;
            Vector3A pos = getObjectCoordinates(item.objcoor, false);
            if (!isValidCoordinate(pos) || pos.Z < Global_Filter_Min_Z || pos.Z > Global_Filter_Max_Z) continue;
            int seat = -1;
            for (int s = 1; s <= meeting_total_seats; ++s) {
                float th = (s - 1) * (360.0f / meeting_total_seats) * 3.14159265f / 180.0f;
                float ex = meeting_center_x + meeting_radius * std::sin(th);
                float ey = meeting_center_y + meeting_radius * std::cos(th);
                float dx = pos.X - ex;
                float dy = pos.Y - ey;
                if (dx * dx + dy * dy < 9.0f) {
                    seat = s;
                    break;
                }
            }
            if (seat != -1) {
                g_meeting_seat_map[item.obj] = seat;
                {
                    std::lock_guard<std::mutex> lock(mimic_mutex);
                    bound_seat_by_class[std::string(item.类名)] = seat;
                }
                local_bound_seats[std::string(item.类名)] = seat;
            }
        }
    }

    bool found_self = false;
    float min_center_dist = 9999.0f;
    int best_candidate_index = -1;
    bool is_mj_active = MjSubsystem::ShouldBypassFilter();

    for (int i = 0; i < maxDrawCount; ++i) {
        const auto &item = current_data[i];
        if (item.阵营 != 1 && item.阵营 != 2) continue;
        Vector3A pos = getObjectCoordinates(item.objcoor, false);
        if (!isValidCoordinate(pos) || pos.Z < Global_Filter_Min_Z || pos.Z > Global_Filter_Max_Z) continue;
        float cam_z = matrix[3] * pos.X + matrix[7] * pos.Z + matrix[11] * pos.Y + matrix[15];

        if (!is_mj_active) {
            if (cam_z < 5.0f || cam_z > 80.0f) continue;
        } else {
            if (cam_z < 1.0f || cam_z > 120.0f) continue;
        }

        if (!is_mj_active) {
            int zy = 0;
            vm_readv(item.obj + 0xAA, &zy, 1);
            if (!(zy & 1)) continue;
        }

        float rx_calc = px + (matrix[0] * pos.X + matrix[4] * pos.Z + matrix[8] * pos.Y + matrix[12]) / cam_z * px;
        float dist_from_center = std::fabs(rx_calc - px);
        if (dist_from_center < min_center_dist && dist_from_center < 150.0f) {
            min_center_dist = dist_from_center;
            best_candidate_index = i;
        }
    }

    if (best_candidate_index != -1) {
        GlobalMemory::自身 = current_data[best_candidate_index].obj;
        Z = getObjectCoordinates(current_data[best_candidate_index].objcoor, false);
        found_self = true;
        g_selfAction.store(current_data[best_candidate_index].action);
    } else {
        if (Z.X == 0.0f) {
            for (int i = 0; i < maxDrawCount; ++i) {
                if (current_data[i].阵营 == 2) {
                    Vector3A pos = getObjectCoordinates(current_data[i].objcoor, false);
                    if (isValidCoordinate(pos) && pos.Z >= Global_Filter_Min_Z && pos.Z <= Global_Filter_Max_Z) {
                        Z = pos;
                        break;
                    }
                }
            }
        }
    }

    if (!found_self && MjSubsystem::ShouldBypassFilter()) {
        float min_screen_dist = 999999.0f;
        for (int i = 0; i < maxDrawCount; ++i) {
            const auto &item = current_data[i];
            if (item.阵营 != 2) continue;
            Vector3A pos = getObjectCoordinates(item.objcoor, false);
            if (!isValidCoordinate(pos)) continue;

            float cam_z = matrix[3] * pos.X + matrix[7] * pos.Z + matrix[11] * pos.Y + matrix[15];
            if (cam_z < 0.1f) continue;

            float rx = px + (matrix[0] * pos.X + matrix[4] * pos.Z + matrix[8] * pos.Y + matrix[12]) / cam_z * px;
            float ry = py - (matrix[1] * pos.X + matrix[5] * (pos.Z + 8.5f) + matrix[9] * pos.Y + matrix[13]) / cam_z * py;
            float screen_dist = (rx - px)*(rx - px) + (ry - py)*(ry - py);

            if (screen_dist < min_screen_dist) {
                min_screen_dist = screen_dist;
                Z = pos;
                found_self = true;
                GlobalMemory::自身 = item.obj;
                g_selfAction.store(item.action);
            }
        }
    }

    g_mirrorList.clear();
    if (show_draw_redqueen && found_self) {
        Vector3A mirrorCenter;
        Vector3A mirrorNormal;
        bool hasMirror = false;
        bool isHolding = false;

        for (int i = 0; i < maxDrawCount; ++i) {
            const auto &item = current_data[i];
            if (item.阵营 != 5) continue;
            Vector3A pos = getObjectCoordinates(item.objcoor, false);
            if (!isValidCoordinate(pos)) continue;

            if (std::strstr(item.类名, "redqueen_mirror")) {
                mirrorCenter = pos;
                float yaw_cos = getFloat(item.objcoor + 0xB8);
                float yaw_sin = getFloat(item.objcoor + 0xC0);
                mirrorNormal.X = -yaw_sin;
                mirrorNormal.Y = yaw_cos;
                hasMirror = true;
            } else if (std::strstr(item.类名, "mirror_model")) {
                isHolding = true;
                mirrorCenter = pos;
                mirrorNormal.X = pos.X - Z.X;
                mirrorNormal.Y = pos.Y - Z.Y;
                hasMirror = true;
            }
        }

        g_holdMirror = isHolding;
        if (hasMirror) {
            g_mirrorCenter = mirrorCenter;
            g_mirrorNormal = mirrorNormal;

            for (int i = 0; i < maxDrawCount; ++i) {
                const auto &item = current_data[i];
                if (item.阵营 != 2) continue;
                Vector3A survivorPos = getObjectCoordinates(item.objcoor, false);
                if (!isValidCoordinate(survivorPos)) continue;

                Vector3A mirrorPt = FastMath::CalculateSurvivorMirrorPos(
                        survivorPos, mirrorCenter, mirrorNormal);
                MirrorInfo mi;
                mi.survivorPos = survivorPos;
                mi.mirrorPos = mirrorPt;
                std::strncpy(mi.name, item.str, sizeof(mi.name)-1);
                g_mirrorList.push_back(mi);
            }
        }
    }

    for (int i = 0; i < maxDrawCount; ++i) {
        const auto &item = current_data[i];
        const uintptr_t coorBase = item.objcoor;
        if (!coorBase) continue;
        bool isProp = (item.阵营 == 4 || item.阵营 == 6);
        Vector3A raw = getObjectCoordinates(coorBase, isProp);
        if (!isValidCoordinate(raw) || raw.Z < Global_Filter_Min_Z || raw.Z > Global_Filter_Max_Z) continue;

        if (show_draw_redqueen && item.阵营 == 5) {
            float r_x, r_y, r_w;
            if (optimizedWorldToScreen(raw, matrix, px, py, r_x, r_y, r_w)) {
                auto sz = ImGui::CalcTextSize(item.str);
                Draw->AddText({SnapToPixel(r_x - sz.x * 0.5f), SnapToPixel(r_y)}, ImColor(0, 255, 255), item.str);
            }
            continue;
        }

        bool self_pos_valid = (Z.X != 0.0f || Z.Y != 0.0f || Z.Z != 0.0f);
        const float maxDist = Global_Filter_Max_Distance * 距离比例;
        int distance;

        if (self_pos_valid) {
            float distSquared = FastMath::fastDistanceSquared(raw, Z);
            if (distSquared > maxDist * maxDist) continue;
            distance = static_cast<int>(FastMath::fastDistance(raw, Z) / 距离比例);
        } else {
            distance = -1;
        }

        float r_x, r_y, r_w;
        if (!optimizedWorldToScreen(raw, matrix, px, py, r_x, r_y, r_w)) continue;
        W = (r_y - r_w) * 0.5f;
        H = r_y - r_w;
        X1 = SnapToPixel(r_x - W * 0.5f);
        Y1 = SnapToPixel(r_y - H * 0.5f);
        X2 = SnapToPixel(X1 + W);
        Y2 = SnapToPixel(Y1 + H);
        if (!isValidScreenPosition(r_x, r_y, W, H)) continue;

        ProcessObjectWithFullDetails(Draw, item, raw, r_x, r_y, W, H, distance,
                                     local_validRoles, local_bound_seats);
    }

    if (show_draw_redqueen && !g_mirrorList.empty()) {
        for (const auto &mi : g_mirrorList) {
            float r_x, r_y, r_w;
            if (!optimizedWorldToScreen(mi.mirrorPos, matrix, px, py, r_x, r_y, r_w)) continue;
            float W_m = (r_y - r_w) * 0.5f;
            float H_m = r_y - r_w;
            float X1_m = SnapToPixel(r_x - W_m * 0.5f);
            float Y1_m = SnapToPixel(r_y - H_m * 0.5f);
            float X2_m = SnapToPixel(X1_m + W_m);
            float Y2_m = SnapToPixel(Y1_m + H_m);
            if (!isValidScreenPosition(r_x, r_y, W_m, H_m)) continue;

            int dist_m = static_cast<int>(FastMath::fastDistance(mi.mirrorPos, Z) / 距离比例);
            ImColor mirror_color = ImColor(0, 255, 255, 255);

            if (show_draw_Name) {
                char m_text[256];
                snprintf(m_text, sizeof(m_text), "[镜像]%s", mi.name);
                auto sz = ImGui::CalcTextSize(m_text);
                Draw->AddText({SnapToPixel(r_x - sz.x * 0.5f), Y1_m - 35}, mirror_color, m_text);
            }
            if (show_draw_EnhancedFrame) {
                DrawEnhancedFrame(Draw, X1_m, Y1_m, X2_m, Y2_m, mirror_color, dist_m);
            }
            if (show_draw_Distance) {
                char d_t[32];
                snprintf(d_t, sizeof(d_t), "%d m", dist_m);
                auto sz = ImGui::CalcTextSize(d_t);
                Draw->AddText({X1_m + W_m * 0.5f - sz.x * 0.5f, Y2_m + 2}, ImColor(235, 235, 235, 255), d_t);
            }
            if (show_draw_Line) {
                Draw->AddLine({px, 160.0f}, {r_x, Y1_m}, mirror_color, 2.0f);
            }
        }
    }

    if (show_touch_point) {
        ImVec2 touch_center = ImVec2(wood_touch_x, wood_touch_y);
        Draw->AddCircleFilled(touch_center, 15.0f, ImColor(0, 255, 0, 100));
        Draw->AddCircle(touch_center, 18.0f, ImColor(0, 255, 0, 200), 0, 3.0f);
        Draw->AddLine(ImVec2(touch_center.x - 25, touch_center.y),
                      ImVec2(touch_center.x + 25, touch_center.y),
                      ImColor(255, 255, 0, 180), 2.0f);
        Draw->AddLine(ImVec2(touch_center.x, touch_center.y - 25),
                      ImVec2(touch_center.x, touch_center.y + 25),
                      ImColor(255, 255, 0, 180), 2.0f);
    }

    // ========== 摸金导航地图 ==========
    if (g_map_enabled) {
        TryAutoDetectMap(current_data);
        Draw_MapOverlay(Draw, current_data);
    }

    // ========== 新地图检测提示 ==========
    if (g_new_map_detected) {
        if (g_new_map_frame_counter > 0) {
            g_new_map_frame_counter--;

            float alpha = 1.0f;
            if (g_new_map_frame_counter < 30) {
                alpha = g_new_map_frame_counter / 30.0f;
            }

            const char* line1 = "发现新地图！";
            const char* line2 = "切换到「地图管理」菜单，点击「设为新地图」按钮";

            ImVec2 line1_size = ImGui::CalcTextSize(line1);
            ImVec2 line2_size = ImGui::CalcTextSize(line2);
            float box_width = std::max(line1_size.x, line2_size.x) + 50.0f;
            float box_height = line1_size.y + line2_size.y + 45.0f;

            ImVec2 box_min((displayInfo.width - box_width) * 0.5f, (displayInfo.height - box_height) * 0.5f);
            ImVec2 box_max = ImVec2(box_min.x + box_width, box_min.y + box_height);

            ImU32 bg_color = IM_COL32(18, 20, 26, (int)(245 * alpha));
            ImU32 accent_color = IM_COL32(255, 200, 60, (int)(255 * alpha));
            ImU32 border_color = IM_COL32(80, 70, 40, (int)(180 * alpha));
            ImU32 shadow_color = IM_COL32(0, 0, 0, (int)(90 * alpha));

            Draw->AddRectFilled(ImVec2(box_min.x + 6, box_min.y + 8), ImVec2(box_max.x + 6, box_max.y + 8), shadow_color, 20.0f);
            Draw->AddRectFilled(box_min, box_max, bg_color, 20.0f);
            Draw->AddRect(box_min, box_max, border_color, 20.0f, 0, 2.0f);
            Draw->AddRectFilled(box_min, ImVec2(box_min.x + 5, box_max.y), accent_color, 20.0f);

            ImGui::PushFont(g_font_ui);
            ImVec2 title_size = ImGui::CalcTextSize(line1);
            ImVec2 text1_pos(box_min.x + (box_width - title_size.x) * 0.5f, box_min.y + 14.0f);
            Draw->AddText(g_font_ui, ImGui::GetFontSize() * 1.1f, text1_pos, accent_color, line1);
            ImGui::PopFont();

            ImVec2 text2_pos(box_min.x + (box_width - line2_size.x) * 0.5f, text1_pos.y + title_size.y + 10.0f);
            Draw->AddText(text2_pos, IM_COL32(230, 232, 240, (int)(255 * alpha)), line2);
        } else {
            g_new_map_detected = false;
            g_new_map_frame_counter = 0;
            g_new_map_prompted = false;  // 重置提示标记，下次发现新地图可再次弹窗
        }
    }

    // ========== 通知中心（现代 Toast 风格） ==========
    if (!g_notifications.empty()) {
        std::vector<size_t> toRemove;
        float notifY = displayInfo.height * 0.12f;
        const float notifMaxW = displayInfo.width * 0.85f;
        for (size_t i = 0; i < g_notifications.size(); i++) {
            auto& notif = g_notifications[i];
            notif.timer -= ImGui::GetIO().DeltaTime;
            if (notif.timer <= 0.0f) {
                toRemove.push_back(i);
                continue;
            }

            float alpha = 1.0f;
            if (notif.timer < 1.0f) alpha = notif.timer;

            ImVec2 textSize = ImGui::CalcTextSize(notif.text.c_str());
            float padX = 18.0f, padY = 12.0f;
            float boxW = std::min(textSize.x + padX * 2.0f, notifMaxW);
            float boxH = textSize.y + padY * 2.0f;
            ImVec2 boxMin((displayInfo.width - boxW) * 0.5f, notifY);
            ImVec2 boxMax(boxMin.x + boxW, boxMin.y + boxH);

            ImU32 accent = IM_COL32((int)(notif.color.x*255), (int)(notif.color.y*255), (int)(notif.color.z*255), (int)(255 * alpha));
            ImU32 bg = IM_COL32(18, 20, 26, (int)(235 * alpha));
            ImU32 border = IM_COL32(55, 60, 80, (int)(150 * alpha));
            ImU32 textCol = IM_COL32(245, 247, 250, (int)(255 * alpha));
            ImU32 shadow = IM_COL32(0, 0, 0, (int)(80 * alpha));

            // 柔和阴影
            Draw->AddRectFilled(ImVec2(boxMin.x + 4, boxMin.y + 6), ImVec2(boxMax.x + 4, boxMax.y + 6), shadow, 14.0f);
            Draw->AddRectFilled(boxMin, boxMax, bg, 14.0f);
            Draw->AddRect(boxMin, boxMax, border, 14.0f, 0, 1.5f);
            // 左侧强调色条
            Draw->AddRectFilled(ImVec2(boxMin.x, boxMin.y + 8), ImVec2(boxMin.x + 4, boxMax.y - 8), accent, 2.0f);
            Draw->AddText(ImVec2(boxMin.x + padX, boxMin.y + padY), textCol, notif.text.c_str());

            notifY += boxH + 12.0f;
        }

        for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
            g_notifications.erase(g_notifications.begin() + *it);
        }
    }
}

void Draw_Main(ImDrawList *Draw) { Draw_Main_Optimized(Draw); }

// 写入调试日志（线程安全）
void WriteDebugLog(const char* fmt, ...) {
    if (!g_debug_file) return;
    std::lock_guard<std::mutex> lock(g_debug_mutex);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_debug_file, fmt, args);
    va_end(args);
    fflush(g_debug_file);
}

// 打开调试日志文件
void OpenDebugLog() {
    if (g_debug_enabled.exchange(true)) return;

    static int counter = 0;
    char path[256];
    snprintf(path, sizeof(path), "/data/local/tmp/debug_%d.log", counter++);

    std::lock_guard<std::mutex> lock(g_debug_mutex);
    g_debug_file = fopen(path, "w");
    if (g_debug_file) {
        strncpy(g_debug_file_path, path, sizeof(g_debug_file_path) - 1);
        fprintf(g_debug_file, "classname,faction,subtype,posX,posY,posZ\n");
        fflush(g_debug_file);
        g_logged_entities.clear();
    }
}

// 关闭调试日志文件
void CloseDebugLog() {
    if (!g_debug_enabled.exchange(false)) return;
    std::lock_guard<std::mutex> lock(g_debug_mutex);
    if (g_debug_file) {
        fclose(g_debug_file);
        g_debug_file = nullptr;
    }
}

void read_thread(long int 状态数值, long int PD2, long int PD3) {
    printf("\033[35m[系统初始化中...]\033[0m\n");
    Timer data_timer("DataThread");
    data_timer.BindCurrentThreadToCores(false, "DataThread");
    printf("\033[35m[核心载入中...]\033[0m\n");

    while (true) {
        int saved = g_saved_pid.exchange(-1);
        if (saved != -1) {
            pid = saved;
            get_name_pid(extractedString);
        } else {
            pid = -1;
            while (pid == -1) {
                pid = get_name_pid1("dwrg");
                if (pid == -1) sleep(1);
            }
            get_name_pid(extractedString);
        }

        ModuleBssInfo result;
        if (std::strstr(extractedString, "com.netease.idv")) {
            GlobalMemory::libbase = getModuleBasegjf(pid, ".");
            result = get_module_bssgjf(pid, ".");
            GlobalMemory::bss_base = result.addr;
        } else {
            GlobalMemory::libbase = getModuleBase(const_cast<char *>(GlobalMemory::libso));
            result = get_module_bss(pid, GlobalMemory::libso);
            GlobalMemory::bss_base = result.addr;
        }
        GlobalMemory::ModulePagesCount = (result.taddr - result.addr) / 4096;
        std::vector<long> buff(512);

        GlobalMemory::MatrixOffset = 0;
        GlobalMemory::ArrayaddrOffset = 0;
        GlobalMemory::状态 = 1;
        int fail_cnt = 0;
        while (GlobalMemory::MatrixOffset == 0 || GlobalMemory::ArrayaddrOffset == 0) {
            for (long int i = 0; i < GlobalMemory::ModulePagesCount; i++) {
                vm_readv(result.addr + (i * 4096), buff.data(), 0x1000);
                for (int ii = 0; ii < 512; ii += 1) {
                    unsigned long val = buff[ii];
                    long int CurrentAddr = result.addr + (i * 4096) + (ii * 8);

                    if (GlobalMemory::MatrixOffset == 0) {
                        uint32_t low  = (uint32_t)(val & 0xFFFFFFFF);
                        uint32_t high = (uint32_t)(val >> 32);
                        long int candidate = 0;
                        if (low == 442745336)       candidate = CurrentAddr;
                        else if (high == 442745336) candidate = CurrentAddr + 4;
                        if (candidate != 0) {
                            if (getDword(candidate + 792) == 257 &&
                                getFloat(candidate + 320) == 1.0f) {
                                GlobalMemory::MatrixOffset = (candidate - GlobalMemory::libbase) + 1224;
                            }
                        }
                    }
                    if (val == 16384) {
                        if (getFloat(CurrentAddr - 16) == 1.0f && getDword(CurrentAddr - 8) == 257) {
                            GlobalMemory::ArrayaddrOffset = CurrentAddr - GlobalMemory::libbase + 56;
                        }
                    }
                }
            }
            if (GlobalMemory::MatrixOffset != 0 && GlobalMemory::ArrayaddrOffset != 0) {
                GlobalMemory::状态 = 2;
                printf("\033[35m[运行状态: 就绪]\033[0m\n");
                printf("\033[35m[数据线程绑定在 CPU: %d]\033[0m\n", sched_getcpu());
                break;
            }
            if (++fail_cnt > 10) {
                printf("\033[35m[错误: 偏移扫描超时，重新获取基址...]\033[0m\n");
                GlobalMemory::状态 = 0;
                break;
            }
            sleep(2);
        }

        if (GlobalMemory::状态 != 2) continue;

        int back_buffer_idx = 1;
        char temp_name[256];
        std::string s_prophet;
        std::unordered_set<uintptr_t> seen_pointers;

        while (true) {
            static int empty_count = 0;
            if (g_need_reinit.load()) {
                g_need_reinit.store(false);
                printf("\033[35m[运行状态: 数据线程安全重初始化...]\033[0m\n");

                int cur_pid = pid.load();
                if (cur_pid > 0) {
                    g_saved_pid.store(cur_pid);
                } else {
                    g_saved_pid.store(-1);
                }

                skipClassCache.clear();
                fakeHunterCache.clear();
                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    data_buffers[0].clear();
                    data_buffers[1].clear();
                }
                {
                    std::lock_guard<std::mutex> lock(mimic_mutex);
                    global_validRoles.clear();
                    bound_seat_by_class.clear();
                }
                监管者预知[0] = '\0';
                GlobalMemory::数量 = 0;
                GlobalMemory::状态 = 0;

                // 重置地图状态：清除过期纹理缓存和地图索引
                // 防止重连/切场景后残留旧地图数据（地图索引错误、纹理ID过期）
                InvalidateMapTextures();
                g_current_map_index = -1;
                g_current_floor_index = 0;
                g_last_valid_map_index = -1;
                g_last_valid_floor_index = 0;
                g_switch_candidate_index = -1;
                g_switch_confirm_frames = 0;
                g_musicbox_moved_on_same_map = false;
                g_new_map_detected = false;
                g_new_map_prompted = false;
                g_new_map_frame_counter = 0;
                g_detected_musicbox_pos = Vector3A{};
                g_detected_piano_pos = Vector3A{};
                g_frames_since_musicbox_lost = 0;

                break;
            }

            auto &local_data = data_buffers[back_buffer_idx];
            local_data.clear();
            seen_pointers.clear();
            GlobalMemory::Arrayaddr = GlobalMemory::libbase + GlobalMemory::ArrayaddrOffset;
            uintptr_t StartPtr = getPtr64(GlobalMemory::Arrayaddr);
            uintptr_t EndPtr = getPtr64(GlobalMemory::Arrayaddr + 8);
            long count = 0;
            if (StartPtr > 0 && EndPtr > StartPtr) {
                count = (EndPtr - StartPtr) / 8;
            }
            if (count > 1500) count = 1500;

            s_prophet.clear();

            for (int ii = 0; ii < count; ii++) {
                uintptr_t 对象 = getPtr64(StartPtr + ii * 8);
                if (对象 == 0) continue;
                if (!seen_pointers.insert(对象).second) continue;

                uintptr_t coorBase = getPtr64(对象 + 0x28);
                if (!coorBase) continue;
                uintptr_t namezfcz = getPtr64(getPtr64(getPtr64(getPtr64(getPtr64(对象 + 0xF8) + 0x0) + 0x8) + 0x20) + 0x20);
                if (namezfcz == 0) {
                    if (Debugging) {
                        DataStruct item{};
                        item.obj = 对象;
                        item.阵营 = 6;
                        item.sub_type = ObjSubClass::Prop;
                        snprintf(item.类名, sizeof(item.类名), "[未知:0x%lx]", 对象);
                        local_data.push_back(item);
                    }
                    continue;
                }
                int len = getDword(namezfcz + 0x10);
                uintptr_t name_val_ptr = getPtr64(namezfcz + 0x8);
                if (name_val_ptr == 0 || len <= 0 || len >= 256) continue;
                vm_readv(name_val_ptr, temp_name, len);

                temp_name[len] = '\0';

                if (should_filter_cached(temp_name)) continue;

                const char *cls = temp_name;

                ObjLocalCache objProps;
                vm_readv(对象 + 0x70, &objProps, sizeof(ObjLocalCache));
                float 状态数值 = objProps.状态数值;
                int 实体特征码 = objProps.实体特征码;

                if (show_draw_prophet) {
                    if (std::strstr(cls, "boss") && !std::strstr(cls, "burke_console") &&
                        !std::strstr(cls, "h55_joseph_camera") &&
                        !std::strstr(cls, "redqueen_e_heijin_yizi") &&
                        !std::strstr(cls, "chuanhuo")) {
                        if (!IsFakeHunter_cached(cls)) {
                            std::string s = getboss(cls);
                            if (!s.empty() && !std::strstr(s.c_str(), "butcher") &&
                                s_prophet.find(s) == std::string::npos) {
                                s_prophet += s + " ";
                            }
                        }
                    }
                }

                bool is_woodplane = (std::strstr(cls, "woodplane01") || std::strstr(cls, "woodplane001"));
                bool is_faction4 = (std::strstr(cls, "prop") || std::strstr(cls, "mj_") || std::strstr(cls, "rd") || MjSubsystem::IsMjSpecialClass(cls));

                bool effective_disable_filter = disable_skip_filter || MjSubsystem::ShouldBypassFilter();
                if (!effective_disable_filter && !is_woodplane && !is_faction4) {
                    if (std::isnan(状态数值) || std::isinf(状态数值) || std::abs(状态数值 - std::round(状态数值)) > 0.0f) continue;
                    if (std::abs(状态数值) > 1000.0f || 状态数值 < 0.0f) continue;
                    if (状态数值 == 0.0f && 实体特征码 == 0) continue;
                    bool isSender = (std::strstr(cls, "sender") != nullptr) || (std::strstr(cls, "dm65_scene_sender") != nullptr);
                    if (isSender && (状态数值 == 0.0f || 实体特征码 == 0)) continue;
                }

                if (std::strstr(cls, "player") || std::strstr(cls, "boss") ||
                    状态数值 == 450.0f || std::strstr(cls, "scene") ||
                    std::strstr(cls, "prop") || std::strstr(cls, "mirror") || Debugging ||
                    disable_skip_filter ||
                    MjSubsystem::IsMjSpecialClass(cls) ||
                    MjSubsystem::IsMjPropClass(cls) || std::strstr(cls, "monster")) {

                    int actionId = 0;
                    uintptr_t actionPtr = getPtr64(对象 + 0x730);
                    if (actionPtr != 0) actionId = getDword(actionPtr + 0x30);

                    DataStruct item{};
                    item.obj = 对象;
                    item.objcoor = coorBase;
                    item.action = actionId;
                    item.状态数值 = 状态数值;
                    item.实体特征码 = 实体特征码;
                    item.sub_type = ObjSubClass::Unknown;
                    item.prop_name[0] = '\0';
                    item.is_ghost = (实体特征码 != 0x1000000 || 状态数值 != 450.0f);

                    if (std::strstr(cls, "random01_in_piano01.gim")) {
                        item.阵营 = 6;
                        item.sub_type = ObjSubClass::Prop;
                        std::strcpy(item.str, "");
                    } else if (std::strstr(cls, "trap.gim")) {
                        item.阵营 = 6;
                        item.sub_type = ObjSubClass::Prop;
                        std::strcpy(item.str, "");
                    } else if (std::strstr(cls, "monster_daozei") ||
                               std::strstr(cls, "monster_xiaobai") ||
                               std::strstr(cls, "monster_muchao") ||
                               std::strstr(cls, "monster_tiao") ||
                               std::strstr(cls, "monster_miaosha")) {
                        item.阵营 = 6;
                        item.sub_type = ObjSubClass::Prop;
                        std::strcpy(item.str, "");
                    } else if (MjSubsystem::IsMjSpecialClass(cls)) {
                        item.阵营 = 6;
                        item.sub_type = ObjSubClass::Prop;
                        std::strcpy(item.str, "");
                    } else if (std::strstr(cls, "redqueen_mirror.gim") || std::strstr(cls, "redqueen_mirror_model")) {
                        item.阵营 = 5;
                        std::strcpy(item.str, std::strstr(cls, "model") ? "水镜[捏镜]" : "水镜[实体]");
                    } else if (std::strstr(cls, "boss") && !std::strstr(cls, "prop") && !std::strstr(cls, "mj_") && !std::strstr(cls, "rd") && !std::strstr(cls, "trap.gim")) {
                        std::strcpy(item.str, getboss(cls));
                        item.阵营 = 1;
                        item.sub_type = ObjSubClass::Boss;
                    } else if ((std::strstr(cls, "player") || std::strstr(cls, "npc_deluosi_dress_ghost")) &&
                               !std::strstr(cls, "prop") && !std::strstr(cls, "mj_") && !std::strstr(cls, "rd")) {
                        std::strcpy(item.str, getplayer(cls));
                        item.阵营 = 2;
                        item.sub_type = ObjSubClass::Player;
                    } else if (std::strstr(cls, "scene") && !std::strstr(cls, "prop") && !std::strstr(cls, "rd") && !MjSubsystem::IsMjSpecialClass(cls) && !std::strstr(cls, "monster")) {
                        std::strcpy(item.str, getscene(cls));
                        item.阵营 = 3;
                        if (std::strstr(cls, "trap.gim")) item.sub_type = ObjSubClass::Trap;
                        else if (std::strstr(cls, "sender") || std::strstr(cls, "dm65_scene_sender")) item.sub_type = ObjSubClass::CipherMachine;
                        else if (std::strstr(cls, "polun_jiazi.gim")) item.sub_type = ObjSubClass::Clip;
                        else if (std::strstr(cls, "h55_sleepingtown3_jpcat01low")) item.sub_type = ObjSubClass::Cat;
                        else if (std::strstr(cls, "h55_playground_lion")) item.sub_type = ObjSubClass::Lion;
                        else if (std::strstr(cls, "dm65_scene_prop_76")) item.sub_type = ObjSubClass::Cellar;
                        else if (std::strstr(cls, "dm65_scene_prop_01") || std::strstr(cls, "christmasbox01") || std::strstr(cls, "halloweenbox01")) item.sub_type = ObjSubClass::Box;
                        else if (std::strstr(cls, "dm65_scene_gallows") || std::strstr(cls, "dm65_scene_gallows_hx_low")) item.sub_type = ObjSubClass::Chair;
                        else if (std::strstr(cls, "woodplane001") || std::strstr(cls, "woodplane01")) item.sub_type = ObjSubClass::Pallet;
                    } else if (std::strstr(cls, "prop") || std::strstr(cls, "mj_") || std::strstr(cls, "rd")) {
                        std::strcpy(item.str, getprop(cls));
                        if (std::strstr(cls, "prop_musicbox") || MjSubsystem::IsMjPropClass(cls) || MjSubsystem::IsMjSpecialClass(cls)) {
                            item.阵营 = 6;
                        } else {
                            item.阵营 = 4;
                        }
                        item.sub_type = ObjSubClass::Prop;
                    } else {
                        continue;
                    }

                    if (item.阵营 == 4 || item.阵营 == 6) {
                        bool found = false;
                        for (const auto& [keyword, display_name] : g_prop_name_map) {
                            if (std::strstr(cls, keyword.c_str())) {
                                std::strcpy(item.prop_name, display_name.c_str());
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            snprintf(item.prop_name, sizeof(item.prop_name), "[新:%04lx]", item.obj & 0xFFFF);
                        }
                    }

                    std::strcpy(item.类名, cls);

                    if (item.阵营 == 1 || item.阵营 == 2) {
                        if (item.is_ghost) {
                            if (!inform_ghost) continue;
                            if (std::strstr(item.str, "红蝶") || std::strstr(item.str, "无常") ||
                                std::strstr(item.str, "歌剧") || std::strstr(item.str, "破轮") ||
                                std::strstr(item.str, "木偶") || std::strstr(item.str, "冒险家")) continue;
                        }
                    }

                    if (Debugging) {
                        Vector3A pos = getObjectCoordinates(item.objcoor, (item.阵营 == 4 || item.阵营 == 6));
                        char key[512];
                        if (isValidCoordinate(pos)) {
                            snprintf(key, sizeof(key), "%s|%d|%.2f,%.2f,%.2f",
                                     item.类名, item.阵营, pos.X, pos.Y, pos.Z);
                        } else {
                            snprintf(key, sizeof(key), "%s|%d|invalid", item.类名, item.阵营);
                        }
                        bool should_log = false;
                        {
                            std::lock_guard<std::mutex> lock(g_debug_mutex);
                            if (g_logged_entities.find(key) == g_logged_entities.end()) {
                                g_logged_entities.insert(key);
                                should_log = true;
                            }
                        }
                        if (should_log) {
                            const char* stype = "Unknown";
                            switch (item.sub_type) {
                                case ObjSubClass::CipherMachine: stype = "CipherMachine"; break;
                                case ObjSubClass::Pallet:        stype = "Pallet"; break;
                                case ObjSubClass::Chair:         stype = "Chair"; break;
                                case ObjSubClass::Box:           stype = "Box"; break;
                                case ObjSubClass::Cellar:        stype = "Cellar"; break;
                                case ObjSubClass::Trap:          stype = "Trap"; break;
                                case ObjSubClass::Clip:          stype = "Clip"; break;
                                case ObjSubClass::Cat:           stype = "Cat"; break;
                                case ObjSubClass::Lion:          stype = "Lion"; break;
                                case ObjSubClass::Prop:          stype = "Prop"; break;
                                case ObjSubClass::Boss:          stype = "Boss"; break;
                                case ObjSubClass::Player:        stype = "Player"; break;
                                default: break;
                            }
                            if (isValidCoordinate(pos)) {
                                WriteDebugLog("%s,%d,%s,%.2f,%.2f,%.2f\n",
                                              item.类名, item.阵营, stype, pos.X, pos.Y, pos.Z);
                            } else {
                                WriteDebugLog("%s,%d,%s,invalid,invalid,invalid\n",
                                              item.类名, item.阵营, stype);
                            }
                        }
                    }

                    local_data.push_back(item);
                }
            }

            bool has_valid_data = false;
            for (const auto& d : local_data) {
                if (d.类名[0] != '[') {
                    has_valid_data = true;
                    break;
                }
            }
            if (!has_valid_data) {
                empty_count++;
                if (empty_count >= 5) {
                    printf("\033[35m[运行状态: 自动重新初始化...]\033[0m\n");
                    g_need_reinit.store(true);
                    empty_count = 0;
                }
            } else {
                empty_count = 0;
            }

            std::snprintf(监管者预知, sizeof(监管者预知), "%s", s_prophet.c_str());
            GlobalMemory::数量 = local_data.size();
            front_buffer_idx.store(back_buffer_idx, std::memory_order_release);
            back_buffer_idx = 1 - back_buffer_idx;

            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(40, 55);
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));
        }
    }
    CloseDebugLog();
}


bool InitTouch() {
    if (g_touch_ready) return true;
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDWR);
        if (fd < 0) continue;
        unsigned long abs_bits[ABS_MAX / (sizeof(unsigned long) * 8) + 1] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) {
            close(fd);
            continue;
        }
        auto check_bit = [&](int code) -> bool {
            return (abs_bits[code / (sizeof(unsigned long) * 8)] >> (code % (sizeof(unsigned long) * 8))) & 1;
        };
        if (!check_bit(ABS_MT_SLOT) || !check_bit(ABS_MT_TRACKING_ID) ||
            !check_bit(ABS_MT_POSITION_X) || !check_bit(ABS_MT_POSITION_Y)) {
            close(fd);
            continue;
        }
        struct input_absinfo abs_info;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_info) == 0) g_touch_max_x = abs_info.maximum;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_info) == 0) g_touch_max_y = abs_info.maximum;
        strncpy(g_touch_path, path, sizeof(g_touch_path));
        g_touch_ready = true;
        close(fd);
        return true;
    }
    return false;
}

void SimulateClick(int x, int y) {
    if (!g_touch_ready) {
        if (!InitTouch()) return;
    }
    int raw_x = (int)((float)x / displayInfo.width * g_touch_max_x);
    int raw_y = (int)((float)y / displayInfo.height * g_touch_max_y);
    int fd = open(g_touch_path, O_RDWR);
    if (fd < 0) return;
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS; ev.code = ABS_MT_SLOT; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_TRACKING_ID; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.type = EV_KEY; ev.code = BTN_TOOL_FINGER; ev.value = 1; write(fd, &ev, sizeof(ev));
    ev.code = BTN_TOUCH; ev.value = 1; write(fd, &ev, sizeof(ev));
    ev.type = EV_ABS; ev.code = ABS_MT_POSITION_X; ev.value = raw_x; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_POSITION_Y; ev.value = raw_y; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_TOUCH_MAJOR; ev.value = 10; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_PRESSURE; ev.value = 50; write(fd, &ev, sizeof(ev));
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0; write(fd, &ev, sizeof(ev));
    usleep(50000);
    ev.type = EV_ABS; ev.code = ABS_MT_SLOT; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.code = ABS_MT_TRACKING_ID; ev.value = -1; write(fd, &ev, sizeof(ev));
    ev.type = EV_KEY; ev.code = BTN_TOOL_FINGER; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.code = BTN_TOUCH; ev.value = 0; write(fd, &ev, sizeof(ev));
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0; write(fd, &ev, sizeof(ev));
    close(fd);
}

void AutoWoodCheck() {
    if (!wood_enabled) return;
    const auto& current_data = data_buffers[front_buffer_idx.load(std::memory_order_acquire)];
    bool self_is_survivor = false;
    for (const auto& item : current_data) {
        if (item.obj == GlobalMemory::自身) {
            if (item.阵营 == 2) self_is_survivor = true;
            break;
        }
    }
    if (!self_is_survivor) return;
    if (GlobalMemory::自身 == 0) return;

    float minHunterDist = 9999.0f;
    const DataStruct* nearest_hunter = nullptr;
    Vector3A hunterPos;
    for (const auto& item : current_data) {
        if (item.阵营 != 1 || item.is_ghost) continue;
        Vector3A pos = getObjectCoordinates(item.objcoor, false);
        if (!isValidCoordinate(pos)) continue;
        float dist = FastMath::fastDistanceSquared(Z, pos);
        if (dist < minHunterDist) {
            minHunterDist = dist;
            nearest_hunter = &item;
            hunterPos = pos;
        }
    }
    if (!nearest_hunter) return;

    float minWoodDist = 9999.0f;
    const DataStruct* nearest_wood = nullptr;
    Vector3A woodPos;
    for (const auto& item : current_data) {
        if (item.sub_type != ObjSubClass::Pallet) continue;
        if (item.action == 131088 || item.action == 196624) continue;
        Vector3A pos = getObjectCoordinates(item.objcoor, false);
        if (!isValidCoordinate(pos)) continue;
        float dist = FastMath::fastDistanceSquared(Z, pos);
        if (dist < minWoodDist) {
            minWoodDist = dist;
            nearest_wood = &item;
            woodPos = pos;
        }
    }
    if (!nearest_wood) return;

    float dx = getFloat(nearest_wood->objcoor + 0xB8);
    float dy = getFloat(nearest_wood->objcoor + 0xC0);
    float angle = atan2f(dy, dx);

    float x[4], y[4];
    x[0] = woodPos.X + (wood_length / 2) * cosf(angle) + (wood_width / 2) * cosf(angle + M_PI_2);
    y[0] = woodPos.Y + (wood_length / 2) * sinf(angle) + (wood_width / 2) * sinf(angle + M_PI_2);
    x[1] = woodPos.X + (wood_length / 2) * cosf(angle) - (wood_width / 2) * cosf(angle + M_PI_2);
    y[1] = woodPos.Y + (wood_length / 2) * sinf(angle) - (wood_width / 2) * sinf(angle + M_PI_2);
    x[2] = woodPos.X - (wood_length / 2) * cosf(angle) - (wood_width / 2) * cosf(angle + M_PI_2);
    y[2] = woodPos.Y - (wood_length / 2) * sinf(angle) - (wood_width / 2) * sinf(angle + M_PI_2);
    x[3] = woodPos.X - (wood_length / 2) * cosf(angle) + (wood_width / 2) * cosf(angle + M_PI_2);
    y[3] = woodPos.Y - (wood_length / 2) * sinf(angle) + (wood_width / 2) * sinf(angle + M_PI_2);

    float xmax = x[0], xmin = x[0], ymax = y[0], ymin = y[0];
    for (int i = 1; i < 4; i++) {
        if (x[i] > xmax) xmax = x[i];
        if (x[i] < xmin) xmin = x[i];
        if (y[i] > ymax) ymax = y[i];
        if (y[i] < ymin) ymin = y[i];
    }

    bool hunterInside = (hunterPos.X <= xmax && hunterPos.X >= xmin &&
                         hunterPos.Y <= ymax && hunterPos.Y >= ymin);
    float distToWood = sqrtf(FastMath::fastDistanceSquared(Z, woodPos)) / 11.886f;

    if (hunterInside && distToWood <= wood_trigger_dist) {
        int tx = wood_touch_x + (rand() % 100 - 50);
        int ty = wood_touch_y + (rand() % 100 - 50);
        SimulateClick(tx, ty);
        usleep(50000);
    }
}

// ===================== g_talent_view功能 =====================
namespace fs = std::filesystem;

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

const std::map<int, std::string> SURVIVOR_TALENT_MAP = {
        {1, "关系场"}, {2, "相濡以沫"}, {3, "祸福相依"}, {4, "幸存者本能"},
        {5, "韦伯定律"}, {6, "防御机制"}, {7, "愈合"}, {8, "飞轮效应"},
        {9, "好奇心"}, {10, "心灵感应"}, {11, "不屈不挠"}, {12, "分心"},
        {13, "逃逸"}, {14, "触摸效应"}, {15, "假寐"}, {16, "回光返照"},
        {17, "鸟笼效应"}, {18, "宣泄效应"}, {19, "救世主情结"}, {20, "悄无声息"},
        {21, "从众心理"}, {22, "避难所"}, {23, "医者"}, {24, "化险为夷"},
        {25, "火中取栗"}, {26, "绝处逢生"}, {27, "马蝇效应"}, {28, "囚徒困境"},
        {29, "肌肉记忆"}, {30, "求生意志"}, {31, "巨力"}, {32, "膝跳反射"},
        {33, "共生效应"}, {34, "结伴效应"}, {35, "寒意"}, {36, "酝酿效应"},
        {37, "观众效应"}, {38, "云中漫步"}, {39, "共情"}, {40, "感觉适应"}
};
const std::map<int, std::string> BUTCHER_TALENT_MAP = {
        {1, "恶化"}, {2, "执念"}, {3, "枯萎"}, {4, "恐慌"},
        {5, "破坏欲"}, {6, "狂暴"}, {7, "愤怒"}, {8, "禁闭空间"},
        {9, "封禁"}, {10, "困兽之斗"}, {11, "狩猎本能"}, {12, "报幕"},
        {13, "好客之道"}, {14, "崩坏"}, {15, "通缉"}, {16, "底牌"},
        {17, "惯性"}, {18, "摧枯拉朽"}, {19, "嘲弄"}, {20, "淬火效应"},
        {21, "狂欢"}, {22, "巨钳"}, {23, "拘禁狂"}, {24, "挽留"},
        {25, "首路"}, {26, "掌控欲"}, {27, "耐受力"}, {28, "表现欲"},
        {29, "后遗症"}, {30, "追猎"}, {31, "清道夫"}, {32, "张狂"},
        {33, "戏弄"}, {34, "成瘾症"}, {35, "冲动"}, {36, "幽闭恐惧"},
        {37, "恋旧癖"}, {38, "饥荒"}, {39, "无人生还"}, {40, "警觉"}
};
const std::map<int, std::string> SKILL_MAP = {
        {1, "聆听"}, {2, "失常"}, {3, "金身"}, {4, "巡视者"},
        {5, "传送"}, {6, "插眼"}, {7, "闪现"}, {8, "移形"}
};

fs::path find_snapshot_file(const fs::path& search_dir) {
    std::error_code ec;
    fs::path target_file;
    fs::file_time_type newest_time;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(search_dir, ec)) {
            if (ec) { ec.clear(); continue; }
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().filename() == "battle_frames_snapshot_0.txt") {
                auto write_time = fs::last_write_time(entry.path(), ec);
                if (!ec && write_time > newest_time) {
                    newest_time = write_time;
                    target_file = entry.path();
                }
            }
        }
    } catch (...) { return fs::path(); }
    return target_file;
}

void parse_talent_json(TalentState& state, const std::string& json_path, bool detailed) {
    state.players.clear();
    std::ifstream file(json_path);
    if (!file) { state.status = "无法打开JSON"; return; }
    json j;
    try { file >> j; } catch (...) { state.status = "JSON解析失败"; return; }
    if (!j.contains("data") || !j["data"].is_array() || j["data"].empty()) {
        state.status = "JSON格式错误"; return;
    }
    const auto& entities = j["data"][0]["snap_shot"]["event_data"]["entities"];
    if (!entities.is_object()) { state.status = "无entities"; return; }
    static const std::unordered_set<int> big_talents = {8, 16, 24, 32};
    for (auto& [uid, obj] : entities.items()) {
        if (!obj.contains("unit_type") || !obj.contains("player_name")) continue;
        int utype = obj["unit_type"];
        if (utype != 1 && utype != 2) continue;
        PlayerInfo info;
        info.unit_type = utype;
        info.name = obj["player_name"];
        info.camp = (utype == 1) ? "监管者" : "求生者";
        const auto* talent_map = (utype == 1) ? &BUTCHER_TALENT_MAP : &SURVIVOR_TALENT_MAP;
        if (obj.contains("genius_id_lvs") && obj["genius_id_lvs"].is_array()) {
            for (auto& pair : obj["genius_id_lvs"]) {
                if (pair.is_array() && pair.size() >= 1) {
                    int tid = pair[0];
                    if (!detailed && big_talents.find(tid) == big_talents.end()) continue;
                    auto it = talent_map->find(tid);
                    if (it != talent_map->end()) {
                        info.talents.push_back(it->second);
                        info.talent_ids.push_back(tid);
                    } else {
                        info.talents.push_back("[天赋" + std::to_string(tid) + "]");
                        info.talent_ids.push_back(tid);
                    }
                }
            }
        }
        if (obj.contains("support_skill_id") && obj["support_skill_id"].is_array()) {
            for (const auto& sid : obj["support_skill_id"]) {
                if (sid.is_number()) {
                    int skill_id = sid;
                    info.skill_ids.push_back(skill_id);
                    auto it = SKILL_MAP.find(skill_id);
                    if (it != SKILL_MAP.end()) info.skill_names.push_back(it->second);
                    else info.skill_names.push_back("技能" + std::to_string(skill_id));
                }
            }
        }
        state.players.push_back(info);
    }
    state.status = "已加载 " + std::to_string(state.players.size()) + " 名玩家";
}

// 现代 UI 主题结构定义（实现函数在 Layout_tick_UI 之前）
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

static UITheme g_theme;
static float g_ui_density = 1.0f;  // 全局 UI 密度，供子窗口/弹窗共享

void show_talent_viewer() {
    static TalentState state;
    static bool need_refresh = true;
    static const std::string pickle_path = "/sdcard/battle_frames_snapshot.pickle";
    static const std::string json_path  = "/sdcard/battle_frames_snapshot.json";

    std::string game_package;
    if (extractedString[0] != '\0') game_package = extractedString;
    else DetectGameProcess(game_package);

    if (g_talent_need_refresh) {
        need_refresh = true;
        g_talent_need_refresh = false;
    }
    auto now = std::chrono::steady_clock::now();
    if (need_refresh || now - state.last_check > std::chrono::seconds(3)) {
        need_refresh = false;
        state.last_check = now;
        if (game_package.empty()) {
            state.status = "未检测到游戏进程";
            return;
        }
        std::string netease_root = "/storage/emulated/0/Android/data/" + game_package + "/files/netease/";
        fs::path search_root(netease_root);
        bool file_found = false;
        if (fs::exists(search_root)) {
            auto newest = find_snapshot_file(search_root);
            if (!newest.empty()) {
                std::ifstream src(newest, std::ios::binary);
                std::ofstream dst(pickle_path, std::ios::binary);
                dst << src.rdbuf();
                file_found = true;
            }
        }
        if (file_found) {
            std::string cmd = "/data/data/com.termux/files/usr/bin/python3 /sdcard/convert_pickle.py "
                              + pickle_path + " " + json_path + " 2>&1";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buffer[256];
                std::string result;
                while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
                int ret = pclose(pipe);
                if (ret == 0 && result.find("OK") != std::string::npos) {
                    parse_talent_json(state, json_path, g_show_detailed);
                } else {
                    state.status = "转换失败: " + result;
                    state.players.clear();
                }
            } else state.status = "无法执行Python";
        }
    }

    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f * g_ui_density);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * g_ui_density);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * g_ui_density, 14 * g_ui_density));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, g_theme.bg_dark);
    ImGui::PushStyleColor(ImGuiCol_Border, g_theme.border_strong);
    ImGui::PushStyleColor(ImGuiCol_Button, g_theme.primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_theme.primary_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_theme.primary_active);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, g_theme.check_mark);
    ImGui::Begin("天赋查看", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushFont(g_font_ui);
    ImGui::TextColored(g_theme.text_title, "天赋查看");
    ImGui::PopFont();
    ImGui::Separator();
    ImVec4 status_col;
    if (state.status.find("已加载") != std::string::npos) status_col = g_theme.success;
    else if (state.status.find("失败") != std::string::npos || state.status.find("X") != std::string::npos) status_col = g_theme.danger;
    else status_col = g_theme.warning;
    ImGui::TextColored(status_col, "状态: %s", state.status.c_str());
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, g_theme.success);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_theme.success_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_theme.success_active);
    ImGui::PushStyleColor(ImGuiCol_Text, g_theme.text_on_primary);
    if (ImGui::Button(" 刷新  ")) need_refresh = true;
    ImGui::PopStyleColor(4);
    ImGui::SameLine();
    if (ImGui::Checkbox("详细天赋", &g_show_detailed)) need_refresh = true;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!state.players.empty()) {
        if (ImGui::BeginTable("天赋表", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("阵营", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("玩家", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("携带天赋", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& p : state.players) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(p.unit_type == 1 ? g_theme.danger : g_theme.success, "%s", p.camp.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", p.name.c_str());
                ImGui::TableSetColumnIndex(2);
                if (p.talents.empty() && p.skill_names.empty()) {
                    ImGui::TextColored(g_theme.text_muted, "无");
                } else {
                    for (size_t i = 0; i < p.talents.size(); i++) {
                        int tid = (i < p.talent_ids.size()) ? p.talent_ids[i] : 0;
                        ImVec4 tag_color;
                        if (tid == 8 || tid == 24) tag_color = g_theme.danger;
                        else if (tid == 16 || tid == 32) tag_color = g_theme.primary;
                        else {
                            static const ImVec4 color_pool[] = {
                                    ImVec4(0.9f, 0.5f, 0.2f, 1.0f), ImVec4(0.5f, 0.8f, 0.2f, 1.0f),
                                    ImVec4(0.4f, 0.7f, 0.9f, 1.0f), ImVec4(0.8f, 0.4f, 0.8f, 1.0f),
                                    ImVec4(0.9f, 0.8f, 0.2f, 1.0f), ImVec4(0.3f, 0.9f, 0.7f, 1.0f),
                                    ImVec4(1.0f, 0.6f, 0.7f, 1.0f), ImVec4(0.7f, 0.7f, 0.7f, 1.0f)
                            };
                            int idx = i % 8;
                            tag_color = color_pool[idx];
                        }
                        ImGui::PushStyleColor(ImGuiCol_Button, tag_color);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tag_color);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * g_ui_density);
                        ImGui::SmallButton(p.talents[i].c_str());
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor(2);
                        if (i < p.talents.size() - 1) ImGui::SameLine();
                    }
                    if (!p.skill_names.empty()) {
                        if (!p.talents.empty()) { ImGui::SameLine(); ImGui::TextColored(g_theme.text_muted, "|"); }
                        for (size_t j = 0; j < p.skill_names.size(); j++) {
                            ImGui::SameLine();
                            ImGui::PushStyleColor(ImGuiCol_Button, g_theme.info);
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_theme.info);
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * g_ui_density);
                            ImGui::SmallButton(p.skill_names[j].c_str());
                            ImGui::PopStyleVar();
                            ImGui::PopStyleColor(2);
                        }
                    }
                }
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextColored(g_theme.text_muted, "暂无数据，请进入游戏后点击刷新");
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
}

// ============================================================
//  现代 UI 主题系统：统一配色、字体层级、间距、圆角、动效
// ============================================================
static void InitModernUITheme() {
    // 现代深色主题：深蓝灰背景 + 青蓝主色 + 语义辅助色
    g_theme.bg_dark         = ImVec4(0.035f, 0.037f, 0.045f, 0.96f);
    g_theme.bg_panel        = ImVec4(0.060f, 0.063f, 0.075f, 0.94f);
    g_theme.bg_card         = ImVec4(0.085f, 0.090f, 0.110f, 0.90f);
    g_theme.bg_card_hover   = ImVec4(0.100f, 0.105f, 0.130f, 0.95f);
    g_theme.bg_input        = ImVec4(0.110f, 0.120f, 0.150f, 0.85f);
    g_theme.bg_hover        = ImVec4(0.130f, 0.140f, 0.170f, 0.90f);
    g_theme.bg_active       = ImVec4(0.165f, 0.175f, 0.210f, 0.95f);
    g_theme.bg_overlay      = ImVec4(0.000f, 0.000f, 0.000f, 0.650f);

    g_theme.primary         = ImVec4(0.220f, 0.560f, 0.980f, 0.95f);
    g_theme.primary_hover   = ImVec4(0.350f, 0.670f, 1.000f, 1.00f);
    g_theme.primary_active  = ImVec4(0.150f, 0.450f, 0.880f, 1.00f);
    g_theme.primary_soft    = ImVec4(0.220f, 0.560f, 0.980f, 0.20f);

    g_theme.success         = ImVec4(0.180f, 0.760f, 0.460f, 0.95f);
    g_theme.success_hover   = ImVec4(0.280f, 0.860f, 0.560f, 1.00f);
    g_theme.success_active  = ImVec4(0.120f, 0.600f, 0.360f, 1.00f);

    g_theme.danger          = ImVec4(0.920f, 0.280f, 0.320f, 0.95f);
    g_theme.danger_hover    = ImVec4(1.000f, 0.380f, 0.420f, 1.00f);
    g_theme.danger_active   = ImVec4(0.780f, 0.200f, 0.240f, 1.00f);

    g_theme.warning         = ImVec4(1.000f, 0.760f, 0.180f, 1.00f);
    g_theme.info            = ImVec4(0.350f, 0.750f, 0.950f, 1.00f);

    g_theme.text            = ImVec4(0.930f, 0.940f, 0.960f, 1.00f);
    g_theme.text_muted      = ImVec4(0.580f, 0.610f, 0.680f, 1.00f);
    g_theme.text_title      = ImVec4(0.980f, 0.980f, 1.000f, 1.00f);
    g_theme.text_on_primary = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);

    g_theme.border          = ImVec4(0.180f, 0.200f, 0.260f, 0.55f);
    g_theme.border_strong   = ImVec4(0.280f, 0.320f, 0.420f, 0.75f);
    g_theme.border_light    = ImVec4(0.400f, 0.450f, 0.580f, 0.35f);

    g_theme.check_mark      = ImVec4(0.250f, 0.900f, 0.520f, 1.00f);
    g_theme.slider_grab     = ImVec4(0.300f, 0.620f, 0.980f, 1.00f);
    g_theme.slider_grab_active = ImVec4(0.450f, 0.720f, 1.000f, 1.00f);
}

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t));
}

struct StyleBackup {
    float WindowRounding, FrameRounding, ChildRounding, PopupRounding;
    float ScrollbarRounding, GrabRounding, TabRounding;
    ImVec2 WindowPadding, FramePadding, ItemSpacing, ItemInnerSpacing, CellPadding, TouchExtraPadding;
    float IndentSpacing, ScrollbarSize;
    ImVec4 Colors[ImGuiCol_COUNT];
};

static StyleBackup BackupImGuiStyle() {
    StyleBackup bak;
    ImGuiStyle& s = ImGui::GetStyle();
    bak.WindowRounding = s.WindowRounding; bak.FrameRounding = s.FrameRounding; bak.ChildRounding = s.ChildRounding;
    bak.PopupRounding = s.PopupRounding; bak.ScrollbarRounding = s.ScrollbarRounding; bak.GrabRounding = s.GrabRounding; bak.TabRounding = s.TabRounding;
    bak.WindowPadding = s.WindowPadding; bak.FramePadding = s.FramePadding; bak.ItemSpacing = s.ItemSpacing; bak.ItemInnerSpacing = s.ItemInnerSpacing;
    bak.CellPadding = s.CellPadding; bak.TouchExtraPadding = s.TouchExtraPadding;
    bak.IndentSpacing = s.IndentSpacing; bak.ScrollbarSize = s.ScrollbarSize;
    memcpy(bak.Colors, s.Colors, sizeof(s.Colors));
    return bak;
}

static void RestoreImGuiStyle(const StyleBackup& bak) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = bak.WindowRounding; s.FrameRounding = bak.FrameRounding; s.ChildRounding = bak.ChildRounding;
    s.PopupRounding = bak.PopupRounding; s.ScrollbarRounding = bak.ScrollbarRounding; s.GrabRounding = bak.GrabRounding; s.TabRounding = bak.TabRounding;
    s.WindowPadding = bak.WindowPadding; s.FramePadding = bak.FramePadding; s.ItemSpacing = bak.ItemSpacing; s.ItemInnerSpacing = bak.ItemInnerSpacing;
    s.CellPadding = bak.CellPadding; s.TouchExtraPadding = bak.TouchExtraPadding;
    s.IndentSpacing = bak.IndentSpacing; s.ScrollbarSize = bak.ScrollbarSize;
    memcpy(s.Colors, bak.Colors, sizeof(s.Colors));
}

static void ApplyModernUIStyle(float density) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 18.0f * density;
    s.ChildRounding     = 14.0f * density;
    s.FrameRounding     = 10.0f * density;
    s.PopupRounding     = 16.0f * density;
    s.ScrollbarRounding = 10.0f * density;
    s.GrabRounding      = 8.0f  * density;
    s.TabRounding       = 10.0f * density;

    s.WindowPadding     = ImVec2(20.0f * density, 16.0f * density);
    s.FramePadding      = ImVec2(12.0f * density, 7.0f  * density);
    s.ItemSpacing       = ImVec2(12.0f * density, 10.0f * density);
    s.ItemInnerSpacing  = ImVec2(8.0f  * density, 5.0f  * density);
    s.CellPadding       = ImVec2(10.0f * density, 7.0f  * density);
    s.TouchExtraPadding = ImVec2(6.0f  * density, 6.0f  * density);
    s.IndentSpacing     = 22.0f * density;
    s.ScrollbarSize     = 32.0f * density;   // 触摸热区 ≈44px (@1.0密度), 适配无障碍标准
    s.ScrollbarRounding = 16.0f * density;    // 圆角滚动条更顺滑
    s.AntiAliasedLines  = true;
    s.AntiAliasedFill   = true;

    s.Colors[ImGuiCol_Text]                 = g_theme.text;
    s.Colors[ImGuiCol_TextDisabled]         = g_theme.text_muted;
    s.Colors[ImGuiCol_WindowBg]             = g_theme.bg_dark;
    s.Colors[ImGuiCol_ChildBg]              = g_theme.bg_panel;
    s.Colors[ImGuiCol_PopupBg]               = g_theme.bg_card;
    s.Colors[ImGuiCol_Border]                = g_theme.border;
    s.Colors[ImGuiCol_BorderShadow]          = ImVec4(0,0,0,0);
    s.Colors[ImGuiCol_FrameBg]               = g_theme.bg_input;
    s.Colors[ImGuiCol_FrameBgHovered]        = g_theme.bg_hover;
    s.Colors[ImGuiCol_FrameBgActive]         = g_theme.bg_active;
    s.Colors[ImGuiCol_TitleBg]               = g_theme.bg_panel;
    s.Colors[ImGuiCol_TitleBgActive]         = g_theme.bg_panel;
    s.Colors[ImGuiCol_TitleBgCollapsed]      = g_theme.bg_panel;
    s.Colors[ImGuiCol_MenuBarBg]             = g_theme.bg_panel;
    s.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.025f, 0.025f, 0.035f, 0.40f);
    s.Colors[ImGuiCol_ScrollbarGrab]         = g_theme.border_strong;
    s.Colors[ImGuiCol_ScrollbarGrabHovered]  = g_theme.primary;
    s.Colors[ImGuiCol_ScrollbarGrabActive]   = g_theme.primary_active;
    s.Colors[ImGuiCol_CheckMark]             = g_theme.check_mark;
    s.Colors[ImGuiCol_SliderGrab]            = g_theme.slider_grab;
    s.Colors[ImGuiCol_SliderGrabActive]      = g_theme.slider_grab_active;
    s.Colors[ImGuiCol_Button]                = g_theme.primary;
    s.Colors[ImGuiCol_ButtonHovered]         = g_theme.primary_hover;
    s.Colors[ImGuiCol_ButtonActive]          = g_theme.primary_active;
    s.Colors[ImGuiCol_Header]                = g_theme.bg_hover;
    s.Colors[ImGuiCol_HeaderHovered]         = g_theme.bg_active;
    s.Colors[ImGuiCol_HeaderActive]          = g_theme.primary_active;
    s.Colors[ImGuiCol_Separator]             = g_theme.border;
    s.Colors[ImGuiCol_SeparatorHovered]      = g_theme.border_strong;
    s.Colors[ImGuiCol_SeparatorActive]       = g_theme.primary;
    s.Colors[ImGuiCol_ResizeGrip]            = g_theme.border_strong;
    s.Colors[ImGuiCol_ResizeGripHovered]     = g_theme.primary;
    s.Colors[ImGuiCol_ResizeGripActive]       = g_theme.primary_active;
    s.Colors[ImGuiCol_Tab]                   = g_theme.bg_card;
    s.Colors[ImGuiCol_TabHovered]            = g_theme.bg_hover;
    s.Colors[ImGuiCol_TabActive]             = g_theme.primary;
    s.Colors[ImGuiCol_TabUnfocused]          = g_theme.bg_card;
    s.Colors[ImGuiCol_TabUnfocusedActive]    = g_theme.bg_active;
    s.Colors[ImGuiCol_PlotLines]             = g_theme.primary;
    s.Colors[ImGuiCol_PlotLinesHovered]      = g_theme.primary_hover;
    s.Colors[ImGuiCol_PlotHistogram]         = g_theme.primary;
    s.Colors[ImGuiCol_PlotHistogramHovered]  = g_theme.primary_hover;
    s.Colors[ImGuiCol_TextSelectedBg]        = g_theme.primary_active;
    s.Colors[ImGuiCol_DragDropTarget]        = g_theme.warning;
    s.Colors[ImGuiCol_NavHighlight]          = g_theme.primary;
    s.Colors[ImGuiCol_NavWindowingHighlight] = g_theme.primary;
    s.Colors[ImGuiCol_NavWindowingDimBg]     = g_theme.bg_overlay;
    s.Colors[ImGuiCol_ModalWindowDimBg]      = g_theme.bg_overlay;
}

// 按钮变体：主按钮、成功、危险、次要
enum class ButtonVariant { Primary, Success, Danger, Secondary };

static bool StyledButton(const char* label, ButtonVariant variant = ButtonVariant::Primary, const ImVec2& size = ImVec2(0,0), float density = 1.0f) {
    ImVec4 bg, hover, active, text;
    switch (variant) {
        case ButtonVariant::Success:
            bg = g_theme.success; hover = g_theme.success_hover; active = g_theme.success_active; text = g_theme.text_on_primary;
            break;
        case ButtonVariant::Danger:
            bg = g_theme.danger; hover = g_theme.danger_hover; active = g_theme.danger_active; text = g_theme.text_on_primary;
            break;
        case ButtonVariant::Secondary:
            bg = g_theme.bg_card; hover = g_theme.bg_hover; active = g_theme.bg_active; text = g_theme.text;
            break;
        default:
            bg = g_theme.primary; hover = g_theme.primary_hover; active = g_theme.primary_active; text = g_theme.text_on_primary;
            break;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * density);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * density, 8.0f * density));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return clicked;
}

static void StyledSectionHeader(const char* label, const ImVec4& color = ImVec4(0,0,0,0), float density = 1.0f) {
    ImVec4 c = (color.w > 0.0f) ? color : g_theme.text_title;
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::PushFont(g_font_ui);
    ImGui::Text("%s", label);
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

static void StyledCardBegin(const char* id, const ImVec2& size, float density, ImDrawList* draw_list, ImVec2& out_pos, ImVec2& out_size) {
    ImGui::BeginChild(id, size, false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    out_pos = ImGui::GetWindowPos();
    out_size = ImGui::GetWindowSize();
    draw_list->AddRectFilled(out_pos, ImVec2(out_pos.x + out_size.x, out_pos.y + out_size.y),
                             IM_COL32(14, 15, 20, 230), 14.0f * density);
    draw_list->AddRect(out_pos, ImVec2(out_pos.x + out_size.x, out_pos.y + out_size.y),
                       IM_COL32(45, 50, 70, 120), 14.0f * density, 0, 1.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * density);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f * density);
}

static void StyledCardEnd() {
    ImGui::EndChild();
}

void Layout_tick_UI(bool *main_thread_flag) {
    if (!ImGui::GetCurrentContext() || !g_font_ui || !g_font_ui->IsLoaded()) return;
    px = static_cast<float>(displayInfo.width) * 0.5f;
    py = static_cast<float>(displayInfo.height) * 0.5f;
    screen_config();
    drawBegin();

    // GL 上下文丢失检测与恢复
    CheckGLContextLoss();
    RecoverFromGLContextLoss();

    Draw_Main_Optimized(ImGui::GetForegroundDrawList());
    AutoWoodCheck();

    const float base = static_cast<float>(std::min(displayInfo.width, displayInfo.height));
    const float g_density = std::clamp(std::sqrt(base) * 0.0045f, 0.8f, 2.0f);
    g_ui_density = g_density;

    static bool was_in_talent_view = false;
    if (g_talent_view) {
        if (!was_in_talent_view) g_talent_need_refresh = true;
        was_in_talent_view = true;
        show_talent_viewer();
    } else {
        was_in_talent_view = false;
    }

    static bool synced_threshold = false;
    if (!synced_threshold) {
        MjSubsystem::high_value_threshold = g_treasure_threshold;
        synced_threshold = true;
    }

    static char new_name[64] = "";
    static float new_music_x = 0.0f, new_music_y = 0.0f, new_music_z = 0.0f;
    static float new_piano_x = 0.0f, new_piano_y = 0.0f, new_piano_z = 0.0f;
    static char new_texture[128] = "/sdcard/maps/";

    static float ui_anim_scale = 0.0f;
    const float anim_speed_fast = 0.15f;
    const float anim_speed_slow = 0.17f;
    float target_scale = MemuSwitch ? 1.0f : 0.0f;
    float animSpeed = (ui_anim_scale < target_scale) ? anim_speed_fast : anim_speed_slow;
    float deltaTimeScaled = ImGui::GetIO().DeltaTime / animSpeed;
    ui_anim_scale = ImLerp(ui_anim_scale, target_scale, deltaTimeScaled);
    static bool theme_initialized = false;
    if (!theme_initialized) { InitModernUITheme(); theme_initialized = true; }
    StyleBackup style_bak = BackupImGuiStyle();
    ApplyModernUIStyle(g_density);

    if (ui_anim_scale > 0.01f) {
        ImDrawList *fg_draw_list = ImGui::GetForegroundDrawList();
        const char *watermark_text = "大米饭先生";
        float watermark_font_size = 45.0f * g_density;
        ImVec2 watermark_pos(80.0f, displayInfo.height - (watermark_font_size + 40.0f));
        fg_draw_list->AddText(g_font_ui, watermark_font_size, watermark_pos, IM_COL32(255, 50, 50, 100), watermark_text);

        const float win_w_final = std::min(base * 0.51f * 1.618f, displayInfo.width * 0.9f);
        const float win_h_final = std::min(win_w_final / 1.618f, displayInfo.height * 0.9f);
        float anim_win_w = win_w_final * ui_anim_scale;
        float anim_win_h = win_h_final * ui_anim_scale;
        ImVec2 anim_win_pos((displayInfo.width - anim_win_w) * 0.5f, (displayInfo.height - anim_win_h) * 0.5f);

        // 自定义窗口位置（支持拖动后保持）
        static ImVec2 g_custom_win_pos(0, 0);
        static bool g_custom_pos_set = false;
        ImVec2 final_win_pos = g_custom_pos_set ? g_custom_win_pos : anim_win_pos;

        ImGui::SetNextWindowPos(final_win_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(anim_win_w, anim_win_h), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(ui_anim_scale);
        ImGui::Begin("大米饭先生", main_thread_flag, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        const ImVec2 window_pos2 = ImGui::GetWindowPos();
        const ImVec2 window_size = ImGui::GetWindowSize();
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const float titlebar_height = ImGui::GetTextLineHeight() * 2.4f;

        // 现代标题栏：柔和渐变 + 微妙阴影
        draw_list->AddRectFilledMultiColor(window_pos2, ImVec2(window_pos2.x + window_size.x, window_pos2.y + titlebar_height),
                                           IM_COL32(18, 22, 35, 255), IM_COL32(25, 30, 48, 255), IM_COL32(25, 30, 48, 255), IM_COL32(18, 22, 35, 255));
        draw_list->AddRectFilled(ImVec2(window_pos2.x, window_pos2.y + titlebar_height),
                                ImVec2(window_pos2.x + window_size.x, window_pos2.y + titlebar_height + 4.0f * g_density),
                                IM_COL32(0, 0, 0, 60), 0.0f);
        draw_list->AddLine(ImVec2(window_pos2.x, window_pos2.y + titlebar_height),
                           ImVec2(window_pos2.x + window_size.x, window_pos2.y + titlebar_height),
                           IM_COL32(60, 100, 160, 180), 2.0f * g_density);

        const char *title = "大米饭先生";
        ImGui::PushFont(g_font_ui);
        const ImVec2 text_size_title = ImGui::CalcTextSize(title);
        ImVec2 title_text_pos(window_pos2.x + (window_size.x - text_size_title.x) * 0.5f, window_pos2.y + (titlebar_height - text_size_title.y) * 0.5f);
        draw_list->AddText(g_font_ui, ImGui::GetFontSize() * 1.15f, title_text_pos, IM_COL32(220, 230, 255, 255), title);
        ImGui::PopFont();

        // ========== 通过标题栏"大米饭先生"拖动窗口 ==========
        static bool is_dragging = false;
        static ImVec2 drag_start_offset;
        const ImVec2 mouse_pos_win = ImGui::GetMousePos();

        bool hit_titlebar = (mouse_pos_win.x >= window_pos2.x &&
                             mouse_pos_win.x <= window_pos2.x + window_size.x &&
                             mouse_pos_win.y >= window_pos2.y &&
                             mouse_pos_win.y <= window_pos2.y + titlebar_height);

        if (ImGui::IsMouseClicked(0) && hit_titlebar) {
            is_dragging = true;
            drag_start_offset = ImVec2(mouse_pos_win.x - window_pos2.x, mouse_pos_win.y - window_pos2.y);
            g_custom_pos_set = true; // 标记已设置自定义位置，不再回弹
        }

        if (is_dragging) {
            if (ImGui::IsMouseDown(0)) {
                ImVec2 new_pos = ImVec2(mouse_pos_win.x - drag_start_offset.x, mouse_pos_win.y - drag_start_offset.y);
                new_pos.x = std::clamp(new_pos.x, 0.0f, displayInfo.width - window_size.x);
                new_pos.y = std::clamp(new_pos.y, 0.0f, displayInfo.height - window_size.y);
                g_custom_win_pos = new_pos; // 保存到持久化变量，下一帧沿用此位置
                ImGui::SetWindowPos(new_pos);
            } else {
                is_dragging = false;
            }
        }
        struct NavItem { const char *label; const char *icon; };
        static const NavItem nav_items[] = {
                {"状态信息", "\xee\xa2\x80"}, {"功能设置", "\xee\xa4\x82"},
                {"自动盖板", "\xee\xa5\x85"}, {"模仿者",   "\xee\xa6\x83"},
                {"摸金模式", "\xee\xa8\x84"},
                {"地图管理", "\xee\xa9\x85"},
                {"关于",     "\xee\xa7\x81"},
        };
        static int current_tab = 0;

        float max_text_width = 0.0f;
        for (const auto& item : nav_items) {
            float w = ImGui::CalcTextSize(item.label).x;
            if (w > max_text_width) max_text_width = w;
        }
        const float btn_inner_padding = ImGui::GetStyle().FramePadding.x * 2.0f;
        const float indicator_space = 12.0f * g_density;
        const float sidebar_width = max_text_width + btn_inner_padding + indicator_space + 18.0f * g_density;
        const float content_x = sidebar_width + 12.0f * g_density;
        const float nav_start_y = titlebar_height + 16.0f * g_density;

        // 侧边栏卡片背景
        ImVec2 sidebar_pos(window_pos2.x + 10.0f * g_density, window_pos2.y + nav_start_y);
        ImVec2 sidebar_size(sidebar_width - 10.0f * g_density, window_size.y - nav_start_y - 20.0f * g_density);
        draw_list->AddRectFilled(sidebar_pos, ImVec2(sidebar_pos.x + sidebar_size.x, sidebar_pos.y + sidebar_size.y), IM_COL32(12, 14, 20, 220), 14.0f * g_density);
        draw_list->AddRect(sidebar_pos, ImVec2(sidebar_pos.x + sidebar_size.x, sidebar_pos.y + sidebar_size.y), IM_COL32(40, 45, 65, 120), 14.0f * g_density, 0, 1.5f);

        ImGui::SetCursorPos(ImVec2(8.0f * g_density, nav_start_y));
        ImGui::BeginChild("##Sidebar", ImVec2(sidebar_width, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * g_density);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 8.0f * g_density));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * g_density, 10.0f * g_density));
            for (int i = 0; i < IM_ARRAYSIZE(nav_items); i++) {
                bool is_active = (current_tab == i);
                ImVec4 bg_col = is_active ? g_theme.primary : g_theme.bg_card;
                ImVec4 hover_col = is_active ? g_theme.primary_hover : g_theme.bg_hover;
                ImVec4 active_col = is_active ? g_theme.primary_active : g_theme.bg_active;
                ImVec4 text_col = is_active ? g_theme.text_on_primary : g_theme.text;

                ImGui::PushStyleColor(ImGuiCol_Button, bg_col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover_col);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, active_col);
                ImGui::PushStyleColor(ImGuiCol_Text, text_col);
                if (ImGui::Button(nav_items[i].label, ImVec2(-1, 0))) current_tab = i;
                ImGui::PopStyleColor(4);

                if (is_active) {
                    ImVec2 btn_min = ImGui::GetItemRectMin();
                    ImVec2 btn_max = ImGui::GetItemRectMax();
                    float line_h = (btn_max.y - btn_min.y) * 0.55f;
                    float line_x = btn_min.x - 4.0f * g_density;
                    float line_y = btn_min.y + (btn_max.y - btn_min.y - line_h) * 0.5f;
                    ImGui::GetWindowDrawList()->AddRectFilled(
                            ImVec2(line_x, line_y),
                            ImVec2(line_x + 4.0f * g_density, line_y + line_h),
                            IM_COL32(140, 200, 255, 255), 2.0f * g_density);
                }
            }
            ImGui::PopStyleVar(3);
            ImGui::Dummy(ImVec2(0, 18.0f * g_density));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * g_density);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * g_density, 10.0f * g_density));
            ImGui::PushStyleColor(ImGuiCol_Button, g_theme.danger);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_theme.danger_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_theme.danger_active);
            ImGui::PushStyleColor(ImGuiCol_Text, g_theme.text_on_primary);
            if (ImGui::Button("退出脚本", ImVec2(-1, 0))) {
                *main_thread_flag = false;
                CloseDebugLog();
                SaveConfig();
            }
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
        }
        ImGui::EndChild();

        // 主内容区：卡片容器
        ImGui::SetCursorPos(ImVec2(content_x, nav_start_y));
        ImGui::BeginChild("MainContent", ImVec2(window_size.x - content_x - 14.0f * g_density, window_size.y - nav_start_y - 20.0f * g_density), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImDrawList *child_draw = ImGui::GetWindowDrawList();
        ImVec2 child_pos = ImGui::GetWindowPos();
        ImVec2 child_size = ImGui::GetWindowSize();
        child_draw->AddRectFilled(child_pos, ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y), IM_COL32(13, 14, 19, 245), 16.0f * g_density);
        child_draw->AddRect(child_pos, ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y), IM_COL32(45, 50, 70, 130), 16.0f * g_density, 0, 1.5f);
        child_draw->AddRectFilled(ImVec2(child_pos.x, child_pos.y), ImVec2(child_pos.x + child_size.x, child_pos.y + 3.0f * g_density), IM_COL32(50, 90, 150, 120), 16.0f * g_density);

        switch (current_tab) {
            case 0:
                StyledSectionHeader("系统信息", g_theme.text_title, g_density);
                ImGui::Text("渲染模式: %s", graphics->RenderName);
                ImGui::Text("GUI 版本: %s", IMGUI_VERSION);
                ImGui::Text("帧率: %.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
                ImGui::Spacing();
                StyledSectionHeader("数据状态", g_theme.text_title, g_density);
                if (GlobalMemory::状态 == 2) ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "✓ 数据已就绪");
                else if (GlobalMemory::状态 == 1) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), ":) 正在初始化...");
                else ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "✗ 未获取数据");
                ImGui::Spacing();
                StyledSectionHeader("游戏信息", g_theme.text_title, g_density);
                ImGui::Text("进程 ID: %d", pid.load()); ImGui::SameLine();
                ImGui::Text("包名: %s", extractedString);
                ImGui::Spacing();
                StyledSectionHeader("内存地址", g_theme.text_title, g_density);
                ImGui::Text("模块基址: 0x%lx", GlobalMemory::libbase); ImGui::SameLine();
                ImGui::Text("对象数组: 0x%lx", GlobalMemory::Arrayaddr);
                ImGui::Text("矩阵地址: 0x%lx", GlobalMemory::Matrix);
                ImGui::Spacing();
                StyledSectionHeader("偏移与统计", g_theme.text_title, g_density);
                ImGui::Text("矩阵偏移: 0x%lx", GlobalMemory::MatrixOffset); ImGui::SameLine();
                ImGui::Text("数组偏移: 0x%lx", GlobalMemory::ArrayaddrOffset);
                ImGui::Text("模块页数: %ld", GlobalMemory::ModulePagesCount); ImGui::SameLine();
                ImGui::Text("对象数量: %d", GlobalMemory::数量);
                ImGui::Spacing();
                StyledSectionHeader("角色与坐标", g_theme.text_title, g_density);
                ImGui::Text("监管者: %s", 监管者预知);
                ImGui::Text("自身坐标: X:%.1f, Y:%.1f, Z:%.1f", Z.X, Z.Y, Z.Z);
                break;
            case 1:
                StyledSectionHeader("绘制选项", g_theme.text_title, g_density);
                ImGui::Columns(2, "draw_cols", true);
                ImGui::Checkbox("增强框体", &show_draw_EnhancedFrame);
                ImGui::Checkbox("绘制道具", &show_draw_Prop);
                ImGui::Checkbox("查看天赋", &g_talent_view);
                ImGui::ColorEdit3("求生者方框颜色", (float*)&g_BoxColor_Survivor);
                ImGui::ColorEdit3("监管者方框颜色", (float*)&g_BoxColor_Hunter);
                ImGui::ColorEdit3("幽灵方框颜色", (float*)&g_BoxColor_Ghost);
                ImGui::NextColumn();
                ImGui::Checkbox("人物射线", &show_draw_Line);
                ImGui::Checkbox("绘制名字", &show_draw_Name);
                ImGui::Checkbox("绘制密码机", &show_draw_sender);
                ImGui::Checkbox("预知监管", &show_draw_prophet);
                if (ImGui::Checkbox("红夫人模式", &show_draw_redqueen)) {
                    if (show_draw_redqueen) disable_skip_filter = true;
                }
                ImGui::Columns(1);
                ImGui::Spacing(); ImGui::Separator();
                StyledSectionHeader("场景对象距离", g_theme.text_title, g_density);
                ImGui::SliderInt("椅子距离", &g_chair_dist, 10, 100);
                ImGui::SliderInt("板子距离", &g_board_dist, 10, 100);
                ImGui::SliderInt("箱子距离", &g_box_dist, 10, 100);
                if (StyledButton("一键重置", ButtonVariant::Secondary, ImVec2(0,0), g_density)) { g_chair_dist = 40; g_board_dist = 40; g_box_dist = 30; }
                ImGui::Spacing(); ImGui::Separator();
                StyledSectionHeader("场景对象开关", g_theme.text_title, g_density);
                ImGui::Checkbox("椅子", &show_draw_Chair); ImGui::SameLine();
                ImGui::Checkbox("板子", &show_draw_BANZI); ImGui::SameLine();
                ImGui::Checkbox("道具箱", &show_draw_BoxItem);
                ImGui::Spacing(); ImGui::Separator();
                StyledSectionHeader("调试与过滤", g_theme.text_title, g_density);
                ImGui::Checkbox("显示幽灵/残影", &inform_ghost); ImGui::SameLine();
                if (ImGui::Checkbox("调试模式", &Debugging)) {
                    if (Debugging) OpenDebugLog();
                    else CloseDebugLog();
                }
                ImGui::Checkbox("无视过滤", &disable_skip_filter);
                if (ImGui::Button("清理缓存")) {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    data_buffers[0].clear(); data_buffers[1].clear();
                    GlobalMemory::数量 = 0; 监管者预知[0] = '\0';
                    std::lock_guard<std::mutex> mimic_lock(mimic_mutex);
                    global_validRoles.clear(); bound_seat_by_class.clear();
                }
                ImGui::SameLine();
                if (ImGui::Button("打印全场坐标")) {
                    int current_idx = front_buffer_idx.load(std::memory_order_acquire);
                    const auto &current_data = data_buffers[current_idx];
                    int print_count = 0;
                    printf("\n================ 场景内所有对象详细数据 ================\n");
                    for (const auto &item : current_data) {
                        if (item.阵营 != 1 && item.阵营 != 2) continue;
                        Vector3A pos = getObjectCoordinates(item.objcoor, false);
                        if (isValidCoordinate(pos)) {
                            printf("[阵营:%d] 别名: %s | 类名: %s\n", item.阵营, item.str[0]!='\0' ? item.str : "无", item.类名[0]!='\0' ? item.类名 : "未知");
                            printf("  -> 坐标: X: %.2f, Y: %.2f, Z: %.2f\n", pos.X, pos.Y, pos.Z);
                            printf("  -> 调试: Obj: 0x%lx | Act: %d | 特征: 0x%x | 状态: %.1f | 幽灵: %s\n", item.obj, item.action, item.实体特征码, item.状态数值, item.is_ghost ? "Yes" : "No");
                            print_count++;
                        }
                    }
                    printf("共计输出 %d 个对象。\n", print_count);
                    fflush(stdout);
                }
                break;
            case 2:
                StyledSectionHeader("自动盖板设置", g_theme.text_title, g_density);
                ImGui::Checkbox("启用自动盖板", &wood_enabled); ImGui::SameLine();
                if (StyledButton("测试触摸", ButtonVariant::Secondary, ImVec2(0,0), g_density)) { SimulateClick(wood_touch_x, wood_touch_y); }
                ImGui::SameLine(); ImGui::Checkbox("显示触摸点", &show_touch_point);
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_muted, "交互键坐标");
                ImGui::SliderFloat("X 坐标", &wood_touch_x, 0.0f, (float)displayInfo.width);
                ImGui::SliderFloat("Y 坐标", &wood_touch_y, 0.0f, (float)displayInfo.height);
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_muted, "判定参数");
                ImGui::SliderFloat("触发距离(米)", &wood_trigger_dist, 5.0f, 40.0f);
                ImGui::SliderFloat("判定长度", &wood_length, 5.0f, 30.0f);
                ImGui::SliderFloat("判定宽度", &wood_width, 3.0f, 20.0f);
                ImGui::Spacing();
                ImGui::TextColored(g_theme.warning, "提示：先测试触摸，确认交互键有反应后再开启");
                break;
            case 3:
                StyledSectionHeader("模仿者模式扫描", g_theme.text_title, g_density);
                if (ImGui::Checkbox("启用模仿者识别", &g_MimicModeEnabled)) {
                    if (!g_MimicModeEnabled) {
                        std::lock_guard<std::mutex> lock(mimic_mutex);
                        global_validRoles.clear(); bound_seat_by_class.clear();
                        if (is_scanning_mimic.load()) is_scanning_mimic.store(false);
                    }
                }
                if (is_scanning_mimic.load()) {
                    ImGui::TextColored(g_theme.warning, "正在深度扫描内存中...");
                } else {
                    if (StyledButton("扫描对局身份", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                        is_scanning_mimic.store(true);
                        std::thread([]() {
                            cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(7, &cpuset);
                            sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
                            scan_mimic_roles();
                            is_scanning_mimic.store(false);
                        }).detach();
                    }
                    ImGui::SameLine();
                    if (StyledButton("清空上局数据", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                        std::lock_guard<std::mutex> lock(mimic_mutex);
                        global_validRoles.clear(); bound_seat_by_class.clear();
                    }
                    ImGui::SameLine(); ImGui::Checkbox("显示独立悬浮窗", &show_mimic_overlay);
                }
                ImGui::Spacing(); ImGui::Separator();
                StyledSectionHeader("扫描结果", g_theme.text_title, g_density);
                {
                    std::lock_guard<std::mutex> lock(mimic_mutex);
                    if (global_validRoles.empty()) {
                        ImGui::Text("暂无数据或尚未扫描。");
                    } else {
                        for (const auto &r : global_validRoles) {
                            std::string roleName = RoleIdToChinese(r.roleId);
                            const char *campStr = r.campId == 1 ? "侦探团" : (r.campId == 2 ? "狼人" : "神秘客");
                            ImVec4 color;
                            if (r.campId == 1) color = ImVec4(0.2f, 0.8f, 0.8f, 1.0f);
                            else if (r.campId == 2) color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                            else color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                            ImGui::TextColored(color, "[%02d]号 | 阵营: %-6s | 身份: %s", r.index + 1, campStr, roleName.c_str());
                        }
                    }
                }
                break;
            case 4:  // 摸金模式
            {
                StyledSectionHeader("摸金模式", g_theme.text_title, g_density);
                if (ImGui::Checkbox("摸金模式", &MjSubsystem::draw_props)) {
                    if (MjSubsystem::draw_props) {
                        disable_skip_filter = true;
                        inform_ghost = true;
                    }
                }
                ImGui::SameLine();
                ImGui::Checkbox("显示距离", &MjSubsystem::show_distance);
                ImGui::SameLine();
                ImGui::SliderFloat("高价值阈值", &g_treasure_threshold, 1000.0f, 20000.0f, "%.0f");
                if (g_treasure_threshold != MjSubsystem::high_value_threshold) {
                    MjSubsystem::high_value_threshold = g_treasure_threshold;
                }

                if (ImGui::CollapsingHeader("精细过滤")) {
                    ImGui::Checkbox("怪物", &MjSubsystem::show_monsters); ImGui::SameLine();
                    ImGui::Checkbox("紫/金宝箱", &MjSubsystem::show_big_chest); ImGui::SameLine();
                    ImGui::Checkbox("小箱子", &MjSubsystem::show_small_chest);
                    ImGui::Checkbox("陷阱/夹子/碎石", &MjSubsystem::show_traps); ImGui::SameLine();
                    ImGui::Checkbox("门/板/钢琴/花瓶", &MjSubsystem::show_interactables);
                    ImGui::Checkbox("高价值物品", &MjSubsystem::show_high_value); ImGui::SameLine();
                    ImGui::Checkbox("低价值物品", &MjSubsystem::show_low_value);
                }

                if (ImGui::CollapsingHeader("各类最大显示距离")) {
                    ImGui::SliderFloat("怪物", &MjSubsystem::max_dist_monsters, 5.0f, 500.0f, "%.0f m");
                    ImGui::SliderFloat("紫/金宝箱", &MjSubsystem::max_dist_big_chest, 5.0f, 500.0f, "%.0f m");
                    ImGui::SliderFloat("小箱子", &MjSubsystem::max_dist_small_chest, 5.0f, 500.0f, "%.0f m");
                    ImGui::SliderFloat("陷阱/夹子", &MjSubsystem::max_dist_traps, 5.0f, 500.0f, "%.0f m");
                    ImGui::SliderFloat("交互物", &MjSubsystem::max_dist_interactables, 5.0f, 500.0f, "%.0f m");
                    ImGui::SliderFloat("高价值物品", &MjSubsystem::max_dist_high_value, 5.0f, 500.0f, "%.0f m");
                    ImGui::SliderFloat("低价值物品", &MjSubsystem::max_dist_low_value, 5.0f, 500.0f, "%.0f m");
                }

                ImGui::TextColored(g_theme.text_muted, "颜色: 紫宝箱(紫) 金宝箱(金) 高价(粉) 怪物(红) 其他见过滤");
            }
                break;
            case 5:  // 地图管理（已在前面替换为完整新代码）
            {
                StyledSectionHeader("地图管理", g_theme.text_title, g_density);

                // === ★ 一键设为新地图（始终可见的快捷按钮） ===
                ImGui::PushStyleColor(ImGuiCol_Button, g_theme.success);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_theme.success_hover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_theme.success_active);
                ImGui::PushStyleColor(ImGuiCol_Text, g_theme.text_on_primary);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f * g_density);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f * g_density, 14.0f * g_density));
                // 宽高用0让ImGui根据内容自动适配 + 加大填充确保文字完整
                ImGui::SetNextItemWidth(-1); // 占满整行
                if (ImGui::Button("★ 注册当前场景为新地图", ImVec2(0, 0))) {
                    if (g_detected_musicbox_pos.X != 0.0f || g_detected_musicbox_pos.Y != 0.0f) {
                        new_music_x = g_detected_musicbox_pos.X;
                        new_music_y = g_detected_musicbox_pos.Y;
                        new_music_z = g_detected_musicbox_pos.Z;
                    } else if (Z.X != 0.0f || Z.Y != 0.0f) {
                        new_music_x = Z.X;
                        new_music_y = Z.Y;
                        new_music_z = Z.Z;
                    }
                    // 同步检测钢琴位置（第二信号源）
                    if (g_detected_piano_pos.X != 0.0f || g_detected_piano_pos.Y != 0.0f) {
                        new_piano_x = g_detected_piano_pos.X;
                        new_piano_y = g_detected_piano_pos.Y;
                        new_piano_z = g_detected_piano_pos.Z;
                    } else {
                        new_piano_x = new_piano_y = new_piano_z = 0.0f;
                    }
                    int next_id = (int)g_all_maps.size() + 1;
                    snprintf(new_name, sizeof(new_name), "地图%d 一楼", next_id);
                    snprintf(new_texture, sizeof(new_texture), "/sdcard/maps/map%d_floor1.png", next_id);
                    ImGui::OpenPopup("添加新地图##detected");
                }
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(4);

                bool map_invalid = (g_current_map_index < 0 || g_current_map_index >= (int)g_all_maps.size() ||
                                    g_all_maps[g_current_map_index].empty());

                if (map_invalid) {
                    ImGui::TextColored(g_theme.warning, "尚未识别地图，部分功能不可用");
                    ImGui::TextColored(g_theme.text_muted, "%s", g_map_scores_buf);
                }

                ImGui::Checkbox("启用导航地图", &g_map_enabled);
                ImGui::SameLine();
                ImGui::Checkbox("自动识别", &g_map_auto_detect);
                ImGui::SameLine();
                ImGui::Checkbox("路线规划", &g_show_nav_line);

                if (!map_invalid && !g_map_auto_detect) {
                    ImGui::SameLine();
                    std::vector<const char*> manual_names;
                    for (auto& floor_vec : g_all_maps) {
                        if (!floor_vec.empty() && floor_vec[0].name && strlen(floor_vec[0].name) > 0)
                            manual_names.push_back(floor_vec[0].name);
                    }
                    if (!manual_names.empty()) {
                        int manual_idx = g_current_map_index;
                        if (manual_idx < 0 || manual_idx >= (int)manual_names.size()) manual_idx = (int)manual_names.size() - 1;
                        ImGui::SetNextItemWidth(120);
                        if (ImGui::Combo("手动选择", &manual_idx, manual_names.data(), manual_names.size())) {
                            if (manual_idx >= 0 && manual_idx < (int)g_all_maps.size()) {
                                g_current_map_index = manual_idx;
                                g_current_floor_index = 0;
                                LoadMapTexture(g_current_map_index, 0);
                            }
                        }
                    } else {
                        ImGui::TextColored(ImVec4(1,0,0,1), "无地图配置");
                    }
                }

                if (!map_invalid) {
                    bool unknown_map = false;
                    if (g_map_enabled && g_map_auto_detect) {
                        bool found_in_db = false;
                        for (auto& mb : g_musicbox_db) {
                            if (mb.mapIndex == g_current_map_index) {
                                found_in_db = true;
                                break;
                            }
                        }
                        if (!found_in_db) unknown_map = true;
                    }

                    if (unknown_map) {
                        ImGui::TextColored(g_theme.warning, "未知地图 (自动识别未匹配)");
                        if (g_detected_musicbox_pos.X != 0.0f || g_detected_musicbox_pos.Y != 0.0f) {
                            ImGui::SameLine();
                            if (StyledButton("以此坐标添加新地图", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                                ImGui::SetNextWindowFocus();
                                new_music_x = g_detected_musicbox_pos.X;
                                new_music_y = g_detected_musicbox_pos.Y;
                                new_music_z = g_detected_musicbox_pos.Z;
                                new_piano_x = g_detected_piano_pos.X;
                                new_piano_y = g_detected_piano_pos.Y;
                                new_piano_z = g_detected_piano_pos.Z;
                                int next_id = (int)g_all_maps.size() + 1;
                                snprintf(new_name, sizeof(new_name), "地图%d 一楼", next_id);
                                snprintf(new_texture, sizeof(new_texture), "/sdcard/maps/map%d_floor1.png", next_id);
                                ImGui::OpenPopup("添加新地图##detected");
                            }
                        }
                    } else if (g_current_map_index >= 0 && g_current_map_index < (int)g_all_maps.size() && !g_all_maps[g_current_map_index].empty()) {
                        int dispFloor = SafeClampFloorIdx(g_current_map_index, g_current_floor_index);
                        ImGui::TextColored(g_theme.success, "当前: %s", g_all_maps[g_current_map_index][dispFloor].name);
                        ImGui::SameLine();
                        ImGui::TextColored(g_theme.text_muted, " [idx=%d tex=%s]",
                            g_current_map_index,
                            g_all_maps[g_current_map_index][dispFloor].texturePath);
                        ImGui::TextColored(g_theme.text_muted, "%s", g_map_scores_buf);

                        // === 音乐盒被移动到了同地图其他位置 ===
                        if (g_musicbox_moved_on_same_map && g_map_auto_detect) {
                            ImGui::TextColored(g_theme.warning,
                                "⚠ 音乐盒位置已改变 (可能被拾取后重新放置)");
                            if (g_detected_musicbox_pos.X != 0.0f || g_detected_musicbox_pos.Y != 0.0f) {
                                ImGui::SameLine();
                                if (StyledButton("更新音乐盒坐标", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                                    g_musicbox_moved_on_same_map = false;
                                    new_music_x = g_detected_musicbox_pos.X;
                                    new_music_y = g_detected_musicbox_pos.Y;
                                    new_music_z = g_detected_musicbox_pos.Z;
                                    new_piano_x = g_detected_piano_pos.X;
                                    new_piano_y = g_detected_piano_pos.Y;
                                    new_piano_z = g_detected_piano_pos.Z;
                                    // 使用当前地图名称
                                    snprintf(new_name, sizeof(new_name), "%s",
                                        g_all_maps[g_current_map_index][g_current_floor_index].name);
                                    snprintf(new_texture, sizeof(new_texture), "%s",
                                        g_all_maps[g_current_map_index][g_current_floor_index].texturePath);
                                    ImGui::OpenPopup("添加新地图##detected");
                                }
                            }
                            ImGui::Separator();
                        }

                        // === 同步场景物体到当前地图（钢琴+凳子）===
                        ImGui::TextColored(g_theme.text_muted, "[P][C] 钢琴:%s | 凳子:%zu",
                            (g_detected_piano_pos.X != 0.0f ? "OK" : "--"),
                            g_detected_chairs.size());
                        ImGui::SameLine();
                        if (StyledButton("[同步] 物体到当前地图", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                            SaveSceneObjectsToJSON(g_current_map_index, g_current_floor_index);
                            AddNotification("已同步: 钢琴 + " + std::to_string(g_detected_chairs.size()) + "个凳子",
                                3.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                        }
                    } else {
                        ImGui::TextColored(g_theme.danger, "地图索引无效");
                    }

                    ImGui::TextColored(g_theme.warning, "纹理: %s", g_texture_status);

                    ImGui::SliderFloat("尺寸", &g_map_display_size, 100, 1800, "%.0f");
                    ImGui::SliderFloat("位置X", &g_map_pos_x, 0, displayInfo.width, "%.0f");
                    ImGui::SliderFloat("位置Y", &g_map_pos_y, 0, displayInfo.height, "%.0f");
                    ImGui::SliderFloat("标签大小", &g_map_label_scale, 0.2f, 0.8f, "%.2f");

                    ImGui::Separator();

                    StyledSectionHeader("路线规划筛选", g_theme.text_title, g_density);
                    ImGui::PushItemWidth(100);
                    ImGui::InputInt("最低价值", &g_route_min_value, 100);
                    if (g_route_min_value < 0) g_route_min_value = 0;
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (StyledButton("全部", ButtonVariant::Secondary, ImVec2(0,0), g_density)) g_route_min_value = 0;
                    ImGui::SameLine();
                    if (StyledButton("≥10000", ButtonVariant::Secondary, ImVec2(0,0), g_density)) g_route_min_value = 10000;
                    ImGui::SameLine();
                    if (StyledButton("≥50000", ButtonVariant::Secondary, ImVec2(0,0), g_density)) g_route_min_value = 50000;

                    if (g_show_nav_line) {
                        ImGui::Spacing();
                        ImGui::TextColored(g_theme.info, "操作提示: 勾选「路线规划」后，在小地图上点击物品即可标记为导航目标");
                        if (g_priority_items.empty()) {
                            ImGui::TextColored(g_theme.text_muted, "当前未选中任何物品，请点击地图上的物品标记路线");
                        } else {
                            ImGui::TextColored(g_theme.success, "已标记 %zu 个导航目标，点击已标记物品可取消", g_priority_items.size());
                        }
                    }

                    ImGui::Separator();
                }

                if (ImGui::CollapsingHeader("透明度设置")) {
                    ImGui::SliderFloat("地图底图", &g_map_opacity, 0.1f, 1.0f);
                    ImGui::SliderFloat("物品标签", &g_label_opacity, 0.1f, 1.0f);
                    ImGui::SliderFloat("自身标记", &g_self_opacity, 0.1f, 1.0f);
                    ImGui::SliderFloat("路线", &g_route_opacity, 0.1f, 1.0f);
                    ImGui::SliderFloat("已保存路径", &g_saved_path_opacity, 0.1f, 1.0f);
                }

                if (!map_invalid) {
                    ImGui::Separator();

                    StyledSectionHeader("出口管理", g_theme.text_title, g_density);
                    if (StyledButton("标记当前点为出口", ButtonVariant::Success, ImVec2(0,0), g_density)) {
                        if (g_current_map_index >= 0 && g_current_floor_index >= 0 && Z.X != 0) {
                            while (g_exits.size() <= g_current_map_index) g_exits.push_back({});
                            while (g_exits[g_current_map_index].size() <= g_current_floor_index)
                                g_exits[g_current_map_index].push_back({});
                            g_exits[g_current_map_index][g_current_floor_index].push_back(Z);
                            SaveExitsToJSON(g_current_map_index, g_current_floor_index);
                            g_pathGraph.dirty = true;
                            AddNotification("出口已标记", 2.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                        }
                    }
                    ImGui::SameLine();
                    if (StyledButton("清除本层出口", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                        ImGui::OpenPopup("确认清除出口");
                    }
                    if (ImGui::BeginPopupModal("确认清除出口", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("确定要清除本层所有出口标记吗？此操作不可撤销。");
                        if (ImGui::Button("确定")) {
                            if (g_current_map_index < g_exits.size() &&
                                g_current_floor_index < g_exits[g_current_map_index].size()) {
                                g_exits[g_current_map_index][g_current_floor_index].clear();
                                SaveExitsToJSON(g_current_map_index, g_current_floor_index);
                                g_pathGraph.dirty = true;
                                AddNotification("出口已清除", 2.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    ImGui::TextColored(g_theme.text_muted, "提示：已校准地图可在小地图上长按标记出口");

                    ImGui::Separator();

                    StyledSectionHeader("路径绘制", g_theme.text_title, g_density);
                    auto& cfg_path = g_all_maps[g_current_map_index][g_current_floor_index];
                    if (cfg_path.calibrated) {
                        ImGui::Checkbox("显示已绘制路径", &g_show_saved_paths);
                        ImGui::SliderFloat("平滑强度", &g_smooth_strength, 0.0f, 20.0f, "%.0f px");
                        // 网格吸附功能已移除，路径点定位在用户实际点击的精确位置
                        ImGui::Checkbox("显示网格参考线", &g_show_grid);
                        if (g_show_grid) {
                            ImGui::SliderFloat("网格透明度", &g_grid_alpha, 0.1f, 1.0f);
                        }
                        ImGui::Separator();
                        if (StyledButton("开始绘制路径", ButtonVariant::Primary, ImVec2(0,0), g_density)) {
                            g_path_edit_mode = 1;
                            g_current_drawing_path.clear();
                        }
                        ImGui::SameLine();
                        if (StyledButton("取消绘制", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                            g_path_edit_mode = 0;
                            g_current_drawing_path.clear();
                        }
                        ImGui::SameLine();
                        if (StyledButton("清除所有路径", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                            ImGui::OpenPopup("确认清除路径");
                        }
                    } else {
                        ImGui::TextColored(g_theme.warning, "请先校准地图才能绘制路径");
                    }
                    if (ImGui::BeginPopupModal("确认清除路径", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("确定要清除所有手动画线路径吗？此操作不可撤销。");
                        if (StyledButton("确定", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                            g_saved_paths.clear();
                            g_current_drawing_path.clear();
                            g_path_edit_mode = 0;
                            SavePlayerPathsToJSON(g_current_map_index, g_current_floor_index);
                            g_pathGraph.dirty = true;
                            AddNotification("路径已清除", 2.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (StyledButton("取消", ButtonVariant::Secondary, ImVec2(0,0), g_density)) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    if (g_path_edit_mode == 1) {
                        ImGui::SameLine();
                        if (StyledButton("撤销上一个点", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                            if (!g_current_drawing_path.empty()) {
                                g_current_drawing_path.pop_back();
                            }
                        }
                    }
                    // 选中路径操作
                    if (g_selected_path_index >= 0 && g_selected_path_index < (int)g_saved_paths.size()) {
                        if (StyledButton("删除选中路径", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                            g_saved_paths.erase(g_saved_paths.begin() + g_selected_path_index);
                            g_selected_path_index = -1;
                            SavePlayerPathsToJSON(g_current_map_index, g_current_floor_index);
                            g_pathGraph.dirty = true;
                            AddNotification("选中路径已删除", 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                        }
                        ImGui::SameLine();
                        ImGui::TextColored(g_theme.warning, "路径 #%d 已选中 (Delete键删除)", g_selected_path_index);
                    }
                    ImGui::TextColored(g_theme.text_muted, "长按地图拖动绘制路径，松开保存（自由模式：任意方向平滑弯曲）");
                    if (g_path_edit_mode == 1) {
                        ImGui::TextColored(g_theme.info,
                            g_ortho_draw ? "✓ 正交模式：强制水平/垂直线段" : "● 自由模式：任意方向平滑曲线");
                    }

                    ImGui::Separator();
                    StyledSectionHeader("路径绘制选项", g_theme.text_title, g_density);
                    ImGui::Checkbox("正交绘制 (水平/垂直)", &g_ortho_draw);
                    ImGui::SliderFloat("平滑度", &g_smooth_strength, 1.0f, 30.0f, "%.1f");

                    ImGui::Separator();
                    StyledSectionHeader("路径显示", g_theme.text_title, g_density);
                    ImGui::Checkbox("显示已保存路径", &g_show_saved_paths);
                    ImGui::SameLine();
                    if (StyledButton(g_show_saved_paths ? "隐藏" : "显示", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                        g_show_saved_paths = !g_show_saved_paths;
                    }
                    if (g_show_saved_paths) {
                        ImGui::SliderFloat("路径透明度", &g_saved_path_opacity, 0.1f, 1.0f, "%.2f");
                    }

                    ImGui::Checkbox("显示通行网络 (调试)", &g_show_graph_debug);
                    if (g_show_graph_debug) {
                        ImGui::SameLine();
                        if (StyledButton("强制重建网络", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                            g_pathGraph.dirty = true;
                        }
                    }

                    ImGui::Separator();

                    if (ImGui::CollapsingHeader("地图校准", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("启用校准", &g_use_calib);
                        if (g_use_calib) {
                            if (ImGui::CollapsingHeader("校准步骤说明", ImGuiTreeNodeFlags_None)) {
                                ImGui::TextWrapped(
                                        "1. 站在可辨认位置，点按钮记录点1/点2\n"
                                        "2. 参照游戏小地图，拖动（橙色/蓝色标记）到纹理图对应位置\n"
                                        "3. 第二个点与第一个点必须有一段距离（两点距离要远）\n"
                                        "4. 完成上述步骤点击\"自动计算\"并保存\n"
                                        "（若已知音乐盒纹理位置，可直接\"填入预设\"）\n"
                                        "（若这些文字让您的CPU过载，建议您与手机一同进入休眠模式，对彼此都好）"
                                );
                            }
                            ImGui::Separator();

                            auto& floors = g_all_maps[g_current_map_index];
                            if (!floors.empty()) {
                                auto& cfg = floors[g_current_floor_index];
                                if (cfg.calibrated) {
                                    g_map_scale_x = cfg.scaleX; g_map_scale_y = cfg.scaleY;
                                    g_map_offset_u = cfg.offsetU; g_map_offset_v = cfg.offsetV;
                                    g_map_flip_x = cfg.flipX; g_map_flip_y = cfg.flipY;
                                }
                            }
                            ImGui::TextColored(g_theme.success, "拖拽地图上的橙色/蓝色标记来校准位置");

                            ImGui::TextColored(g_theme.text_muted, "校准点1：音乐盒位置");
                            if (StyledButton("填入预设音乐盒位置", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                                MusicboxKey* found = nullptr;
                                for (auto& mb : g_musicbox_db) {
                                    if (mb.mapIndex == g_current_map_index) {
                                        if (mb.floorIndex == g_current_floor_index) { found = &mb; break; }
                                        if (!found) found = &mb;
                                    }
                                }
                                if (found) { g_pt1_wx = found->x; g_pt1_wy = found->y; g_pt1_tu = found->texU; g_pt1_tv = found->texV; }
                            }
                            if (StyledButton("将我当前位置设为点1 (音乐盒)", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                                g_pt1_wx = Z.X;
                                g_pt1_wy = Z.Y;
                            }
                            ImGui::DragFloat("世界X1", &g_pt1_wx, 1.0f, -50000, 50000, "%.1f");
                            ImGui::DragFloat("世界Y1", &g_pt1_wy, 1.0f, -50000, 50000, "%.1f");

                            ImGui::PushItemWidth(80);
                            ImGui::DragFloat("纹理U1", &g_pt1_tu, 0.001f, -2.0f, 2.0f, "%.4f");
                            ImGui::PopItemWidth();
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("◀##u1", ImVec2(40, 40))) {
                                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                                }
                                g_pt1_tu -= 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_u1l += ImGui::GetIO().DeltaTime;
                                if (hold_u1l > 0.3f) g_pt1_tu -= 0.002f;
                            } else { hold_u1l = 0; }
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▶##u1", ImVec2(40, 40))) {
                                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                                }
                                g_pt1_tu += 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_u1r += ImGui::GetIO().DeltaTime;
                                if (hold_u1r > 0.3f) g_pt1_tu += 0.002f;
                            } else { hold_u1r = 0; }

                            ImGui::PushItemWidth(80);
                            ImGui::DragFloat("纹理V1", &g_pt1_tv, 0.001f, -2.0f, 2.0f, "%.4f");
                            ImGui::PopItemWidth();
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▲##v1", ImVec2(40, 40))) {
                                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                                }
                                g_pt1_tv -= 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_v1u += ImGui::GetIO().DeltaTime;
                                if (hold_v1u > 0.3f) g_pt1_tv -= 0.002f;
                            } else { hold_v1u = 0; }
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▼##v1", ImVec2(40, 40))) {
                                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                                }
                                g_pt1_tv += 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_v1d += ImGui::GetIO().DeltaTime;
                                if (hold_v1d > 0.3f) g_pt1_tv += 0.002f;
                            } else { hold_v1d = 0; }

                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("↩##undo1", ImVec2(40, 40)) && !g_pt1_history.empty()) {
                                auto prev = g_pt1_history.back();
                                g_pt1_history.pop_back();
                                g_pt1_tu = prev.first;
                                g_pt1_tv = prev.second;
                            }

                            ImGui::Separator();

                            ImGui::TextColored(g_theme.text_muted, "校准点2：大门位置");
                            if (StyledButton("将我当前位置设为点2 (大门)", ButtonVariant::Secondary, ImVec2(0,0), g_density)) {
                                g_pt2_wx = Z.X;
                                g_pt2_wy = Z.Y;
                            }
                            ImGui::DragFloat("世界X2", &g_pt2_wx, 1.0f, -50000, 50000, "%.1f");
                            ImGui::DragFloat("世界Y2", &g_pt2_wy, 1.0f, -50000, 50000, "%.1f");

                            ImGui::PushItemWidth(80);
                            ImGui::DragFloat("纹理U2", &g_pt2_tu, 0.001f, -2.0f, 2.0f, "%.4f");
                            ImGui::PopItemWidth();
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("◀##u2", ImVec2(40, 40))) {
                                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                                }
                                g_pt2_tu -= 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_u2l += ImGui::GetIO().DeltaTime;
                                if (hold_u2l > 0.3f) g_pt2_tu -= 0.002f;
                            } else { hold_u2l = 0; }
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▶##u2", ImVec2(40, 40))) {
                                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                                }
                                g_pt2_tu += 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_u2r += ImGui::GetIO().DeltaTime;
                                if (hold_u2r > 0.3f) g_pt2_tu += 0.002f;
                            } else { hold_u2r = 0; }

                            ImGui::PushItemWidth(80);
                            ImGui::DragFloat("纹理V2", &g_pt2_tv, 0.001f, -2.0f, 2.0f, "%.4f");
                            ImGui::PopItemWidth();
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▲##v2", ImVec2(40, 40))) {
                                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                                }
                                g_pt2_tv -= 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_v2u += ImGui::GetIO().DeltaTime;
                                if (hold_v2u > 0.3f) g_pt2_tv -= 0.002f;
                            } else { hold_v2u = 0; }
                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("▼##v2", ImVec2(40, 40))) {
                                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                                }
                                g_pt2_tv += 0.001f;
                            }
                            if (ImGui::IsItemActive()) {
                                hold_v2d += ImGui::GetIO().DeltaTime;
                                if (hold_v2d > 0.3f) g_pt2_tv += 0.002f;
                            } else { hold_v2d = 0; }

                            ImGui::SameLine(0, 15.0f);
                            if (ImGui::Button("↩##undo2", ImVec2(40, 40)) && !g_pt2_history.empty()) {
                                auto prev = g_pt2_history.back();
                                g_pt2_history.pop_back();
                                g_pt2_tu = prev.first;
                                g_pt2_tv = prev.second;
                            }

                            ImGui::Separator();

                            if (StyledButton("自动计算并保存", ButtonVariant::Success, ImVec2(0,0), g_density)) {
                                if ((fabsf(g_pt1_wx) < 0.01f && fabsf(g_pt1_wy) < 0.01f) ||
                                    (fabsf(g_pt2_wx) < 0.01f && fabsf(g_pt2_wy) < 0.01f)) {
                                    AddNotification("请先记录点1和点2的世界坐标", 3.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                                } else {
                                    float dx = g_pt2_wx - g_pt1_wx;
                                    float dy = g_pt2_wy - g_pt1_wy;
                                    float du = g_pt2_tu - g_pt1_tu;
                                    float dv = g_pt2_tv - g_pt1_tv;

                                    if (fabsf(dx) < 0.01f || fabsf(dy) < 0.01f) {
                                        AddNotification("两点距离太近，请重新选点", 2.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                                    } else {
                                        g_map_scale_x = du / dx;
                                        g_map_scale_y = dv / dy;
                                        g_map_offset_u = g_pt1_tu - g_map_scale_x * g_pt1_wx;
                                        g_map_offset_v = g_pt1_tv - g_map_scale_y * g_pt1_wy;

                                        auto& floorRef = g_all_maps[g_current_map_index];
                                        if (!floorRef.empty()) {
                                            auto& Cfg = floorRef[g_current_floor_index];
                                            Cfg.scaleX = g_map_scale_x;
                                            Cfg.scaleY = g_map_scale_y;
                                            Cfg.offsetU = g_map_offset_u;
                                            Cfg.offsetV = g_map_offset_v;
                                            Cfg.flipX = g_map_flip_x;
                                            Cfg.flipY = g_map_flip_y;
                                            Cfg.calibrated = true;
                                            g_use_calib = false;

                                            for (auto& mb : g_musicbox_db) {
                                                if (mb.mapIndex == g_current_map_index && mb.floorIndex == g_current_floor_index) {
                                                    mb.texU = g_pt1_tu;
                                                    mb.texV = g_pt1_tv;
                                                    break;
                                                }
                                            }
                                            SaveConfig();

                                            float dist = sqrtf(dx*dx + dy*dy);
                                            int score = 100;
                                            if (dist < 100.0f) {
                                                score -= (int)((100.0f - dist) * 0.5f);
                                                if (score < 0) score = 0;
                                                char msg[128];
                                                snprintf(msg, sizeof(msg), "校准已保存: %s (精准度: %d%%) - 两点距离过近，精度可能较低", Cfg.name, score);
                                                AddNotification(msg, 4.0f, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                                            } else {
                                                char msg[128];
                                                snprintf(msg, sizeof(msg), "校准已保存: %s (精准度: %d%%)", Cfg.name, score);
                                                AddNotification(msg, 3.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                                            }
                                        }
                                    }
                                }
                            }
                            ImGui::SameLine();
                            ImGui::Checkbox("水平翻转", &g_map_flip_x);
                            ImGui::SameLine();
                            ImGui::Checkbox("垂直翻转", &g_map_flip_y);
                        }
                    }

                    ImGui::Separator();

                    // 预览功能已整合到小地图中，请调整小地图尺寸和位置查看
                    ImGui::TextColored(g_theme.text_muted, "出口/校准功能已整合到小地图中");

                    ImGui::Separator();
                }

                if (ImGui::CollapsingHeader("配置管理")) {
                    if (StyledButton("添加新地图", ButtonVariant::Primary, ImVec2(0,0), g_density)) { ImGui::OpenPopup("添加新地图##detected"); }
                    ImGui::SameLine();
                    if (StyledButton("清理绘制缓存", ButtonVariant::Secondary, ImVec2(0,0), g_density)) { skipClassCache.clear(); fakeHunterCache.clear(); }

                    if (!map_invalid) {
                        bool any_calib = false;
                        for (int i = 0; i < (int)g_all_maps.size(); i++) {
                            for (int j = 0; j < (int)g_all_maps[i].size(); j++) {
                                auto& cfg = g_all_maps[i][j];
                                if (cfg.calibrated) {
                                    any_calib = true;
                                    ImGui::Text("%s (已校准)", cfg.name);
                                    ImGui::SameLine();
                                    std::string resetBtn = "重置##" + std::to_string(i) + "_" + std::to_string(j);
                                    if (StyledButton(resetBtn.c_str(), ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                                        ImGui::OpenPopup(("确认重置校准##" + std::to_string(i) + "_" + std::to_string(j)).c_str());
                                    }
                                    std::string popupName = "确认重置校准##" + std::to_string(i) + "_" + std::to_string(j);
                                    if (ImGui::BeginPopupModal(popupName.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                                        ImGui::Text("确定要重置 %s 的校准数据吗？此操作不可撤销。", cfg.name);
                                        if (StyledButton("确定", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                                            if (i == 0 && j == 0) { cfg.minX = -548.48f; cfg.maxX = 4730.32f; cfg.minY = -3500.49f; cfg.maxY = 1380.00f; }
                                            else if (i == 0 && j == 1) { cfg.minX = -548.48f; cfg.maxX = 4730.32f; cfg.minY = -3500.49f; cfg.maxY = 1380.00f; }
                                            else { cfg.minX = -500.0f; cfg.maxX = 5000.0f; cfg.minY = -3000.0f; cfg.maxY = 1500.0f; }
                                            cfg.scaleX = 1.0f; cfg.scaleY = 1.0f; cfg.offsetU = 0.0f; cfg.offsetV = 0.0f;
                                            cfg.flipX = false; cfg.flipY = false; cfg.calibrated = false;
                                            SaveConfig();
                                            AddNotification("校准已重置", 2.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                                            ImGui::CloseCurrentPopup();
                                        }
                                        ImGui::SameLine();
                                        if (StyledButton("取消", ButtonVariant::Secondary, ImVec2(0,0), g_density)) ImGui::CloseCurrentPopup();
                                        ImGui::EndPopup();
                                    }
                                    ImGui::SameLine();
                                    std::string viewBtn = "查看##" + std::to_string(i) + "_" + std::to_string(j);
                                    if (StyledButton(viewBtn.c_str(), ButtonVariant::Secondary, ImVec2(0,0), g_density)) ImGui::OpenPopup("校准详情");
                                    if (ImGui::BeginPopup("校准详情")) {
                                        ImGui::Text("边界: %.1f~%.1f, %.1f~%.1f", cfg.minX, cfg.maxX, cfg.minY, cfg.maxY);
                                        ImGui::Text("缩放: %.4f x %.4f", cfg.scaleX, cfg.scaleY);
                                        ImGui::Text("偏移: %.4f, %.4f", cfg.offsetU, cfg.offsetV);
                                        ImGui::Text("翻转: %s %s", cfg.flipX?"X":"", cfg.flipY?"Y":"");
                                        ImGui::EndPopup();
                                    }
                                }
                            }
                        }
                        if (!any_calib) ImGui::TextDisabled("暂无已校准地图");
                    }
                }

                static bool show_restart_confirm = false;
                if (!show_restart_confirm) {
                    if (StyledButton("初始化 (城堡专用)", ButtonVariant::Danger, ImVec2(0,0), g_density)) ImGui::OpenPopup("确认初始化");
                }
                if (ImGui::BeginPopupModal("确认初始化", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("确定要初始化吗？这将重启程序。");
                    if (StyledButton("确定", ButtonVariant::Danger, ImVec2(0,0), g_density)) {
                        SaveConfig();
                        char exe_path[256] = {0};
                        if (readlink("/proc/self/exe", exe_path, sizeof(exe_path)) > 0) {
                            char *argv[] = { exe_path, nullptr };
                            execv(exe_path, argv);
                        }
                        exit(0);
                    }
                    ImGui::SameLine();
                    if (StyledButton("取消", ButtonVariant::Secondary, ImVec2(0,0), g_density)) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
                break;
            case 6:  // 关于
                ImGui::PushFont(g_font_ui);
                ImGui::TextColored(g_theme.danger, "超级框架");
                ImGui::PopFont();
                ImGui::TextColored(g_theme.text_muted, "有问题请联系qq：1539093706");
                ImGui::Spacing();
                ImGui::Text("作者: 大米饭先生");
                ImGui::TextColored(g_theme.info, "本辅助不承担任何法律责任");
                break;
        }
        ImGui::EndChild();

        RestoreImGuiStyle(style_bak);
        g_window = ImGui::GetCurrentWindow();
        ImGui::End();
    }

    // ========== 添加新地图弹窗 ==========
    if (ImGui::BeginPopupModal("添加新地图##detected", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("名称", new_name, 64);
        ImGui::Separator();
        ImGui::TextColored(g_theme.text_muted, "音乐盒坐标（主信号源）");
        ImGui::InputFloat("音乐盒 X", &new_music_x);
        ImGui::InputFloat("音乐盒 Y", &new_music_y);
        ImGui::InputFloat("音乐盒 Z", &new_music_z);
        ImGui::Separator();
        ImGui::TextColored(g_theme.text_muted, "钢琴坐标（辅助信号源，自动检测）");
        ImGui::InputFloat("钢琴 X", &new_piano_x);
        ImGui::InputFloat("钢琴 Y", &new_piano_y);
        ImGui::InputFloat("钢琴 Z", &new_piano_z);
        ImGui::Separator();
        ImGui::InputText("纹理路径", new_texture, 128);
        if (StyledButton("确定", ButtonVariant::Success, ImVec2(0,0), g_density)) {
            json j;
            std::ifstream ifs("/sdcard/maps/map_config.json");
            if (ifs) ifs >> j; else j["maps"] = json::array();

            // 检查是否已存在同名地图——存在则更新，不存在则新增
            bool updated = false;
            if (j.contains("maps") && j["maps"].is_array()) {
                for (auto& existing : j["maps"]) {
                    if (existing.value("name", "") == std::string(new_name) &&
                        existing.value("floor", -1) == 0) {
                        // 更新已有地图的音乐盒 + 钢琴坐标
                        existing["music_x"] = new_music_x;
                        existing["music_y"] = new_music_y;
                        existing["music_z"] = new_music_z;
                        existing["piano_x"] = new_piano_x;
                        existing["piano_y"] = new_piano_y;
                        existing["piano_z"] = new_piano_z;
                        if (strlen(new_texture) > 0) existing["texture"] = new_texture;
                        updated = true;
                        break;
                    }
                }
            }

            if (!updated) {
                json m;
                m["name"] = new_name; m["floor"] = 0; m["texture"] = new_texture;
                m["minX"] = -500.0f; m["maxX"] = 5000.0f; m["minY"] = -3000.0f; m["maxY"] = 1500.0f;
                m["music_x"] = new_music_x; m["music_y"] = new_music_y; m["music_z"] = new_music_z;
                m["piano_x"] = new_piano_x; m["piano_y"] = new_piano_y; m["piano_z"] = new_piano_z;
                m["music_texU"] = 0.5; m["music_texV"] = 0.5;
                j["maps"].push_back(m);
            }

            std::ofstream ofs("/sdcard/maps/map_config.json"); ofs << j.dump(4);
            LoadMapConfigFromJSON();
            SaveConfig();
            AddNotification(updated
                ? "已更新地图: " + std::string(new_name) + " (音乐盒+钢琴)"
                : "已添加新地图: " + std::string(new_name),
                3.0f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (StyledButton("取消", ButtonVariant::Secondary, ImVec2(0,0), g_density)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ========== 大图预览已移除，功能整合到小地图 ==========
    if (false) { (void)0; }

    if (show_mimic_overlay) {
        std::lock_guard<std::mutex> lock(mimic_mutex);
        if (!global_validRoles.empty()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * g_density);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * g_density, 12 * g_density));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(g_theme.bg_panel.x, g_theme.bg_panel.y, g_theme.bg_panel.z, 0.92f));
            ImGui::Begin("MimicOverlayWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
            ImDrawList* mimic_draw = ImGui::GetWindowDrawList();
            ImVec2 mimic_pos = ImGui::GetWindowPos();
            ImVec2 mimic_size = ImGui::GetWindowSize();
            mimic_draw->AddRect(mimic_pos, ImVec2(mimic_pos.x + mimic_size.x, mimic_pos.y + mimic_size.y), IM_COL32(60, 80, 120, 160), 16.0f * g_density, 0, 2.0f);
            bool is_overlay_hovered = ImGui::IsWindowHovered();

            // ========== 高级拖动交互：长按拖动 + 单击/双击识别 ==========
            static double last_tap_time = 0.0;
            static bool is_overlay_dragging = false;
            static bool drag_triggered = false;  // 是否已触发拖动（超过拖动阈值）
            static ImVec2 overlay_drag_start_pos;
            static ImVec2 press_mouse_pos;       // 按下时鼠标位置，用于判断拖动距离
            static double press_time = 0.0;      // 按下时间戳，用于判断长按
            const float drag_threshold = 10.0f;  // 拖动触发的最小移动距离（像素）
            const double long_press_time = 0.25; // 长按触发拖动的时间（秒）

            ImVec2 overlay_pos = ImGui::GetWindowPos();
            ImVec2 overlay_size2 = ImGui::GetWindowSize();
            ImVec2 mouse_pos = ImGui::GetMousePos();

            if (ImGui::IsMouseClicked(0) && is_overlay_hovered) {
                double current_time = ImGui::GetTime();

                // 双击（两次点击间隔 <0.4s）→ 关闭悬浮窗
                if (current_time - last_tap_time < 0.4) {
                    show_mimic_overlay = false;
                    last_tap_time = 0.0;
                    is_overlay_dragging = false;
                    drag_triggered = false;
                } else {
                    // 记录按下信息，等待判断是点击/双击还是拖动
                    last_tap_time = current_time;
                    press_time = current_time;
                    press_mouse_pos = mouse_pos;
                    is_overlay_dragging = true;       // 进入拖动候选状态
                    drag_triggered = false;            // 尚未触发实际拖动
                    overlay_drag_start_pos = ImVec2(mouse_pos.x - overlay_pos.x, mouse_pos.y - overlay_pos.y);
                }
            }

            if (is_overlay_dragging) {
                if (ImGui::IsMouseDown(0)) {
                    // 计算手指/鼠标移动距离
                    float dx = mouse_pos.x - press_mouse_pos.x;
                    float dy = mouse_pos.y - press_mouse_pos.y;
                    float moveDist = sqrtf(dx*dx + dy*dy);
                    double holdTime = ImGui::GetTime() - press_time;

                    // 长按超过阈值 或 移动距离超过阈值 → 触发实际拖动
                    if (!drag_triggered && (holdTime > long_press_time || moveDist > drag_threshold)) {
                        drag_triggered = true;
                    }

                    if (drag_triggered) {
                        // 平滑跟随：直接用鼠标位置 - 起始偏移 = 新窗口位置
                        ImVec2 new_pos = ImVec2(mouse_pos.x - overlay_drag_start_pos.x,
                                                 mouse_pos.y - overlay_drag_start_pos.y);
                        // 边界限制：不能超出屏幕可视区域
                        new_pos.x = std::clamp(new_pos.x, 0.0f, displayInfo.width - overlay_size2.x);
                        new_pos.y = std::clamp(new_pos.y, 0.0f, displayInfo.height - overlay_size2.y);
                        ImGui::SetWindowPos(new_pos);
                    }
                } else {
                    // 松手
                    if (!drag_triggered) {
                        // 短按且未拖动→视为点击操作（保持窗口打开，不关闭不拖动）
                        // 如果距上次点击已超过双击间隔则认为是一次单击，不处理
                    }
                    is_overlay_dragging = false;
                    drag_triggered = false;
                }
            }

            for (const auto &r : global_validRoles) {
                std::string roleName = RoleIdToChinese(r.roleId);
                const char *campStr = r.campId == 1 ? "侦探团" : (r.campId == 2 ? "狼  人" : "神秘客");
                ImVec4 color;
                if (r.campId == 1) color = ImVec4(0.2f, 0.8f, 0.8f, 1.0f);
                else if (r.campId == 2) color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                else color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                ImGui::TextColored(color, "[%02d]号 | 阵营: %-6s | 身份: %s", r.index + 1, campStr, roleName.c_str());
            }
            if (is_overlay_dragging && drag_triggered) {
                if (ImGui::IsMouseDown(0)) {
                    ImVec2 new_pos = ImVec2(mouse_pos.x - overlay_drag_start_pos.x, mouse_pos.y - overlay_drag_start_pos.y);
                    new_pos.x = std::clamp(new_pos.x, 0.0f, displayInfo.width - overlay_size2.x);
                    new_pos.y = std::clamp(new_pos.y, 0.0f, displayInfo.height - overlay_size2.y);
                    ImGui::SetWindowPos(new_pos);
                }
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }
    }

    if (main_thread_flag && !*main_thread_flag) {
        CloseDebugLog();
    }

    // === 定期自动保存配置（每300帧 ≈ 5秒） ===
    {
        static int auto_save_counter = 0;
        auto_save_counter++;
        if (auto_save_counter >= 300) {
            auto_save_counter = 0;
            SaveConfig();
        }
    }
}

int 数据() {
    DIR *dir = opendir("/dev/input/");
    if (dir == NULL) return -1;
    struct dirent *ptr = NULL;
    int count = 0;
    while ((ptr = readdir(dir)) != NULL) {
        if (strstr(ptr->d_name, "event")) count++;
    }
    closedir(dir);
    return count ? count : -1;
}

void 处理输入事件(struct input_event ev) {
    if (ev.type == EV_KEY && ev.value == 1) {
        if (ev.code == KEY_VOLUMEUP) MemuSwitch = true;
        else if (ev.code == KEY_VOLUMEDOWN) MemuSwitch = false;
    }
}

void 音量() {
    int EventCount = 数据();
    if (EventCount < 0) {
        printf("未找到输入设备\n");
        return;
    }
    int *fdArray = (int *)malloc(EventCount * sizeof(int));
    fd_set fds;
    struct timeval tv;
    int maxfd = 0;
    for (int i = 0; i < EventCount; i++) {
        char temp[128];
        sprintf(temp, "/dev/input/event%d", i);
        fdArray[i] = open(temp, O_RDONLY | O_NONBLOCK);
        if (fdArray[i] > maxfd) maxfd = fdArray[i];
    }
    struct input_event ev;
    while (true) {
        FD_ZERO(&fds);
        for (int i = 0; i < EventCount; i++) {
            if (fdArray[i] >= 0) FD_SET(fdArray[i], &fds);
        }
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int ret = select(maxfd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0) {
            for (int i = 0; i < EventCount; i++) {
                if (fdArray[i] >= 0 && FD_ISSET(fdArray[i], &fds)) {
                    memset(&ev, 0, sizeof(ev));
                    if (read(fdArray[i], &ev, sizeof(ev)) == sizeof(ev)) {
                        处理输入事件(ev);
                    }
                }
            }
        }
        usleep(5000);
    }
    for (int i = 0; i < EventCount; i++) {
        if (fdArray[i] >= 0) close(fdArray[i]);
    }
    free(fdArray);
}