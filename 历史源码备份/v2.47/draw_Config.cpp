// ============================================================
// draw_Config.cpp — draw_Gui 拆分文件 (v2.47)
// 本文件从原 draw_Gui.cpp 中拆分而来，包含配置加载/保存与
// JSON 导出相关函数：DetectGameProcess、LoadConfig、SaveConfig、
// SaveExitsToJSON、SavePlayerPathsToJSON、SaveSceneObjectsToJSON。
// 所有共享声明见 draw_Gui_internal.h。
// ============================================================

#include "draw_Gui_internal.h"

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

void LoadConfig() {
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
    // 不恢复 g_current_map_index — 每次启动强制重检测，避免残留旧地图索引导致显示错乱
    getFloat("g_map_display_size", g_map_display_size);
    getFloat("g_3d_path_height", g_3d_path_height);
    getInt("g_3d_path_style", g_3d_path_style);
    getFloat("g_3d_path_fade_dist", g_3d_path_fade_dist);
    getFloat("g_3d_line_width", g_3d_line_width);
    getFloat("g_3d_dot_radius", g_3d_dot_radius);
    getFloat("g_3d_flow_speed", g_3d_flow_speed);
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
    getInt("sender_distance", g_sender_dist);

    // Tab 2: 自动盖板
    getBool("wood_enabled", wood_enabled);
    getBool("show_wood_diag", g_show_wood_diag);
    getBool("show_touch_point", show_touch_point);
    // ★ 优先读取百分比 (跨设备持久化), 回退读取旧版绝对坐标
    getFloat("wood_touch_pct_x", wood_touch_pct_x);
    getFloat("wood_touch_pct_y", wood_touch_pct_y);
    getFloat("wood_touch_x", wood_touch_x);
    getFloat("wood_touch_y", wood_touch_y);
    getFloat("wood_trigger_dist", wood_trigger_dist);
    getFloat("wood_cooldown_dur", wood_cooldown_dur);
    getFloat("wood_length", wood_length);
    getFloat("wood_width", wood_width);
    getBool("show_wood_rect", show_wood_rect);
    getFloat("wood_offset_x", wood_offset_x); getFloat("wood_offset_y", wood_offset_y);
    getFloat("calib_A", g_calib_A); getFloat("calib_B", g_calib_B); getFloat("calib_C", g_calib_C);
    getFloat("calib_D", g_calib_D); getFloat("calib_E", g_calib_E); getFloat("calib_F", g_calib_F);
    getBool("calib_done", g_calib_done);

    // Tab 3: 模仿者
    getBool("show_mimic_overlay", show_mimic_overlay);

    // Tab 5: 地图管理
    getBool("g_show_nav_line", g_show_nav_line);
    getFloat("g_map_label_scale", g_map_label_scale);
    getFloat("g_map_opacity", g_map_opacity);
    getFloat("g_label_opacity", g_label_opacity);
    getFloat("g_self_opacity", g_self_opacity);
    getFloat("g_route_opacity", g_route_opacity);
    getFloat("g_saved_path_opacity", g_saved_path_opacity);
    getFloat("g_path_fade_dist", g_path_fade_dist);
    getBool("g_use_calib", g_use_calib);
    getBool("g_show_map_status", g_show_map_status);
    getBool("g_map_flip_x", g_map_flip_x);
    getBool("g_map_flip_y", g_map_flip_y);

    // 大图预览
    getFloat("g_big_map_zoom", g_big_map_zoom);

    // g_talent_view独立窗口
    getBool("show_detailed", g_show_detailed);
}

void SaveConfig() {
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
    file << "g_3d_path_height=" << g_3d_path_height << "\n";
    file << "g_3d_path_style=" << g_3d_path_style << "\n";
    file << "g_3d_path_fade_dist=" << g_3d_path_fade_dist << "\n";
    file << "g_3d_line_width=" << g_3d_line_width << "\n";
    file << "g_3d_dot_radius=" << g_3d_dot_radius << "\n";
    file << "g_3d_flow_speed=" << g_3d_flow_speed << "\n";
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
    file << "sender_distance=" << g_sender_dist << "\n";

    // Tab 2: 自动盖板
    file << "wood_enabled=" << wood_enabled << "\n";
    file << "show_wood_diag=" << g_show_wood_diag << "\n";
    file << "show_touch_point=" << show_touch_point << "\n";
    // ★ 同时保存百分比和绝对坐标 (百分比为主, 绝对坐标为旧版兼容)
    file << "wood_touch_pct_x=" << wood_touch_pct_x << "\n";
    file << "wood_touch_pct_y=" << wood_touch_pct_y << "\n";
    file << "wood_touch_x=" << wood_touch_x << "\n";
    file << "wood_touch_y=" << wood_touch_y << "\n";
    file << "wood_trigger_dist=" << wood_trigger_dist << "\n";
    file << "wood_cooldown_dur=" << wood_cooldown_dur << "\n";
    file << "wood_length=" << wood_length << "\n";
    file << "wood_width=" << wood_width << "\n";
    file << "show_wood_rect=" << show_wood_rect << "\n";
    file << "wood_offset_x=" << wood_offset_x << "\n";
    file << "wood_offset_y=" << wood_offset_y << "\n";
    file << "calib_A=" << g_calib_A << "\n"; file << "calib_B=" << g_calib_B << "\n";
    file << "calib_C=" << g_calib_C << "\n"; file << "calib_D=" << g_calib_D << "\n";
    file << "calib_E=" << g_calib_E << "\n"; file << "calib_F=" << g_calib_F << "\n";
    file << "calib_done=" << g_calib_done << "\n";

    // Tab 3: 模仿者
    file << "show_mimic_overlay=" << show_mimic_overlay << "\n";

    // Tab 5: 地图管理
    file << "g_show_nav_line=" << g_show_nav_line << "\n";
    file << "g_map_label_scale=" << g_map_label_scale << "\n";
    file << "g_map_opacity=" << g_map_opacity << "\n";
    file << "g_label_opacity=" << g_label_opacity << "\n";
    file << "g_self_opacity=" << g_self_opacity << "\n";
    file << "g_route_opacity=" << g_route_opacity << "\n";
    file << "g_saved_path_opacity=" << g_saved_path_opacity << "\n";
    file << "g_path_fade_dist=" << g_path_fade_dist << "\n";
    file << "g_use_calib=" << g_use_calib << "\n";
    file << "g_show_map_status=" << g_show_map_status << "\n";
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
        const char* backup_dir = MAPS_ROOT "calib_backup";
        // 确保目录存在（mkdir -p 等效）
        mkdir(MAPS_ROOT, 0755);
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
    std::string json_path = MAPS_ROOT "map_config.json";
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
    // 写入前备份原文件
    try {
        std::filesystem::copy(json_path, json_path + ".bak",
            std::filesystem::copy_options::overwrite_existing);
    } catch (...) {}
    std::ofstream ofs(json_path);
    ofs << j.dump(4);
}

