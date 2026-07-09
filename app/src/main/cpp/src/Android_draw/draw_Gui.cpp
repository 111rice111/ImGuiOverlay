// ============================================================
// draw_Gui.cpp — 拆分后的主文件 (v2.47)
// 包含：全局变量定义、命名空间成员定义、常量映射、
//       核心初始化/渲染函数、模块查找函数、输入事件处理
// 所有共享声明见 draw_Gui_internal.h
// ============================================================

#include "draw_Gui_internal.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ========== 全局变量定义 ==========

// --- 调试 ---
FILE* g_debug_file = nullptr;
std::mutex g_debug_mutex;
std::atomic<bool> g_debug_enabled{false};
char g_debug_file_path[256] = {0};
std::unordered_set<std::string> g_logged_entities;

// --- 过滤器 ---
float Global_Filter_Max_Abs_XY = 10000.0f;
float Global_Filter_Min_Z = -300.0f;
float Global_Filter_Max_Z = 300.0f;
float Global_Filter_Max_Distance = 300.0f;

// --- UI 状态 ---
bool MemuSwitch = true;
float g_minimized_bar_anim = 0.0f;
ImVec2 g_bar_custom_pos(-1, -1);
bool voice = true;
bool show_mimic_overlay = false;
bool g_MimicModeEnabled = false;

// --- 会议 ---
bool is_meeting_detected = false;
float meeting_center_x = 0.0f;
float meeting_center_y = 0.0f;
int meeting_total_seats = 12;
float meeting_radius = 30.0f;
std::unordered_map<std::string, int> bound_seat_by_class;

// --- 场景物体 ---
Vector3A g_detected_musicbox_pos;
Vector3A g_detected_piano_pos;
int g_last_rendered_map_index = -1;
int g_last_rendered_floor_index = -1;
char g_map_scores_buf[2048] = "";

// --- 出口 ---
std::vector<std::vector<std::vector<Vector3A>>> g_exits;
std::vector<std::vector<std::vector<ImVec2>>> g_exit_uvs;

// --- 地图渲染参数 ---
float g_map_opacity = 0.55f;
float g_label_opacity = 1.0f;
float g_self_opacity = 1.0f;
float g_route_opacity = 1.0f;
float g_saved_path_opacity = 0.6f;
float g_path_fade_dist = 3000.0f;

// --- 路径绘制 ---
bool g_draw_path_mode = false;
std::vector<Vector3A> g_current_drawing_path;
int  g_selected_path_index = -1;
std::vector<std::vector<Vector3A>> g_saved_paths;
std::vector<char> g_path_visible;
std::vector<ImU32> g_path_colors;
std::vector<std::vector<std::vector<std::vector<Vector3A>>>> g_saved_paths_by_map;
int g_last_paths_map_idx = -1;
int g_last_paths_floor_idx = -1;
std::vector<std::vector<ImVec2>> g_path_vertex_cache;
bool g_path_cache_dirty = true;
float g_last_cache_map_pos_x = -1, g_last_cache_map_pos_y = -1, g_last_cache_map_size = -1;
int g_path_edit_mode = 0;
std::vector<Vector3A> g_pending_path;
bool g_pending_save_confirm = false;
float g_pending_save_timeout = 0.0f;
float g_draw_map_size_bak = 800.0f;
float g_draw_map_posx_bak = 700.0f;
float g_draw_map_posy_bak = 200.0f;

// --- 导航 ---
bool  g_dest_select_mode = false;
float g_dest_world_x = 0, g_dest_world_y = 0, g_dest_world_z = 0;
std::vector<Vector3A> g_nav_render_path;

// --- 路径导入预览 ---
std::vector<std::vector<Vector3A>> g_import_preview_paths;
bool g_has_import_preview = false;
int g_import_preview_map_idx = -1;
int g_import_preview_floor_idx = -1;
int g_import_tex_w = 0, g_import_tex_h = 0;
char g_import_txt_path[256] = {};
int g_import_success_count = 0;
std::string g_import_status;

// --- 大地图 ---
bool g_show_big_map = false;
float g_big_map_zoom = 1.0f;

// --- 脏数据 ---
bool g_dirty_exits = false;
bool g_dirty_paths = false;
int g_dirty_flush_counter = 0;

// --- 通知 ---
std::deque<Notification> g_notifications;

