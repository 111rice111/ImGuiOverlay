// =============================================================
// draw_MapSystem.cpp — 地图系统功能模块
// 从 draw_Gui.cpp 拆分而来 (v2.47)
// 包含：脏数据刷写、地图配置获取、楼层判定、纹理加载、
//       地图自动识别、指纹匹配、传送检测、地图切换等
// =============================================================

#include "draw_Gui_internal.h"
#include "stb_image.h"

// 刷写所有脏数据到 JSON（必须在此处定义，依赖 g_current_map_index）
void FlushDirtyData() {
    if (g_dirty_exits && g_current_map_index >= 0) {
        SaveExitsToJSON(g_current_map_index, g_current_floor_index);
        g_dirty_exits = false;
    }
    if (g_dirty_paths && g_current_map_index >= 0) {
        SavePlayerPathsToJSON(g_current_map_index, g_current_floor_index);
        g_dirty_paths = false;
    }
}

// 标记出口数据为脏
void MarkExitsDirty() { g_dirty_exits = true; g_dirty_flush_counter = DIRTY_FLUSH_INTERVAL; }
// 标记路径数据为脏
void MarkPathsDirty() { g_dirty_paths = true; g_dirty_flush_counter = DIRTY_FLUSH_INTERVAL; g_path_cache_dirty = true; }

void AddNotification(const std::string& text, float duration, ImVec4 color) {
    g_notifications.push_back({text, duration, color});
    if (g_notifications.size() > 5) g_notifications.pop_front();
}

// GetActiveMapConfig 函数体（必须在 g_all_maps, g_map_scale_x 等之后定义）
const MapConfig& GetActiveMapConfig() {
    static MapConfig s_fallback;
    if (g_current_map_index >= 0 && g_current_map_index < (int)g_all_maps.size() &&
        g_current_floor_index >= 0 && g_current_floor_index < (int)g_all_maps[g_current_map_index].size()) {
        auto& cfg = g_all_maps[g_current_map_index][g_current_floor_index];
        float ww = cfg.maxX - cfg.minX, wh = cfg.maxY - cfg.minY;
        // ★ P1 防卡死: 二楼未校准时(min==max 或范围极小)回退到一楼配置
        if ((ww < 10.0f || wh < 10.0f) && g_current_floor_index > 0) {
            auto& f0 = g_all_maps[g_current_map_index][0];
            if (f0.maxX - f0.minX >= 10.0f) return f0;
        }
        return cfg;
    }
    s_fallback = MapConfig{};
    s_fallback.scaleX = g_map_scale_x; s_fallback.scaleY = g_map_scale_y;
    s_fallback.offsetU = g_map_offset_u; s_fallback.offsetV = g_map_offset_v;
    s_fallback.flipX = g_map_flip_x; s_fallback.flipY = g_map_flip_y;
    s_fallback.calibrated = false;
    s_fallback.floorZThreshold = 250.0f;
    return s_fallback;
}

// 楼层判断：Z > 190 为二楼
// ========== 楼层阈值：Z > 190 判定为 2楼，≤ 190 为 1楼 ==========
inline int GetFloorFromPlayerZ(const Vector3A& pos) {
    return (pos.Z > 190.0f) ? 1 : 0;
}

// 安全钳制楼层索引到当前地图的有效范围内（防止越界导致显示错误地图）
inline int SafeClampFloorIdx(int mapIdx, int floorIdx) {
    if (mapIdx < 0 || mapIdx >= (int)g_all_maps.size()) return 0;
    if (g_all_maps[mapIdx].empty()) return 0;
    int maxFloor = (int)g_all_maps[mapIdx].size() - 1;
    if (floorIdx > maxFloor) return maxFloor;
    if (floorIdx < 0) return 0;
    return floorIdx;
}

void UpdateCurrentFloor() {
    if (g_current_map_index < 0 || g_current_map_index >= (int)g_all_maps.size()) return;
    auto& floors = g_all_maps[g_current_map_index];
    if (floors.empty()) return;

    // 手动模式下不自动切换楼层，尊重用户的手动选择
    if (!g_map_auto_detect) return;

    int targetFloor = GetFloorFromPlayerZ(Z);

    if (targetFloor >= (int)floors.size()) return;

    if (targetFloor != g_current_floor_index) {
        printf("[Floor] Z=%.1f → %d楼 (阈值190, map[%d] floors=%zu, 旧楼层=%d)\n",
            Z.Z, targetFloor + 1, g_current_map_index, floors.size(), g_current_floor_index);
        g_current_floor_index = targetFloor;

        g_pt1_wx = 0.0f; g_pt1_wy = 0.0f;
        g_pt2_wx = 0.0f; g_pt2_wy = 0.0f;

        g_pt1_tu = 0.4f; g_pt1_tv = 0.45f;
        g_pt2_tu = 0.6f; g_pt2_tv = 0.55f;

        LoadMapTexture(g_current_map_index, targetFloor);
    }
}