// ========== 保存玩家路径到 JSON ==========
void SavePlayerPathsToJSON(int mapIdx, int floorIdx) {
    // 保存前将 g_saved_paths 同步回全量存储
    while (g_saved_paths_by_map.size() <= mapIdx) g_saved_paths_by_map.push_back({});
    while (g_saved_paths_by_map[mapIdx].size() <= floorIdx) g_saved_paths_by_map[mapIdx].push_back({});
    g_saved_paths_by_map[mapIdx][floorIdx] = g_saved_paths;

    std::string json_path = MAPS_ROOT "map_config.json";
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
    // 写入前备份原文件
    try {
        std::filesystem::copy(json_path, json_path + ".bak",
            std::filesystem::copy_options::overwrite_existing);
    } catch (...) {}
    std::ofstream ofs(json_path);
    ofs << j.dump(4);
}


// ========== 保存场景物体（钢琴+凳子）到 JSON ==========
SyncResult SaveSceneObjectsToJSON(int mapIdx, int floorIdx) {
    SyncResult sr;
    std::string json_path = MAPS_ROOT "map_config.json";
    std::ifstream ifs(json_path);
    json j;
    if (ifs) ifs >> j; else return sr;

    if (!j.contains("maps")) return sr;

    for (auto& m : j["maps"]) {
        int mf = m.value("floor", 0);
        int mi = -1;
        for (int i = 0; i < (int)g_all_maps.size(); i++) {
            if (!g_all_maps[i].empty() && g_all_maps[i][0].name == m.value("name", "")) {
                mi = i; break;
            }
        }
        if (mi == mapIdx && mf == floorIdx) {
            // ★ 音乐盒
            if (g_detected_musicbox_pos.X != 0.0f || g_detected_musicbox_pos.Y != 0.0f) {
                m["musicbox_x"] = g_detected_musicbox_pos.X;
                m["musicbox_y"] = g_detected_musicbox_pos.Y;
                m["musicbox_z"] = g_detected_musicbox_pos.Z;
                sr.musicbox = 1;
            }
            // ★ 钢琴
            if (g_detected_piano_pos.X != 0.0f || g_detected_piano_pos.Y != 0.0f) {
                m["piano_x"] = g_detected_piano_pos.X;
                m["piano_y"] = g_detected_piano_pos.Y;
                m["piano_z"] = g_detected_piano_pos.Z;
                sr.piano = 1;
            }
            // ★ 凳子
            m["chairs"] = json::array();
            for (auto& chair : g_detected_chairs) {
                json c;
                c["x"] = chair.X; c["y"] = chair.Y; c["z"] = chair.Z;
                m["chairs"].push_back(c);
            }
            sr.chairs = (int)g_detected_chairs.size();
            break;
        }
    }
    std::ofstream ofs(json_path);
    ofs << j.dump(4);

    // 重新加载数据库
    g_piano_db.clear();
    g_chair_db.clear();
    g_musicbox_db.clear();
    for (auto& m : j["maps"]) {
        int mi = -1;
        for (int i = 0; i < (int)g_all_maps.size(); i++) {
            if (!g_all_maps[i].empty() && g_all_maps[i][0].name == m.value("name", "")) {
                mi = i; break;
            }
        }
        if (mi < 0) continue;
        // 音乐盒
        if (m.contains("musicbox_x") && m.contains("musicbox_y")) {
            MusicboxKey mb;
            mb.x = m["musicbox_x"]; mb.y = m["musicbox_y"];
            mb.z = m.value("musicbox_z", 0.0f); mb.mapIndex = mi;
            g_musicbox_db.push_back(mb);
        }
        // 钢琴
        if (m.contains("piano_x") && m.contains("piano_y")) {
            PianoKey pk;
            pk.x = m["piano_x"]; pk.y = m["piano_y"];
            pk.z = m.value("piano_z", 0.0f); pk.mapIndex = mi;
            g_piano_db.push_back(pk);
        }
        // 凳子
        if (m.contains("chairs")) {
            for (auto& c : m["chairs"]) {
                ChairKey ck;
                ck.x = c["x"]; ck.y = c["y"];
                ck.z = c.value("z", 0.0f); ck.mapIndex = mi;
                g_chair_db.push_back(ck);
            }
        }
    }
    return sr;
}