// --- 校准历史 ---
std::vector<std::pair<float,float>> g_pt1_history;
std::vector<std::pair<float,float>> g_pt2_history;
std::vector<std::vector<Vector3A>> g_path_history;

float hold_u1l = 0, hold_u1r = 0, hold_v1u = 0, hold_v1d = 0;
float hold_u2l = 0, hold_u2r = 0, hold_v2u = 0, hold_v2d = 0;

float g_press_timer = 0;
ImVec2 g_press_pos;

float popup_u = 0, popup_v = 0;
int g_del_exit_idx = 0;
ImVec2 g_last_exit_screen_pos;
ImVec2 g_last_exit_rendered_pos;
bool   g_show_exit_debug = false;
int    g_drag_exit_idx = -1;
bool   g_map_drag_blocked = false;

bool g_path_drawing_active = false;
ImVec2 g_path_start_pos;

// --- 路径平滑与吸附参数 ---
float g_smooth_strength = 4.0f;
bool  g_enable_snap = false;
float g_grid_spacing = 100.0f;
float g_snap_distance = 30.0f;
bool  g_show_grid = false;
float g_grid_alpha = 0.3f;

bool  g_show_3d_paths = false;
float g_3d_path_height = 10.0f;
int   g_3d_path_style = 2;
float g_3d_path_fade_dist = 30.0f;
float g_3d_line_width = 3.0f;
float g_3d_dot_radius = 40.0f;
float g_3d_flow_speed = 20.0f;
bool  g_show_saved_paths = true;

bool  g_ortho_draw = false;
float g_path_draw_threshold = 15.0f;

// --- 重初始化 ---
std::atomic<bool> g_need_reinit{false};
std::atomic<int> g_saved_pid{-1};

// --- 地图纹理 ---
int g_map_texture_w[MAX_MAP_COUNT][MAX_FLOOR_COUNT] = {{0}};
int g_map_texture_h[MAX_MAP_COUNT][MAX_FLOOR_COUNT] = {{0}};

// --- 指纹 ---
std::vector<MapFingerprint> g_fingerprint_db;
std::vector<int> g_mapidx_from_fp_id;
std::vector<int> g_fp_id_from_mapidx;

// --- 检测状态 ---
MapDetectPhase g_detect_phase = MapDetectPhase::LOCKED;
int g_detect_debounce_frames = 0;
int g_low_confidence_counter = 0;
int g_locked_stable_frames = 0;
float g_detect_best_score = 0.0f;
int g_detect_best_fp_id = -1;
char g_detect_status_text[256] = "等待识别";

Vector3A g_prev_player_pos;
bool g_has_prev_pos = false;
bool g_musicbox_moved = false;
Vector3A g_cached_musicbox_pos;
bool g_has_cached_musicbox = false;
std::vector<Vector3A> g_cached_chairs;
Vector3A g_cached_piano_pos;
bool g_has_cached_piano = false;

bool g_new_map_prompt_shown = false;
char g_score_debug_buf[2048] = "";

// --- 地图 UI ---
bool g_use_calib = true;
bool g_show_map_status = false;
std::string g_save_notification;
float g_notification_timer = 0.0f;

float g_pt1_wx = 0, g_pt1_wy = 0;
float g_pt1_tu = 0.6f, g_pt1_tv = 0.55f;
float g_pt2_wx = 0, g_pt2_wy = 0;
float g_pt2_tu = 0.4f, g_pt2_tv = 0.45f;
int g_drag_point = 0;

float g_map_scale_x = 0.0002f;
float g_map_scale_y = 0.0002f;
float g_map_offset_u = 0.5f;
float g_map_offset_v = 0.5f;
bool g_map_flip_x = false;
bool g_map_flip_y = false;
bool g_show_nav_line = false;
float g_map_label_scale = 0.45f;

// --- 非头文件 extern 的全局变量（原文件中非 static）---
bool g_talent_need_refresh = false;
std::vector<RoleInfo> global_validRoles;
std::mutex mimic_mutex;
std::atomic<bool> is_scanning_mimic{false};

std::vector<std::vector<MapConfig>> g_all_maps;
int g_current_map_index = -1;
int g_current_floor_index = 0;

std::vector<MusicboxKey> g_musicbox_db;
std::vector<PianoKey> g_piano_db;
std::vector<ChairKey> g_chair_db;
std::vector<Vector3A> g_detected_chairs;

GLuint g_map_textures[MAX_MAP_COUNT][MAX_FLOOR_COUNT] = {{0}};