void LoaderLoop() {
    while (g_loader_on) {
        PendingTex task{-1,-1}; {
            std::lock_guard<std::mutex> lk(g_pending_mtx);
            for (auto& t : g_pending) if (!t.px && !t.ready && !t.fail) { task = t; break; }
        }
        if (task.map < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
        int sf = SafeClampFloorIdx(task.map, task.floor);
        if (task.map >= (int)g_all_maps.size() || sf >= (int)g_all_maps[task.map].size()) {
            std::lock_guard<std::mutex> lk(g_pending_mtx);
            for (auto& t : g_pending) if (t.map == task.map && t.floor == task.floor) t.fail = true;
            continue;
        }
        int w, h, n;
        unsigned char* d = stbi_load(g_all_maps[task.map][sf].texturePath, &w, &h, &n, 4);
        { std::lock_guard<std::mutex> lk(g_pending_mtx);
            for (auto& t : g_pending) if (t.map == task.map && t.floor == task.floor && !t.ready) {
                if (d) { t.px = d; t.w = w; t.h = h; t.ready = true; } else t.fail = true;
            }
        }
        if (!d) stbi_image_free(d);
    }
}

void StartLoader() { if (!g_loader_on) { g_loader_on = true; g_loader = std::thread(LoaderLoop); g_loader.detach(); } }

void FlushTextures() {
    std::lock_guard<std::mutex> lk(g_pending_mtx);
    for (size_t i = 0; i < g_pending.size(); ) {
        auto& t = g_pending[i]; if (!t.ready) { i++; continue; }
        // ★ P3: 大纹理分帧上传，每帧256行，避免单帧卡顿
        if (t.w > 1024 || t.h > 1024) {
            if (t.upload_row == 0) {
                if (g_map_textures[t.map][t.floor]) { glDeleteTextures(1, &g_map_textures[t.map][t.floor]); g_map_textures[t.map][t.floor] = 0; }
                glGenTextures(1, &t.tex);
                if (!t.tex) { t.fail = true; i++; continue; }
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t.tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
            int end = std::min(t.upload_row + 256, t.h);
            glBindTexture(GL_TEXTURE_2D, t.tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, t.upload_row, t.w, end - t.upload_row, GL_RGBA, GL_UNSIGNED_BYTE, t.px + t.upload_row * t.w * 4);
            t.upload_row = end;
            if (t.upload_row >= t.h) {
                g_map_textures[t.map][t.floor] = t.tex;
                g_map_texture_w[t.map][t.floor] = t.w; g_map_texture_h[t.map][t.floor] = t.h;
                stbi_image_free(t.px); t.px = nullptr; t.uploaded = true;
            } else { i++; continue; }
        } else {
            if (g_map_textures[t.map][t.floor]) { glDeleteTextures(1, &g_map_textures[t.map][t.floor]); g_map_textures[t.map][t.floor] = 0; }
            GLuint tex = 0; glGenTextures(1, &tex);
            if (!tex) { t.fail = true; i++; continue; }
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.px);
            if (glGetError() != GL_NO_ERROR) { glDeleteTextures(1, &tex); stbi_image_free(t.px); t.fail = true; i++; continue; }
            g_map_textures[t.map][t.floor] = tex; g_map_texture_w[t.map][t.floor] = t.w; g_map_texture_h[t.map][t.floor] = t.h;
            stbi_image_free(t.px); t.px = nullptr; t.uploaded = true;
        }
        i++;
    }
    g_pending.erase(std::remove_if(g_pending.begin(), g_pending.end(), [](const PendingTex& t) { return t.uploaded || t.fail; }), g_pending.end());
}

void LoadMapTexture(int mapIdx, int floorIdx) {
    if (mapIdx < 0 || mapIdx >= MAX_MAP_COUNT || floorIdx < 0 || floorIdx >= MAX_FLOOR_COUNT) return;
    StartLoader();
    { std::lock_guard<std::mutex> lk(g_pending_mtx);
        g_pending.erase(std::remove_if(g_pending.begin(), g_pending.end(), [=](const PendingTex& t) { return t.map == mapIdx && t.floor == floorIdx; }), g_pending.end());
        g_pending.push_back({mapIdx, floorIdx}); }
}

// 辅助函数：检查玩家坐标是否在指定地图的任意楼层范围内
bool IsPlayerInMapBounds(int mapIndex, const Vector3A& playerPos) {
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
int FindMapByPlayerPos(const Vector3A& playerPos) {
    for (int i = 0; i < (int)g_all_maps.size(); i++) {
        if (IsPlayerInMapBounds(i, playerPos)) return i;
    }
    return -1;
}

// =============================================================
// 地图识别 + 楼层判定（v2.1）
// =============================================================
// 1) 地图识别：使用原有的多特征指纹匹配（音乐盒/凳子/钢琴评分）
// 2) 楼层判定：Z > 190 → 2楼, Z ≤ 190 → 1楼（与物品过滤阈值统一）
// 3) 瞬移重检：单帧跳跃 > 80(XY)/50(Z) → 立即触发完整重检
// 4) 路径隔离：按地图索引+楼层索引严格分离
// =============================================================
void TryAutoDetectMap(const std::vector<DataStruct>& data) {
    if (!g_map_auto_detect || !g_map_enabled) {
        g_detect_phase = MapDetectPhase::LOCKED;
        g_detect_debounce_frames = 0;
        snprintf(g_map_detect_debug, sizeof(g_map_detect_debug),
            "Auto识别关闭 (auto_detect=%d, map_enabled=%d)", g_map_auto_detect, g_map_enabled);
        return;
    }
    if (g_fingerprint_db.empty()) LoadFingerprintDB();
    
    // Step1: 扫描信号源（音乐盒/钢琴/凳子）
    Vector3A musicbox_pos{}, piano_pos{};
    bool musicbox_found = false, piano_found = false;
    g_detected_chairs.clear();
    for (const auto& item : data) {
        if (item.阵营 == 6 || item.阵营 == 4) {
            if (strstr(item.类名, "prop_musicbox")) {
                musicbox_pos = getObjectCoordinates(item.objcoor, false);
                if (isValidCoordinate(musicbox_pos) && (fabsf(musicbox_pos.X) > 1.0f || fabsf(musicbox_pos.Y) > 1.0f)) {
                    musicbox_found = true; g_detected_musicbox_pos = musicbox_pos;
                }
            }
            if (strstr(item.类名, "random01_in_piano01.gim")) {
                piano_pos = getObjectCoordinates(item.objcoor, false);
                if (isValidCoordinate(piano_pos) && (fabsf(piano_pos.X) > 1.0f || fabsf(piano_pos.Y) > 1.0f)) {
                    piano_found = true; g_detected_piano_pos = piano_pos;
                }
            }
            if (strstr(item.类名, "rd01_in_pianochair01.gim")) {
                Vector3A cp = getObjectCoordinates(item.objcoor, false);
                if (isValidCoordinate(cp) && (fabsf(cp.X) > 1.0f || fabsf(cp.Y) > 1.0f)) g_detected_chairs.push_back(cp);
            }
        }
    }
    
    // ★★★ 音乐盒精确匹配 — 第一优先级 ★★★
    // v2.34: 使用 g_fingerprint_db 搜索匹配指纹，通过 ExecuteMapSwitch() 正确解析 fp_id→g_all_maps
    if (musicbox_found && g_detected_musicbox_pos.X != 0) {
        // 收集所有匹配的指纹ID (XYZ三轴容差5)
        struct FpCandidate { int fp_id; float dist; };
        std::vector<FpCandidate> fp_candidates;
        for (auto& fp : g_fingerprint_db) {
            if (!fp.valid || fp.musicBox.X == 0.0f) continue;
            float dx = g_detected_musicbox_pos.X - fp.musicBox.X;
            float dy = g_detected_musicbox_pos.Y - fp.musicBox.Y;
            float dz = g_detected_musicbox_pos.Z - fp.musicBox.Z;
            float d = sqrtf(dx*dx + dy*dy + dz*dz);
            if (d < 5.0f) fp_candidates.push_back({fp.id, d});
        }
        
        if (!fp_candidates.empty()) {
            int best_fp = -1;
            if (fp_candidates.size() == 1) {
                // ★ 唯一指纹 — 直接命中
                best_fp = fp_candidates[0].fp_id;
            } else {
                // ★ 同名音乐盒多指纹 — 用钢琴/凳子区分
                // 优先钢琴精确匹配（直接从指纹DB查询）
                if (piano_found && g_detected_piano_pos.X != 0) {
                    for (auto& fc : fp_candidates) {
                        for (auto& fp2 : g_fingerprint_db) {
                            if (fp2.id != fc.fp_id || !fp2.valid || fp2.pianos.empty()) continue;
                            for (auto& pkpos : fp2.pianos) {
                                float d = sqrtf((g_detected_piano_pos.X - pkpos.X)*(g_detected_piano_pos.X - pkpos.X)
                                              + (g_detected_piano_pos.Y - pkpos.Y)*(g_detected_piano_pos.Y - pkpos.Y));
                                if (d < 8.0f) { best_fp = fc.fp_id; break; }
                            }
                            if (best_fp >= 0) break;
                        }
                        if (best_fp >= 0) break;
                    }
                }
                // 钢琴不匹配 → 用凳子数量匹配
                if (best_fp < 0) {
                    int det_c = (int)g_detected_chairs.size();
                    int best_diff = 999;
                    for (auto& fc : fp_candidates) {
                        for (auto& fp2 : g_fingerprint_db) {
                            if (fp2.id != fc.fp_id || !fp2.valid) continue;
                            int diff = abs((int)fp2.stools.size() - det_c);
                            if (diff < best_diff) { best_diff = diff; best_fp = fc.fp_id; }
                        }
                    }
                }
            }
            
            if (best_fp >= 0) {
                // 通过 ExecuteMapSwitch 正确解析 fp_id → g_all_maps
                int tgt_check = (best_fp < (int)g_mapidx_from_fp_id.size()) ? g_mapidx_from_fp_id[best_fp] : -1;
                if (tgt_check >= 0 && tgt_check < (int)g_all_maps.size() && tgt_check != g_current_map_index) {
                    ExecuteMapSwitch(best_fp);
                    g_detect_phase = MapDetectPhase::LOCKED;
                    snprintf(g_map_detect_debug, sizeof(g_map_detect_debug), "Musicbox: (%.0f,%.0f,%.0f) fp=%d -> %s",
                        g_detected_musicbox_pos.X, g_detected_musicbox_pos.Y, g_detected_musicbox_pos.Z,
                        best_fp, g_all_maps[tgt_check][0].name);
                    AddNotification("Mbox→" + std::string(g_all_maps[tgt_check][0].name), 1.5f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                    return;
                }
            }
        }
    }
    
    // ★★★ 钢琴精确匹配 — 第二优先级（无音乐盒时兜底）★★★
    if (piano_found && g_detected_piano_pos.X != 0) {
        int best_mi = -1; float best_d = 10.0f;
        for (auto& pk : g_piano_db) {
            float d = sqrtf((g_detected_piano_pos.X - pk.x)*(g_detected_piano_pos.X - pk.x)
                          + (g_detected_piano_pos.Y - pk.y)*(g_detected_piano_pos.Y - pk.y));
            if (d < best_d) { best_d = d; best_mi = pk.mapIndex; }
        }
        if (best_mi >= 0 && best_mi < (int)g_all_maps.size() && best_mi != g_current_map_index) {
            g_current_map_index = best_mi;
            g_current_floor_index = SafeClampFloorIdx(best_mi, GetFloorFromPlayerZ(Z));
            LoadMapTexture(g_current_map_index, g_current_floor_index);
            g_detect_phase = MapDetectPhase::LOCKED;
            snprintf(g_map_detect_debug, sizeof(g_map_detect_debug), "Piano: (%.0f,%.0f) -> %s",
                g_detected_piano_pos.X, g_detected_piano_pos.Y, g_all_maps[best_mi][0].name);
            AddNotification("已识别: " + std::string(g_all_maps[best_mi][0].name), 1.5f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            return;
        }
    }
    
    // Step2: 检测传送（瞬移重检）
    static int g_switch_cooldown = 0;
    if (g_switch_cooldown > 0) {
        g_switch_cooldown--;
        // 冷却期内更新位置历史但不触发新的传送检测
        if (Z.X != 0.0f || Z.Y != 0.0f) {
            g_prev_player_pos = Z;
            g_has_prev_pos = true;
        }
    } else {
        TeleportType tp = DetectPlayerTeleport(Z);
        if (tp == TeleportType::MAP_SWITCH) {
            printf("[MapDetect] SWITCH XY jumped\n");
            ResetObjectCacheOnMapSwitch();
            g_detect_phase = MapDetectPhase::SWITCH_DETECTED;
            g_detect_debounce_frames = DETECT_DEBOUNCE_FRAMES;
            g_switch_cooldown = 120; // 2秒冷却：防止瞬移时连续触发导致崩溃
            snprintf(g_detect_status_text, sizeof(g_detect_status_text), "SWITCH...");
            snprintf(g_map_detect_debug, sizeof(g_map_detect_debug), "New: SWITCH dist=%.0f",
                sqrtf((Z.X-g_prev_player_pos.X)*(Z.X-g_prev_player_pos.X)+(Z.Y-g_prev_player_pos.Y)*(Z.Y-g_prev_player_pos.Y)));
            if (g_current_map_index >= 0) {
                int newFloor = GetFloorFromPlayerZ(Z);
                int clamped = SafeClampFloorIdx(g_current_map_index, newFloor);
                if (clamped != g_current_floor_index) {
                    g_current_floor_index = clamped;
                    g_last_paths_map_idx = -1;
                }
            }
        } else if (tp == TeleportType::FLOOR_CHANGE) {
            // Z 轴瞬移 → 楼层切换
            int nf = GetFloorFromPlayerZ(Z);
            g_current_floor_index = SafeClampFloorIdx(g_current_map_index, nf);
            g_last_paths_map_idx = -1;
            LoadMapTexture(g_current_map_index, g_current_floor_index);
            g_detect_phase = MapDetectPhase::LOCKED;
            snprintf(g_map_detect_debug, sizeof(g_map_detect_debug), "New: FLOOR z=%.0f->%d", Z.Z, nf);
            AddNotification(nf == 0 ? "切换到1楼" : "切换到2楼", 1.5f, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
            return;
        }
    }
    
    // Step3: 状态机（保留原有的指纹匹配流程）
    if (g_detect_phase == MapDetectPhase::SWITCH_DETECTED || g_detect_phase == MapDetectPhase::IDENTIFYING) {
        MapScoreResult sr = ScoreMapFingerprints(musicbox_pos, musicbox_found, piano_pos, piano_found, g_detected_chairs);
        g_detect_best_score = sr.score; g_detect_best_fp_id = sr.fp_id;
        snprintf(g_score_debug_buf, sizeof(g_score_debug_buf), "%s", sr.debug_text.c_str());
        if (g_detect_debounce_frames > 0) {
            g_detect_debounce_frames--;
            g_detect_phase = MapDetectPhase::IDENTIFYING;
            if (g_detect_debounce_frames <= 0) {
                if (sr.fp_id >= 0) {
                    bool sw = (sr.score >= 60.0f && !sr.is_tie) || (sr.score >= 37.0f && !sr.is_tie && (sr.score - sr.second_score) >= 20.0f);
                    if (sw) {
                        ExecuteMapSwitch(sr.fp_id);
                        g_detect_phase = MapDetectPhase::LOCKED;
                        snprintf(g_detect_status_text, sizeof(g_detect_status_text), "OK map[%d] %.0f分", sr.fp_id, sr.score);
                    } else {
                        g_detect_phase = MapDetectPhase::LOW_CONFIDENCE;
                        g_low_confidence_counter = 0;
                        snprintf(g_detect_status_text, sizeof(g_detect_status_text), "低置信度 %.0f分", sr.score);
                    }
                } else {
                    g_detect_phase = MapDetectPhase::LOW_CONFIDENCE;
                    g_low_confidence_counter = 0;
                }
            }
        }
        return;
    }
    if (g_detect_phase == MapDetectPhase::LOW_CONFIDENCE) {
        MapScoreResult sr = ScoreMapFingerprints(musicbox_pos, musicbox_found, piano_pos, piano_found, g_detected_chairs);
        g_detect_best_score = sr.score; g_detect_best_fp_id = sr.fp_id;
        snprintf(g_score_debug_buf, sizeof(g_score_debug_buf), "%s", sr.debug_text.c_str());
        g_low_confidence_counter++;
        if (sr.fp_id >= 0 && sr.score >= 60.0f && !sr.is_tie) {
            // 预检：fp_id 必须有映射才能切换
            int tgt = (sr.fp_id < (int)g_mapidx_from_fp_id.size()) ? g_mapidx_from_fp_id[sr.fp_id] : -1;
            if (tgt >= 0 && tgt < (int)g_all_maps.size()) {
                ExecuteMapSwitch(sr.fp_id);
                g_detect_phase = MapDetectPhase::LOCKED;
                g_low_confidence_counter = 0;
            }
            // 没映射 → 留在 LOW_CONFIDENCE 等超时
        }
        if (g_low_confidence_counter > LOW_CONFIDENCE_TIMEOUT) {
            g_detect_phase = MapDetectPhase::LOCKED;
            g_low_confidence_counter = 0;
        }
        return;
    }
    // LOCKED: 常规验证
    if (musicbox_found || piano_found || !g_detected_chairs.empty()) {
        MapScoreResult sr = ScoreMapFingerprints(musicbox_pos, musicbox_found, piano_pos, piano_found, g_detected_chairs);
        snprintf(g_score_debug_buf, sizeof(g_score_debug_buf), "%s", sr.debug_text.c_str());
        if (sr.fp_id >= 0 && sr.score >= 60.0f && !sr.is_tie) {
            int tgt = (sr.fp_id < (int)g_mapidx_from_fp_id.size()) ? g_mapidx_from_fp_id[sr.fp_id] : -1;
            
            if (g_current_map_index < 0) {
                // ★ 首次检测：直接切换到正确地图
                if (tgt >= 0 && tgt < (int)g_all_maps.size()) {
                    ExecuteMapSwitch(sr.fp_id);
                    g_detect_phase = MapDetectPhase::LOCKED;
                    printf("[MapDetect] 首次检测: fp=%d (%.0f分) → map[%d]\n",
                        sr.fp_id, sr.score, g_current_map_index);
                } else {
                    // fp_id 未关联到任何地图，跳到 LOW_CONFIDENCE 等待用户手动选择
                    printf("[MapDetect] fp_id=%d 未关联到 g_all_maps 索引，转到 LOW_CONFIDENCE\n", sr.fp_id);
                    g_detect_phase = MapDetectPhase::LOW_CONFIDENCE;
                    g_low_confidence_counter = 0;
                }
            } else if (tgt >= 0 && tgt != g_current_map_index && sr.score >= 65.0f) {
                // ★ v2.34: 评分高分指向不同地图 → 直接切换（解决"评分18却匹配1"的问题）
                printf("[MapDetect] LOCKED评分纠正: fp=%d (%.0f分) → map[%d] (原map[%d])\n",
                    sr.fp_id, sr.score, tgt, g_current_map_index);
                ExecuteMapSwitch(sr.fp_id);
                g_detect_phase = MapDetectPhase::LOCKED;
            } else {
                // ★ 当前地图正确，仅更新映射关系
                int cfp = (g_current_map_index < (int)g_fp_id_from_mapidx.size()) ? g_fp_id_from_mapidx[g_current_map_index] : -1;
                if (cfp < 0 && g_current_map_index < (int)g_all_maps.size() && sr.fp_id < (int)g_mapidx_from_fp_id.size() && g_mapidx_from_fp_id[sr.fp_id] < 0) {
                    g_mapidx_from_fp_id[sr.fp_id] = g_current_map_index;
                    if (g_current_map_index >= (int)g_fp_id_from_mapidx.size()) g_fp_id_from_mapidx.resize(g_current_map_index+1, -1);
                    g_fp_id_from_mapidx[g_current_map_index] = sr.fp_id;
                }
            }
        }
        if (musicbox_found) { g_cached_musicbox_pos = musicbox_pos; g_has_cached_musicbox = true; }
        if (piano_found) { g_cached_piano_pos = piano_pos; g_has_cached_piano = true; }
    }
    snprintf(g_map_detect_debug, sizeof(g_map_detect_debug), "New: OK fp=%d s=%.1f", g_detect_best_fp_id, g_detect_best_score);
}

void LoadMapConfigFromJSON() {
    mkdir(MAPS_ROOT, 0755); // 确保数据目录存在
    const std::string json_path = MAPS_ROOT "map_config.json";
    bool file_exists = std::filesystem::exists(json_path);
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
        std::string path = MAPS_ROOT "map" + std::to_string(i) + "_floor1.png";
        if (std::filesystem::exists(path)) {
            map_ids.insert(i);
        } else if (i > 1 && map_ids.find(i - 1) == map_ids.end()) {
            break;
        }
    }

    bool json_modified = false;
    for (int id : map_ids) {
        std::string expected_name = "地图" + std::to_string(id) + " 一楼";
        std::string expected_path = MAPS_ROOT "map" + std::to_string(id) + "_floor1.png";

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
            new_map["floor_z_threshold"] = 250.0;
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
        // 原文件存在且成功加载 → 仅备份，不覆盖（防止新增地图检测导致数据丢失）
        // 用户新增的出口/路径等数据只通过 SaveExitsToJSON / SavePlayerPathsToJSON 写入
        if (file_exists) {
            std::string bak_path = json_path + ".bak";
            try {
                std::filesystem::copy(json_path, bak_path, std::filesystem::copy_options::overwrite_existing);
            } catch (...) {}
        } else {
            // 文件不存在（首次运行）→ 创建初始配置
            std::ofstream ofs(json_path);
            if (ofs) ofs << j.dump(4);
        }
    }

    g_all_maps.clear();
    g_musicbox_db.clear();
    g_piano_db.clear();
    g_chair_db.clear();
    g_exits.clear();
    g_exit_uvs.clear();
    g_saved_paths.clear();
    g_saved_paths_by_map.clear();

    static std::list<std::string> dynamic_names;
    static std::list<std::string> dynamic_paths;

    for (auto& m : j["maps"]) {
        int idx = static_cast<int>(g_all_maps.size());

        MapConfig cfg;
        std::string map_name = m.value("name", "未知地图");
        std::string map_tex = m.value("texture", "");
        if (map_tex.empty()) map_tex = m.value("texturePath", ""); // 兼容旧 key
        if (!map_tex.empty() && map_tex[0] != '/') map_tex = MAPS_ROOT + map_tex; // 相对路径 → 绝对路径
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
        cfg.floorZThreshold = m.value("floor_z_threshold", 250.0f);

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

        while (g_exits.size() <= idx) { g_exits.push_back({}); g_exit_uvs.push_back({}); g_saved_paths_by_map.push_back({}); }
        while (g_exits[idx].size() <= cfg.floorIndex) { g_exits[idx].push_back({}); g_exit_uvs[idx].push_back({}); g_saved_paths_by_map[idx].push_back({}); }
        g_exits[idx][cfg.floorIndex].clear();
        g_exit_uvs[idx][cfg.floorIndex].clear();
        if (m.contains("exits") && m["exits"].is_array()) {
            for (auto& exit_json : m["exits"]) {
                if (exit_json.contains("x") && exit_json.contains("y")) {
                    Vector3A exitPos;
                    exitPos.X = exit_json["x"];
                    exitPos.Y = exit_json["y"];
                    exitPos.Z = exit_json.value("z", 0.0f);
                    g_exits[idx][cfg.floorIndex].push_back(exitPos);
                    // 从世界坐标计算UV（作为初始值，用户后续可通过拖拽微调）
                    float u = exitPos.X * cfg.scaleX + cfg.offsetU;
                    float v = exitPos.Y * cfg.scaleY + cfg.offsetV;
                    if (cfg.flipX) u = 1.0f - u;
                    if (cfg.flipY) v = 1.0f - v;
                    g_exit_uvs[idx][cfg.floorIndex].push_back(ImVec2(u, v));
                }
            }
        }

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
                g_saved_paths_by_map[idx][cfg.floorIndex].push_back(path);
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

    // ★ JSON去重：同地图号的"一楼""二楼"独立slot → 合并
    {
        std::vector<int> dup_slots; // 待删除的重复 slot
        for (int i = 0; i < (int)g_all_maps.size(); i++) {
            if (g_all_maps[i].empty()) continue;
            const char* ni = g_all_maps[i][0].name;
            if (!ni || strncmp(ni, "地图", 6) != 0) continue;
            int num_i = atoi(ni + 6);

            for (int j = i + 1; j < (int)g_all_maps.size(); j++) {
                if (g_all_maps[j].empty()) continue;
                const char* nj = g_all_maps[j][0].name;
                if (!nj || strncmp(nj, "地图", 6) != 0) continue;
                int num_j = atoi(nj + 6);
                if (num_i != num_j) continue;

                // j 是重复的，把它的数据合并到 i，然后标记 j 删除
                for (int fj = 0; fj < (int)g_all_maps[j].size(); fj++) {
                    int tf = g_all_maps[j][fj].floorIndex;
                    while (tf >= (int)g_all_maps[i].size())
                        g_all_maps[i].push_back(g_all_maps[i][0]);
                    g_all_maps[i][tf] = g_all_maps[j][fj];
                }
                dup_slots.push_back(j);
            }
        }
        // 从后往前删，避免索引偏移
        std::sort(dup_slots.begin(), dup_slots.end());
        dup_slots.erase(std::unique(dup_slots.begin(), dup_slots.end()), dup_slots.end());
        for (int k = (int)dup_slots.size() - 1; k >= 0; k--)
            g_all_maps.erase(g_all_maps.begin() + dup_slots[k]);
    }
}

// =============================================================
// 加载地图指纹数据库 (v3.0)
// 从 JSON 文件中解析每张地图的音乐盒、凳子、钢琴坐标
// =============================================================
// =============================================================
// 加载地图指纹数据库 (v4.0 — 复合指纹)
// 优先加载 enhanced_fingerprints.json (日志自动生成), 兜底 musicbox_stools.json
// =============================================================
void LoadFingerprintDB() {
    g_fingerprint_db.clear();
    g_mapidx_from_fp_id.clear();

    // ── 尝试路径列表 ──
    const std::vector<std::string> paths = {
        MAPS_ROOT "musicbox_stools.json",        // 主: 音乐盒+凳子指纹
        "/sdcard/map_fingerprints.json",          // 兼容旧版
    };

    std::ifstream ifs;
    std::string loaded_path;
    for (const auto& p : paths) { ifs.open(p); if (ifs) { loaded_path = p; break; } }
    if (!ifs) { printf("[MapDetect] 指纹文件不存在\n"); return; }

    try {
        json root;
        ifs >> root; ifs.close();
        printf("[MapDetect] 加载: %s\n", loaded_path.c_str());

        // 判断格式: enhanced_fingerprints.json 是对象 { fingerprints: [...] }
        json fp_array;
        bool is_enhanced = false;
        if (root.is_object() && root.contains("fingerprints")) {
            fp_array = root["fingerprints"];
            is_enhanced = true;
        } else if (root.is_array()) {
            fp_array = root;
        } else {
            printf("[MapDetect] 未知指纹格式\n"); return;
        }

        for (const auto& entry : fp_array) {
            if (!entry.contains("id")) continue;
            MapFingerprint fp;
            fp.id = entry["id"];

            // ── 增强格式 (v4.0): 含钢琴/音乐盒坐标 + 物体计数 ──
            if (is_enhanced) {
                fp.piano.X = entry.value("piano_x", 0.0f);
                fp.piano.Y = entry.value("piano_y", 0.0f);
                fp.piano.Z = entry.value("piano_z", 0.0f);
                fp.musicBox.X = entry.value("musicbox_x", 0.0f);
                fp.musicBox.Y = entry.value("musicbox_y", 0.0f);
                fp.musicBox.Z = entry.value("musicbox_z", 0.0f);
                fp.chairCount = entry.value("chair_count", 0);
                fp.coreDoorCount = entry.value("core_door_count", 0);
                fp.outdoorDoorCount = entry.value("outdoor_door_count", 0);
                fp.propDoorCount = entry.value("prop_door_count", 0);
                fp.woodplaneCount = entry.value("woodplane_count", 0);

                // 凳子
                if (entry.contains("chairs") && entry["chairs"].is_array()) {
                    for (const auto& c : entry["chairs"]) {
                        Vector3A s;
                        s.X = c.value("x", 0.0f); s.Y = c.value("y", 0.0f); s.Z = c.value("z", 0.0f);
                        fp.stools.push_back(s);
                    }
                }
                // 钢琴存入 pianos 数组(兼容旧评分)
                if (fp.piano.X != 0 || fp.piano.Y != 0) fp.pianos.push_back(fp.piano);
            } else {
                // ── 旧格式 ──
                if (entry.contains("music_box") && entry["music_box"].is_array() && entry["music_box"].size() >= 3) {
                    fp.musicBox.X = entry["music_box"][0]; fp.musicBox.Y = entry["music_box"][1]; fp.musicBox.Z = entry["music_box"][2];
                }
                if (entry.contains("stools") && entry["stools"].is_array()) {
                    for (const auto& s : entry["stools"]) {
                        if (s.is_array() && s.size() >= 3) {
                            Vector3A v; v.X = s[0]; v.Y = s[1]; v.Z = s[2]; fp.stools.push_back(v);
                        }
                    }
                }
                if (entry.contains("pianos") && entry["pianos"].is_array()) {
                    for (const auto& p : entry["pianos"]) {
                        if (p.is_array() && p.size() >= 3) {
                            Vector3A v; v.X = p[0]; v.Y = p[1]; v.Z = p[2]; fp.pianos.push_back(v);
                        }
                    }
                }
                fp.chairCount = (int)fp.stools.size();
                fp.piano = fp.pianos.empty() ? Vector3A{} : fp.pianos[0];
            }

            fp.valid = true;
            g_fingerprint_db.push_back(fp);
        }

        printf("[MapDetect] 加载 %zu 指纹%s\n", g_fingerprint_db.size(), is_enhanced ? " (增强复合指纹)" : "");

        // ── 重建 g_mapidx_from_fp_id ──
        int max_fp_id = 0;
        for (auto& fp : g_fingerprint_db) if (fp.id > max_fp_id) max_fp_id = fp.id;
        g_mapidx_from_fp_id.assign(max_fp_id + 1, -1);

        for (auto& fp : g_fingerprint_db) {
            // ★ v4.0: 优先用钢琴坐标匹配到 g_all_maps 索引
            if ((fp.piano.X != 0 || fp.piano.Y != 0) && !g_piano_db.empty()) {
                for (auto& pk : g_piano_db) {
                    float d = sqrtf((fp.piano.X - pk.x)*(fp.piano.X - pk.x)
                                  + (fp.piano.Y - pk.y)*(fp.piano.Y - pk.y));
                    if (d < 5.0f) { fp.mapIndex = pk.mapIndex; break; }
                }
            }
            // 其次用音乐盒坐标匹配
            if (fp.mapIndex < 0 && (fp.musicBox.X != 0 || fp.musicBox.Y != 0) && !g_musicbox_db.empty()) {
                for (auto& mb : g_musicbox_db) {
                    float d = sqrtf((fp.musicBox.X - mb.x)*(fp.musicBox.X - mb.x)
                                  + (fp.musicBox.Y - mb.y)*(fp.musicBox.Y - mb.y));
                    if (d < 5.0f) { fp.mapIndex = mb.mapIndex; break; }
                }
            }
            // 最后回退到按名字数字匹配
            if (fp.mapIndex < 0) {
                for (int mi = 0; mi < (int)g_all_maps.size(); mi++) {
                    if (g_all_maps[mi].empty()) continue;
                    const char* name = g_all_maps[mi][0].name;
                    if (name && strncmp(name, "地图", 6) == 0 && atoi(name + 6) == fp.id) {
                        fp.mapIndex = mi; break;
                    }
                }
            }
            // 记录到 g_mapidx_from_fp_id
            if (fp.mapIndex >= 0 && fp.id < (int)g_mapidx_from_fp_id.size())
                g_mapidx_from_fp_id[fp.id] = fp.mapIndex;
        }

        // 重建反向映射
        g_fp_id_from_mapidx.assign(g_all_maps.size(), -1);
        for (int fp_id = 0; fp_id < (int)g_mapidx_from_fp_id.size(); fp_id++) {
            int mi = g_mapidx_from_fp_id[fp_id];
            if (mi >= 0 && mi < (int)g_fp_id_from_mapidx.size())
                g_fp_id_from_mapidx[mi] = fp_id;
        }

    } catch (const std::exception& e) {
        printf("[MapDetect] 指纹加载异常: %s\n", e.what());
    }
}

// =============================================================
// 辅助：根据 g_all_maps 状态重建指纹→地图索引映射
// 在 LoadMapConfigFromJSON 之后调用
// =============================================================
void RebuildFingerprintMapping() {
    g_fp_id_from_mapidx.assign(g_all_maps.size(), -1);

    // 用 g_mapidx_from_fp_id 重建反向映射
    for (int fp_id = 0; fp_id < (int)g_mapidx_from_fp_id.size(); fp_id++) {
        int mi = g_mapidx_from_fp_id[fp_id];
        if (mi >= 0 && mi < (int)g_fp_id_from_mapidx.size()) {
            g_fp_id_from_mapidx[mi] = fp_id;
        }
    }
}

// =============================================================
// 地图评分函数 (v3.0)
// 全量指纹评分: 音乐盒(XY+Z) + 凳子位置重合度
// =============================================================
MapScoreResult ScoreMapFingerprints(
    const Vector3A& musicbox_pos, bool musicbox_found,
    const Vector3A& piano_pos, bool piano_found,
    const std::vector<Vector3A>& detected_chairs)
{
    MapScoreResult result;
    if (g_fingerprint_db.empty()) return result;

    int detected_count = (int)detected_chairs.size();

    struct FpScored {
        int fp_id;
        float total;
        float ms;  // music score
        float sc;  // stool count
        float sp;  // stool position
        float ps;  // piano score
    };
    std::vector<FpScored> scored_list;

    // ★ 全量评分 (无预筛, 23张地图每帧都跑也是毫秒级)
    for (size_t fi = 0; fi < g_fingerprint_db.size(); fi++) {
        auto& fp = g_fingerprint_db[fi];
        if (!fp.valid) continue;

        float musicBoxScore = 0.0f;
        float stoolCountScore = 0.0f;
        float stoolPosScore = 0.0f;
        float pianoScore = 0.0f;

        // 1) 音乐盒匹配 (0~40)——加入Z轴, 容差5单位吸收波动, >25不计数
        if (musicbox_found && fp.musicBox.X != 0.0f) {
            float dx = musicbox_pos.X - fp.musicBox.X;
            float dy = musicbox_pos.Y - fp.musicBox.Y;
            float dz = musicbox_pos.Z - fp.musicBox.Z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist <= 5.0f) musicBoxScore = 40.0f;
            else if (dist < 25.0f) musicBoxScore = 40.0f - (dist - 5.0f) * 2.0f;
        }

        // 2) 凳子数量匹配 (0~10)——考虑到预筛已过，这里加分更精确
        if (fp.chairCount == detected_count) stoolCountScore = 10.0f;
        else if (abs(fp.chairCount - detected_count) == 1) stoolCountScore = 5.0f;

        // 3) ★ 凳子位置重合度 (0~50)——决胜关键，阈值<4.0
        if (fp.stools.empty() && detected_chairs.empty()) {
            stoolPosScore = 50.0f;
        } else if (fp.stools.empty() || detected_chairs.empty()) {
            stoolPosScore = 0.0f;
        } else {
            int matched = 0;
            for (auto& d : detected_chairs) {
                float best = 1e10f;
                for (auto& k : fp.stools) {
                    float dx = d.X - k.X;
                    float dy = d.Y - k.Y;
                    float dz = d.Z - k.Z;
                    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                    if (dist < best) best = dist;
                }
                if (best < 4.0f) matched++;
            }
            // 按比例换算到50分：全部匹配=50，部分匹配线性递减
            float ratio = (float)matched / (float)std::max(1, (int)fp.stools.size());
            stoolPosScore = ratio * 50.0f;
        }

        // 4) ★ 钢琴纯加分 (0~10)——没扫到默认10分，扫到匹配上加分
        bool fp_has_piano = !fp.pianos.empty();
        if (piano_found && fp_has_piano) {
            float best_dist = 1e10f;
            for (auto& kp : fp.pianos) {
                float dx = piano_pos.X - kp.X;
                float dy = piano_pos.Y - kp.Y;
                float dist = sqrtf(dx*dx + dy*dy);
                if (dist < best_dist) best_dist = dist;
            }
            if (best_dist < 3.0f) pianoScore = 10.0f;
            else if (best_dist < 8.0f) pianoScore = 5.0f;
        } else if (!piano_found) {
            pianoScore = 10.0f;  // 没扫到钢琴→中性分
        }
        // 扫到了但已知无钢琴 → 0（不扣分，纯不加）

        float total = musicBoxScore + stoolCountScore + stoolPosScore + pianoScore;
        if (total > 110.0f) total = 110.0f;  // 上限110分
        scored_list.push_back({fp.id, total, musicBoxScore, stoolCountScore, stoolPosScore, pianoScore});
    }

    if (scored_list.empty()) return result;

    // 按总分降序排列
    std::sort(scored_list.begin(), scored_list.end(),
        [](const FpScored& a, const FpScored& b) { return a.total > b.total; });

    auto& best = scored_list[0];
    result.fp_id = best.fp_id;
    result.score = best.total;
    result.scores[0] = best.ms;
    result.scores[1] = best.sc;
    result.scores[2] = best.sp;
    result.scores[3] = best.ps;

    // 检测并列
    if (scored_list.size() >= 2) {
        result.second_score = scored_list[1].total;
        if (fabsf(scored_list[0].total - scored_list[1].total) < 0.5f) {
            result.is_tie = true;
        }
    }

    // 生成调试文本（前3名）
    char buf[2048];
    int pos = 0;
    pos += snprintf(buf, sizeof(buf), "评分: ");
    int show = std::min(3, (int)scored_list.size());
    for (int i = 0; i < show; i++) {
        int chars = snprintf(buf + pos, sizeof(buf) - pos,
            "\n #%d fp[%d]: %.1f/110 [M%.0f+C%.0f+S%.0f+P%.0f]",
            i+1, scored_list[i].fp_id, scored_list[i].total,
            scored_list[i].ms, scored_list[i].sc, scored_list[i].sp, scored_list[i].ps);
        if (chars > 0) pos += chars;
        if (pos >= (int)sizeof(buf) - 20) break;
    }
    if (!scored_list.empty() && !result.is_tie && result.score < 60.0f && result.score >= 37.0f
        && (result.score - result.second_score) >= 20.0f) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, " (差距兜底)");
    }
    result.debug_text = buf;

    return result;
}

// =============================================================
// 新架构核心：检测玩家传送事件 (v3.0)
// =============================================================
TeleportType DetectPlayerTeleport(const Vector3A& current_pos) {
    if (!g_has_prev_pos) {
        g_prev_player_pos = current_pos;
        g_has_prev_pos = true;
        return TeleportType::NONE;
    }

    float dx = current_pos.X - g_prev_player_pos.X;
    float dy = current_pos.Y - g_prev_player_pos.Y;
    float dz = current_pos.Z - g_prev_player_pos.Z;
    float dist_xy = sqrtf(dx*dx + dy*dy);
    float dist_z = fabsf(dz);

    g_prev_player_pos = current_pos;

    // XY 方向大幅跳跃 → 地图切换
    if (dist_xy > TELEPORT_THRESHOLD_XY) {
        return TeleportType::MAP_SWITCH;
    }

    // Z 方向大幅跳跃 → 楼层切换
    if (dist_z > TELEPORT_THRESHOLD_Z) {
        return TeleportType::FLOOR_CHANGE;
    }

    return TeleportType::NONE;
}

// =============================================================
// 新架构核心：地图切换时重置物体状态 (v3.0)
// =============================================================
void ResetObjectCacheOnMapSwitch() {
    g_musicbox_moved = false;
    g_has_cached_musicbox = false;
    g_has_cached_piano = false;
    g_cached_chairs.clear();
    g_locked_stable_frames = 0; // 重置稳定帧计数器
    // 不重置 g_new_map_prompt_shown
}

// =============================================================
// 新架构核心：执行实际的地图切换 (v3.0)
// =============================================================
void ExecuteMapSwitch(int fp_id) {
    if (fp_id < 0 || fp_id >= (int)g_mapidx_from_fp_id.size()) return;

    int target_mi = g_mapidx_from_fp_id[fp_id];
    if (target_mi < 0 || target_mi >= (int)g_all_maps.size()) {
        // 指纹 ID 还没有关联到 g_all_maps
        printf("[MapDetect] fp_id=%d 未关联到 g_all_maps 索引\n", fp_id);
        return;
    }

    int prev_map = g_current_map_index;
    g_current_map_index = target_mi;
    g_current_floor_index = SafeClampFloorIdx(g_current_map_index, GetFloorFromPlayerZ(Z));
    LoadMapTexture(g_current_map_index, g_current_floor_index);

    ResetObjectCacheOnMapSwitch();

    snprintf(g_detect_status_text, sizeof(g_detect_status_text),
        "地图[%d] fp=%d (%.1f分)", target_mi, fp_id, g_detect_best_score);

    if (prev_map != target_mi) {
        printf("[MapDetect] 地图切换: [%d] → [%d] (fp_id=%d, score=%.1f)\n",
            prev_map, target_mi, fp_id, g_detect_best_score);
        AddNotification("地图切换: " + std::string(g_all_maps[target_mi][0].name),
            2.0f, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
    }
}
