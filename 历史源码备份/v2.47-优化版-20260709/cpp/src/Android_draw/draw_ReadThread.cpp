// draw_ReadThread.cpp
// Part of the draw_Gui.cpp split (v2.47).
// Contains the debug-log helpers and the data read thread entry point
// extracted from the original monolithic draw_Gui.cpp translation unit.

#include "draw_Gui_internal.h"

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
    snprintf(path, sizeof(path), "/data/local/bin/debug_%d.log", counter++);

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
        // v3.1 从服务端获取签名扫描参数 (无token则全部0, 扫描失败)
        uint32_t _magic_mat  = GAME_OFFSET(magic_matrix, 442745336);
        uint32_t _magic_dw   = GAME_OFFSET(magic_dword_check, 257);
        float    _magic_fl   = GAME_OFFSET(magic_float_check, 1.0f);
        int32_t  _sig_vdw    = GAME_OFFSET(sig_verify_dword, 792);
        int32_t  _sig_vfl    = GAME_OFFSET(sig_verify_float, 320);
        int32_t  _mat_calc   = GAME_OFFSET(matrix_calc_offset, 1224);
        uint32_t _magic_arr  = GAME_OFFSET(magic_array, 16384);
        int32_t  _arr_vfw    = GAME_OFFSET(sig_array_fwd, -16);
        int32_t  _arr_vbw    = GAME_OFFSET(sig_array_bwd, -8);
        int32_t  _arr_calc   = GAME_OFFSET(array_calc_offset, 56);
        int fail_cnt = 0;
        while (GlobalMemory::MatrixOffset == 0 || GlobalMemory::ArrayaddrOffset == 0) {
            for (long int i = 0; i < GlobalMemory::ModulePagesCount; i++) {
                // ★ 卡屏修复: 每扫描256页让出CPU, 防DrawThread被饿死
                if ((i & 0xFF) == 0) sched_yield();
                vm_readv(result.addr + (i * 4096), buff.data(), 0x1000);
                for (int ii = 0; ii < 512; ii += 1) {
                    unsigned long val = buff[ii];
                    long int CurrentAddr = result.addr + (i * 4096) + (ii * 8);

                    if (GlobalMemory::MatrixOffset == 0) {
                        uint32_t low  = (uint32_t)(val & 0xFFFFFFFF);
                        uint32_t high = (uint32_t)(val >> 32);
                        long int candidate = 0;
                        if (low == _magic_mat)       candidate = CurrentAddr;
                        else if (high == _magic_mat) candidate = CurrentAddr + 4;
                        if (candidate != 0) {
                            if (getDword(candidate + _sig_vdw) == _magic_dw &&
                                getFloat(candidate + _sig_vfl) == _magic_fl) {
                                GlobalMemory::MatrixOffset = (candidate - GlobalMemory::libbase) + _mat_calc;
                            }
                        }
                    }
                    if (val == _magic_arr) {
                        if (getFloat(CurrentAddr + _arr_vfw) == _magic_fl && getDword(CurrentAddr + _arr_vbw) == _magic_dw) {
                            GlobalMemory::ArrayaddrOffset = CurrentAddr - GlobalMemory::libbase + _arr_calc;
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

                uintptr_t coorBase = getPtr64(对象 + GAME_OFFSET(obj_coor_base, 0x28));
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
                bool isSender = (std::strstr(cls, "sender") != nullptr) || (std::strstr(cls, "dm65_scene_sender") != nullptr);
                if (!effective_disable_filter && !is_woodplane && !is_faction4 && !isSender) {
                    if (std::isnan(状态数值) || std::isinf(状态数值) || std::abs(状态数值 - std::round(状态数值)) > 0.0f) continue;
                    if (std::abs(状态数值) > 1000.0f || 状态数值 < 0.0f) continue;
                    if (状态数值 == 0.0f && 实体特征码 == 0) continue;
                }

                if (std::strstr(cls, "player") || std::strstr(cls, "boss") ||
                    状态数值 == 450.0f || std::strstr(cls, "scene") ||
                    std::strstr(cls, "sender") || std::strstr(cls, "prop") || std::strstr(cls, "mirror") || Debugging ||
                    is_woodplane ||
                    disable_skip_filter ||
                    MjSubsystem::IsMjSpecialClass(cls) ||
                    MjSubsystem::IsMjPropClass(cls) || std::strstr(cls, "monster")) {

                    int actionId = 0;
                    uintptr_t actionPtr = getPtr64(对象 + GAME_OFFSET(obj_action, 0x730));
                    if (actionPtr != 0) actionId = getDword(actionPtr + GAME_OFFSET(obj_action_id, 0x30));

                    DataStruct item{};
                    item.obj = 对象;
                    item.objcoor = coorBase;
                    item.action = actionId;
                    item.状态数值 = 状态数值;
                    item.实体特征码 = 实体特征码;
                    item.sub_type = ObjSubClass::Unknown;
                    item.prop_name[0] = '\0';
                    item.is_ghost = (实体特征码 != GAME_OFFSET(entity_feature, 0x1000000) || 状态数值 != GAME_OFFSET(entity_state, 450.0f));

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
                    } else if (std::strstr(cls, "chuanhuo")) {
                        item.阵营 = 5;
                        std::strcpy(item.str, "[厂长残火]");
                    } else if (std::strstr(cls, "boss") && !std::strstr(cls, "prop") && !std::strstr(cls, "mj_") && !std::strstr(cls, "rd") && !std::strstr(cls, "trap.gim")) {
                        std::strcpy(item.str, getboss(cls));
                        item.阵营 = 1;
                        item.sub_type = ObjSubClass::Boss;
                    } else if (std::strstr(cls, "h55_prop_tieqiao")) {
                        // 守墓人遁地形态: 类名含prop但应作为玩家处理, 始终可见(幽灵白名单)
                        std::strcpy(item.str, getplayer(cls));
                        item.阵营 = 2;
                        item.sub_type = ObjSubClass::Player;
                    } else if ((std::strstr(cls, "player") || std::strstr(cls, "npc_deluosi_dress_ghost")) &&
                               !std::strstr(cls, "prop") && !std::strstr(cls, "mj_") && !std::strstr(cls, "rd")) {
                        std::strcpy(item.str, getplayer(cls));
                        item.阵营 = 2;
                        item.sub_type = ObjSubClass::Player;
                    } else if (is_woodplane) {
                        item.阵营 = 3;
                        item.sub_type = ObjSubClass::Pallet;
                    } else if (std::strstr(cls, "sender") || std::strstr(cls, "dm65_scene_sender")) {
                        // 密码机: 独立外层分支，不依赖 scene 条件
                        item.阵营 = 3;
                        std::strcpy(item.str, getscene(cls));
                        item.sub_type = ObjSubClass::CipherMachine;
                    } else if (std::strstr(cls, "dm65_scene_prop_01") || std::strstr(cls, "christmasbox01") || std::strstr(cls, "halloweenbox01")) {
                        item.阵营 = 3;
                        std::strcpy(item.str, getscene(cls));
                        item.sub_type = ObjSubClass::Box;
                    } else if (std::strstr(cls, "scene") && !std::strstr(cls, "prop") && !std::strstr(cls, "rd") && !MjSubsystem::IsMjSpecialClass(cls) && !std::strstr(cls, "monster")) {
                        std::strcpy(item.str, getscene(cls));
                        item.阵营 = 3;
                        if (std::strstr(cls, "trap.gim")) item.sub_type = ObjSubClass::Trap;
                        else if (std::strstr(cls, "polun_jiazi.gim")) item.sub_type = ObjSubClass::Clip;
                        else if (std::strstr(cls, "h55_sleepingtown3_jpcat01low")) item.sub_type = ObjSubClass::Cat;
                        else if (std::strstr(cls, "h55_playground_lion")) item.sub_type = ObjSubClass::Lion;
                        else if (std::strstr(cls, "woodplane001") || std::strstr(cls, "woodplane01")) item.sub_type = ObjSubClass::Pallet;
                    } else if (std::strstr(cls, "dm65_scene_gallows") || std::strstr(cls, "dm65_scene_gallows_hx_low")) {
                        item.阵营 = 3;
                        item.sub_type = ObjSubClass::Chair;
                    } else if (std::strstr(cls, "dm65_scene_prop_76")) {
                        item.阵营 = 3;
                        item.sub_type = ObjSubClass::Cellar;
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
                            // 守墓人遁地始终可见(不受幽灵开关影响)
                            if (!inform_ghost && !std::strstr(item.str, "守墓")) continue;
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

            // ★ v2.39: 每600次迭代(~30秒)重新强制执行亲和性, 防止内核调度器重置
            static int data_affinity_rebind_counter = 0;
            if (++data_affinity_rebind_counter >= 600) {
                data_affinity_rebind_counter = 0;
                data_timer.BindCurrentThreadToCores(false, "DataThread");
            }

            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(40, 55);
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));
        }
    }
    CloseDebugLog();
}