bool g_map_enabled = false;
float g_map_display_size = 800.0f;
float g_map_pos_x = 700.0f;
float g_map_pos_y = 200.0f;
bool g_map_auto_detect = true;
float g_treasure_threshold = 5000.0f;

// --- 盖板/触摸 ---
bool show_draw_MarktheSoul = true;
bool wood_enabled = false;
ImTextureID g_tex_ui_bg = 0;
ImTextureID g_tex_ui_avatar = 0;
int g_tex_ui_bg_w = 0, g_tex_ui_bg_h = 0;
int g_tex_ui_avatar_w = 0, g_tex_ui_avatar_h = 0;
bool g_ui_textures_loaded = false;
float wood_touch_pct_x = 0.103f;
float wood_touch_pct_y = 0.923f;
int   g_last_display_w = 0, g_last_display_h = 0;
float wood_touch_x = 221.796f;
float wood_touch_y = 2215.87f;
float wood_offset_x = 0.0f;
float wood_offset_y = 0.0f;
float wood_length = 17.5f;
float wood_width  = 17.5f;
float wood_trigger_dist = 3.0f;
float wood_cooldown_dur = 2.2f;
bool  show_wood_rect = false;
Vector3A g_wood_viz_pos;
float g_wood_viz_angle = 0;
bool  g_wood_viz_valid = false;
Vector3A g_wood_viz_pos_prev = {0,0,0};
bool  g_was_hunter_inside = false;
float g_wood_cooldown = 0.0f;
float g_last_wood_trigger_time = 0.0f;
bool show_touch_point = false;
bool g_show_wood_diag = false;
float g_last_touch_x = 0, g_last_touch_y = 0;
float g_last_touch_time = 0;
float g_wood_popup_time = 0;

int   g_touch_max_x = 23999;
int   g_touch_max_y = 33919;
char  g_touch_path[128] = {0};
bool  g_touch_ready = false;

int   g_calib_step = 0;
float g_calib_inj[4][2];
float g_calib_obs_buf[4][2];
bool  g_calib_done = true;
float g_calib_A=1.00334f,g_calib_B=0,g_calib_C=-1.67224f;
float g_calib_D=0,g_calib_E=1,g_calib_F=0;

// --- 绘制开关 ---
ImColor g_BoxColor_Survivor = ImColor(50, 255, 50, 255);
ImColor g_BoxColor_Hunter   = ImColor(255, 50, 50, 255);
ImColor g_BoxColor_Ghost    = ImColor(255, 255, 255, 255);
bool show_draw_EnhancedFrame = true;
bool show_draw_Line = false;
bool show_draw_QY = true;
bool show_draw_sender = true;
bool show_draw_Animal = true;
bool show_draw_Name = true;
bool show_draw_Distance = true;
bool show_draw_Cellar = true;
bool show_draw_Chair = false;
bool show_draw_BANZI = false;
bool show_draw_BoxItem = false;
bool show_draw_Prop = true;
bool show_draw_prophet = true;
bool show_draw_redqueen = true;
bool Debugging = false;
bool disable_skip_filter = false;
bool inform_ghost = true;
bool g_talent_view = false;
bool g_show_detailed = false;
std::string g_ConfigPath = "/data/local/bin/overlay_config.txt";
int g_chair_dist = 30;
int g_board_dist = 30;
int g_box_dist = 30;
int g_sender_dist = 50;

std::string g_current_game_package;
int g_current_game_pid = -1;

// --- 过滤缓存 ---
std::unordered_map<std::string, bool> skipClassCache;
std::unordered_map<std::string, bool> fakeHunterCache;

// --- 渲染变量 ---
ImVec2 circle_pos{-1.0f, -1.0f};
ImFont *g_font_ui{};
float circle_radius = 60.0f;
float margin = 10.0f;
std::string 过滤类名, 类名;
uint32_t orientation = static_cast<uint32_t>(-1);
float g_ui_density = 1.0f;
ImFont *g_main_font{};
ImFont *g_ui_font{};
Vector3A D, Z, M;
float z_x{}, z_y{}, z_z{}, d_x{}, d_y{}, d_z{}, camera, r_x{}, r_y{}, r_w{};
float X1{}, Y1{}, X2{}, Y2{}, W{}, H{};
char objtext[256]{};
char 监管者预知[1024]{};
float px{}, py{};

// --- 数据缓冲 ---
std::vector<DataStruct> data_buffers[2];
std::atomic<int> front_buffer_idx = 0;
std::mutex data_mutex;
std::unordered_map<uintptr_t, int> g_meeting_seat_map;
std::vector<MirrorInfo> g_mirrorList;
bool g_holdMirror = false;
Vector3A g_mirrorCenter;
Vector3A g_mirrorNormal;
std::atomic<int> g_selfAction{0};

// --- 调试计数 ---
int g_debug_scanned_count = 0;
int g_debug_player_count = 0;
int g_debug_boss_count = 0;
bool g_debug_self_found = false;
float g_debug_last_cam_z = 0.0f;
char g_debug_self_cls[128] = "";

// --- 字体 ---
bool fonts_initialized = false;

// --- 纹理加载器 ---
std::vector<PendingTex> g_pending;
std::mutex g_pending_mtx;
std::thread g_loader;
bool g_loader_on = false;

// --- UI 主题 ---
UITheme g_theme;

// --- 全局变量 (extern 声明在 draw.h) ---
ANativeWindow *window{};
android::ANativeWindowCreator::DisplayInfo displayInfo{};
ImGuiWindow *g_window{};
int abs_ScreenX{}, abs_ScreenY{};
int native_window_screen_x{}, native_window_screen_y{};
std::unique_ptr<AndroidImgui> graphics{};

// --- 其他全局变量 (供拆分文件使用) ---
float matrix[16]{};
char g_texture_status[512] = "等待加载...";
char g_map_detect_debug[1024] = "";

// ========== 命名空间: GlobalMemory ==========
namespace GlobalMemory {
    uintptr_t libbase{};
    uintptr_t Arrayaddr{};
    long int Count{};
    uintptr_t Matrix{};
    uintptr_t 自身{};
    int 数量{};
    long int MatrixOffset = 0;
    long int ArrayaddrOffset = 0;
    int 状态{};
    const char *libso = "libclient.so";
    long int ModulePagesCount = 0;
    uintptr_t bss_base{};
}

// ========== 命名空间: MjSubsystem ==========
namespace MjSubsystem {
    bool draw_props = true;
    bool show_distance = false;
    float high_value_threshold = 5000.0f;
    bool show_monsters      = true;
    bool show_big_chest     = true;
    bool show_small_chest   = true;
    bool show_traps         = true;
    bool show_interactables = true;
    bool show_high_value    = true;
    bool show_low_value     = true;

    float max_dist_monsters      = 100.0f;
    float max_dist_big_chest     = 300.0f;
    float max_dist_small_chest   = 200.0f;
    float max_dist_traps         = 50.0f;
    float max_dist_interactables = 100.0f;
    float max_dist_high_value    = 300.0f;
    float max_dist_low_value     = 150.0f;

    const std::vector<std::string> allowed_prefixes = {
            "prop_wz_", "rd01_", "mj_", "prop_un_"
    };

    std::unordered_set<std::string> mj_special_classes;

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

// ========== 常量映射 ==========

const std::vector<std::pair<std::string, std::string>> g_prop_name_map = {
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

const std::vector<std::string> g_game_packages = {
        "com.netease.dwrg",
        "com.netease.idv",
        "com.netease.idv.googleplay",
        "com.netease.dwrg.mi",
        "com.netease.dwrg.oppo",
        "com.netease.dwrg.huawei",
        "com.netease.dwrg.aligames",
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

// ========== UI 纹理加载 ==========

void LoadUITextures() {
    if (g_ui_textures_loaded) return;
    if (!graphics) return;
    TextureInfo tex_bg = graphics->LoadTextureFromMemory((void*)g_ui_bg_data, g_ui_bg_data_len);
    if (tex_bg.DS) { g_tex_ui_bg = (ImTextureID)(intptr_t)tex_bg.DS; g_tex_ui_bg_w = tex_bg.w; g_tex_ui_bg_h = tex_bg.h; }
    TextureInfo tex_avatar = graphics->LoadTextureFromMemory((void*)g_ui_avatar_data, g_ui_avatar_data_len);
    if (tex_avatar.DS) { g_tex_ui_avatar = (ImTextureID)(intptr_t)tex_avatar.DS; g_tex_ui_avatar_w = tex_avatar.w; g_tex_ui_avatar_h = tex_avatar.h; }
    g_ui_textures_loaded = true;
}

// ========== 核心函数 ==========

void init_My_drawdata() {
    anti_debug_init();
    driver_init(); // ★ 哈基米风格驱动: 自动扫描 /dev/ + ioctl 读写
    LoadMapConfigFromJSON();
    // 启动时也加载指纹数据库，建立指纹↔地图索引映射
    LoadFingerprintDB();
    RebuildFingerprintMapping();
    LoadConfig();
    MjSubsystem::Init();

    // ★ 跨设备适配: 从百分比计算触摸坐标, 旧版兼容
    // 旧版配置只有绝对坐标无百分比 → 反向计算百分比后沿用
    if (wood_touch_pct_x > 0.0f && wood_touch_pct_y > 0.0f) {
        // 新版: 有百分比 → 计算绝对坐标
        wood_touch_x = wood_touch_pct_x * (float)displayInfo.width;
        wood_touch_y = wood_touch_pct_y * (float)displayInfo.height;
    } else if (wood_touch_x > 0.0f && wood_touch_y > 0.0f &&
               wood_touch_x < (float)displayInfo.width * 2.0f &&
               wood_touch_y < (float)displayInfo.height * 2.0f) {
        // 旧版: 有绝对坐标无百分比 → 反算百分比
        wood_touch_pct_x = wood_touch_x / (float)displayInfo.width;
        wood_touch_pct_y = wood_touch_y / (float)displayInfo.height;
    } else {
        // 无有效配置 → 默认 85%/55% (常见交互按钮位置)
        wood_touch_pct_x = 0.85f;
        wood_touch_pct_y = 0.55f;
        wood_touch_x = wood_touch_pct_x * (float)displayInfo.width;
        wood_touch_y = wood_touch_pct_y * (float)displayInfo.height;
    }
    g_last_display_w = displayInfo.width;
    g_last_display_h = displayInfo.height;

    // ★ 保底: 确保坐标不超出屏幕 (旧版配置迁移后仍需保险)
    if (wood_touch_x <= 0.0f || wood_touch_x > displayInfo.width) {
        wood_touch_x = displayInfo.width * 0.85f;
        wood_touch_pct_x = 0.85f;
    }
    if (wood_touch_y <= 0.0f || wood_touch_y > displayInfo.height) {
        wood_touch_y = displayInfo.height * 0.55f;
        wood_touch_pct_y = 0.55f;
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
    // ★ 方案2: g_ui_density 跟随屏幕尺寸，以 1080px 宽度为基准
    {
        float raw = (float)displayInfo.width / 1080.0f;
        g_ui_density = (raw < 0.6f) ? 0.6f : (raw > 1.8f) ? 1.8f : raw;
    }
}

// ========== 地图状态管理 ==========

void ResetMapState() {
    memset(g_map_textures, 0, sizeof(g_map_textures));
    memset(g_map_texture_w, 0, sizeof(g_map_texture_w));
    memset(g_map_texture_h, 0, sizeof(g_map_texture_h));
    g_current_map_index = -1;
    g_current_floor_index = 0;
    g_detect_phase = MapDetectPhase::LOCKED;
    g_detect_debounce_frames = 0;
    g_detect_best_score = 0.0f;
    g_detect_best_fp_id = -1;
    g_low_confidence_counter = 0;
    g_musicbox_moved = false;
    g_has_prev_pos = false;
    g_has_cached_musicbox = false;
    g_has_cached_piano = false;
    g_cached_chairs.clear();
    g_new_map_prompt_shown = false;
    g_detected_musicbox_pos = Vector3A{};
    g_detected_piano_pos = Vector3A{};
    g_last_rendered_map_index = -1;
    g_last_rendered_floor_index = -1;
    g_selected_path_index = -1;
    g_locked_stable_frames = 0;
}

void InvalidateMapTextures() {
    memset(g_map_textures, 0, sizeof(g_map_textures));
    memset(g_map_texture_w, 0, sizeof(g_map_texture_w));
    memset(g_map_texture_h, 0, sizeof(g_map_texture_h));
    g_last_rendered_map_index = -1;
    g_last_rendered_floor_index = -1;
}

void drawBegin() {
    // v2.45: 同步 ImGui DisplaySize 为真实屏幕尺寸（每帧）
    // 窗口已改为真实屏幕尺寸，DisplaySize 始终与之一致
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)displayInfo.width, (float)displayInfo.height);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }

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

// ========== 模块查找函数 ==========

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
                if (g_drv) g_drv->initialize(id);
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

// ========== 输入事件处理 ==========

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
