// draw_EntityDraw.cpp
// Part of the draw_Gui split (v2.47).
// This file contains entity/object drawing and main draw loop functions
// extracted from draw_Gui.cpp. All functions are declared in
// draw_Gui_internal.h and defined here.

#include "draw_Gui_internal.h"

void scan_mimic_roles() {
    if (pid <= 0) return;

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

bool should_filter_cached(std::string_view cn) noexcept {
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

bool IsFakeHunter_cached(std::string_view name) noexcept {
    std::string name_str(name);
    auto it = fakeHunterCache.find(name_str);
    if (it != fakeHunterCache.end()) return it->second;

    static const std::vector<std::string_view> fakes = {
            "mirror", "crow", "patroller", "peeper", "tentacle", "note", "robot", "phantom",
            "clone", "decoy", "trap", "pet", "em_crow", "em65", "butcher_em",
            "umbrella", "parasol", "buzz", "bird", "chuanhuo"};

    for (const auto &f : fakes) {
        if (name.find(f) != std::string_view::npos) {
            fakeHunterCache[name_str] = true;
            return true;
        }
    }
    fakeHunterCache[name_str] = false;
    return false;
}

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
                if (show_draw_sender && distance <= g_sender_dist) {
                    optimizedDrawText("[密码机]", 密码机色);
                    if (distance >= 0) {
                        char distText[32];
                        std::snprintf(distText, sizeof(distText), "%d m", distance);
                        optimizedDrawText(distText, 密码机色, ImGui::GetTextLineHeight());
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
    } else if ((item.阵营 == 4 || item.阵营 == 6) && show_draw_Prop) {
        const char* display_name = item.prop_name[0] != '\0' ? item.prop_name : item.类名;
        ImColor prop_color = 白色;
        bool should_draw = true;

        // ★ 阵营 == 6 (MJ/特殊道具): 需要逐类子开关检查
        //     阵营 == 4 (普通道具): 直接绘制, 不受 MjSubsystem 子开关影响
        if (item.阵营 == 6) {

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

        // ★ MJ 专属距离过滤 + 贴图渲染
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
        }  // if (item.阵营 == 6)

        // ★ 阵营 == 4: 普通道具 — 直接绘制, 无 MJ 子开关
        if (item.阵营 == 4) {
            int price = ExtractPrice(item.prop_name);
            if (price >= 0) {
                // 根据价值设置颜色 (简化版, 无 MJ 子开关)
                if (price < 1000)           prop_color = ImColor(100, 100, 100, 255);
                else if (price <= 2000)     prop_color = ImColor(180, 150, 120, 255);
                else if (price < 5000)      prop_color = ImColor(100, 180, 210, 255);
                else if (price < 10000)     prop_color = ImColor(255, 140, 0, 255);
                else if (price < 50000)     prop_color = ImColor(255, 0, 255, 255);
                else if (price < 100000)    prop_color = ImColor(255, 215, 0, 255);
                else if (price < 200000)    prop_color = ImColor(255, 255, 0, 255);
                else if (price < 400000)    prop_color = ImColor(0, 255, 0, 255);
                else if (price < 1000000)   prop_color = ImColor(0, 128, 255, 255);
                else {
                    float t = ImGui::GetTime();
                    float brightness = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t * 3.0f));
                    prop_color = ImColor((int)(255 * brightness), (int)(215 * brightness), 0, 255);
                }
            }
            optimizedDrawText(display_name, prop_color);
            if (distance >= 0) {
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

    // ★ 目的地选择按钮 + 导航路径渲染
    {
        float map_h_d = g_map_display_size;
        const auto& cfg_d = GetActiveMapConfig();
        float ww_d = cfg_d.maxX - cfg_d.minX, wh_d = cfg_d.maxY - cfg_d.minY;
        float mw_d = cfg_d.isVerticalMap ? (map_h_d * wh_d / ww_d) : (map_h_d * ww_d / wh_d);
        ImVec2 mp_d(g_map_pos_x, g_map_pos_y);
        ImVec2 me_d(mp_d.x + mw_d, mp_d.y + map_h_d);

        // ── 目的地/清除按钮（仅在导航启用时显示）──
        if (g_show_nav_line) {
            float btn_w = 72, btn_h = 30, gap = 32;
            ImVec2 b1(me_d.x - (btn_w*2 + gap) - 4, me_d.y + 4);
            ImVec2 b2(b1.x + btn_w + gap, b1.y);
            // 目的地按钮
            Draw->AddRectFilled(b1, ImVec2(b1.x+btn_w, b1.y+btn_h), IM_COL32(20, 80, 160, 210), 5.0f);
            Draw->AddRect(b1, ImVec2(b1.x+btn_w, b1.y+btn_h), IM_COL32(80, 160, 255, 220), 5.0f, 0, 1.5f);
            ImVec2 t1sz = ImGui::CalcTextSize("目的地");
            Draw->AddText(ImVec2(b1.x+(btn_w-t1sz.x)*0.5f, b1.y+(btn_h-t1sz.y)*0.5f), IM_COL32(255,255,255,255), "目的地");
            // 清除按钮
            Draw->AddRectFilled(b2, ImVec2(b2.x+btn_w, b2.y+btn_h), IM_COL32(100, 30, 30, 210), 5.0f);
            Draw->AddRect(b2, ImVec2(b2.x+btn_w, b2.y+btn_h), IM_COL32(220, 80, 80, 220), 5.0f, 0, 1.5f);
            ImVec2 t2sz = ImGui::CalcTextSize("清除");
            Draw->AddText(ImVec2(b2.x+(btn_w-t2sz.x)*0.5f, b2.y+(btn_h-t2sz.y)*0.5f), IM_COL32(255,255,255,255), "清除");
            // 点击检测
            ImVec2 ms = ImGui::GetMousePos();
            if (!g_dest_select_mode && !g_path_edit_mode) {
                // 目的地
                if (ms.x >= b1.x && ms.x <= b1.x+btn_w && ms.y >= b1.y && ms.y <= b1.y+btn_h &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    g_dest_select_mode = true;
                    g_draw_map_size_bak = g_map_display_size;
                    g_draw_map_posx_bak = g_map_pos_x; g_draw_map_posy_bak = g_map_pos_y;
                    g_map_display_size = std::min((float)displayInfo.height * 0.75f, 1600.0f);
                    g_map_pos_x = (displayInfo.width - g_map_display_size) * 0.5f;
                    g_map_pos_y = (displayInfo.height - g_map_display_size) * 0.5f;
                }
                // 清除
                if (ms.x >= b2.x && ms.x <= b2.x+btn_w && ms.y >= b2.y && ms.y <= b2.y+btn_h &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    g_dest_world_x = g_dest_world_y = g_dest_world_z = 0;
                    g_nav_render_path.clear();
                    g_show_nav_line = false;
                    AddNotification("目的地已清除", 1.5f, ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
                }
            }
        }

        // ── 目的地选择模式提示 ──
        if (g_dest_select_mode) {
            Draw->AddText(ImVec2(mp_d.x + 5, me_d.y + 5), IM_COL32(255, 255, 0, 255), "请点击地图选择目的地...");
            // 取消按钮
            ImVec2 cancelp(me_d.x - 45, me_d.y + 5);
            Draw->AddRectFilled(cancelp, ImVec2(cancelp.x + 40, cancelp.y + 22), IM_COL32(100, 30, 30, 200), 4.0f);
            Draw->AddText(ImVec2(cancelp.x + 6, cancelp.y + 3), IM_COL32(255, 255, 255, 255), "取消");
            ImVec2 ms2 = ImGui::GetMousePos();
            if (ms2.x >= cancelp.x && ms2.x <= cancelp.x + 40 && ms2.y >= cancelp.y && ms2.y <= cancelp.y + 22 &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                g_map_display_size = g_draw_map_size_bak;
                g_map_pos_x = g_draw_map_posx_bak; g_map_pos_y = g_draw_map_posy_bak;
                g_dest_select_mode = false;
            }

            // 点击地图选点
            ImVec2 mm = ImGui::GetMousePos();
            if (mm.x >= mp_d.x && mm.x <= me_d.x && mm.y >= mp_d.y && mm.y <= me_d.y &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                float u = (mm.x - mp_d.x) / mw_d;
                float v = (mm.y - mp_d.y) / map_h_d;
                const auto& act_cfg = GetActiveMapConfig();
                g_dest_world_x = CoordTransform::UVToX(u, act_cfg);
                g_dest_world_y = CoordTransform::UVToY(v, act_cfg);
                g_dest_world_z = Z.Z;
                // ★ 跨路径连通图导航
                g_nav_render_path.clear();
                Vector3A playerPos(Z.X, Z.Y, Z.Z);
                Vector3A destPos(g_dest_world_x, g_dest_world_y, g_dest_world_z);
                do {  // multi-path Dijkstra scope
                    struct RNode { int pi, ep; Vector3A p; }; // pi=-1玩家,-2目的地; ep=0起点,1终点
                    std::vector<RNode> rn;
                    rn.push_back({-1, 0, playerPos});
                    rn.push_back({-2, 0, destPos});
                    for (int pi = 0; pi < (int)g_saved_paths.size(); pi++) {
                        if (g_saved_paths[pi].size() < 2) continue;
                        rn.push_back({pi, 0, g_saved_paths[pi].front()});
                        rn.push_back({pi, 1, g_saved_paths[pi].back()});
                    }
                    int N = (int)rn.size();
                    if (N <= 2) { AddNotification("无可用路径", 1.5f, ImVec4(1,0.4f,0.2f,1)); break; }

                    // 构建邻接表
                    std::vector<std::vector<std::pair<int,float>>> adj(N);
                    auto addEdge = [&](int a, int b, float cost) { adj[a].push_back({b,cost}); adj[b].push_back({a,cost}); };

                    // 同路径内部边
                    for (int i = 2; i < N; i += 2) {
                        int j = i + 1; auto& p = g_saved_paths[rn[i].pi];
                        float len = 0;
                        for (size_t k = 1; k < p.size(); k++) {
                            float dx = p[k].X-p[k-1].X, dy = p[k].Y-p[k-1].Y;
                            len += sqrtf(dx*dx+dy*dy);
                        }
                        addEdge(i, j, len);
                    }

                    // 跨路径端点连通（阈值内）
                    for (int i = 2; i < N; i++) {
                        for (int j = i + 2; j < N; j++) {
                            if ((i ^ 1) == j) continue; // 同路径跳过
                            float dx = rn[i].p.X - rn[j].p.X, dy = rn[i].p.Y - rn[j].p.Y;
                            float d = sqrtf(dx*dx+dy*dy);
                            if (d < kPathSnapThreshold) addEdge(i, j, d);
                        }
                    }

                    // 玩家/目的地连接到附近端点
                    for (int src = 0; src < 2; src++) {
                        for (int j = 2; j < N; j++) {
                            float dx = rn[src].p.X - rn[j].p.X, dy = rn[src].p.Y - rn[j].p.Y;
                            float d = sqrtf(dx*dx+dy*dy);
                            if (d < kPathSnapThreshold) addEdge(src, j, d);
                        }
                    }

                    // Dijkstra
                    std::vector<float> md(N, 1e9f);
                    std::vector<int> prv(N, -1);
                    using P = std::pair<float,int>;
                    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
                    md[0] = 0; pq.push({0,0});
                    while (!pq.empty()) {
                        auto [du, u] = pq.top(); pq.pop();
                        if (du > md[u]) continue;
                        if (u == 1) break;
                        for (auto& [v, w] : adj[u]) {
                            float nd = du + w;
                            if (nd < md[v]) { md[v]=nd; prv[v]=u; pq.push({nd,v}); }
                        }
                    }

                    if (prv[1] < 0) { AddNotification("目的地不可达(请画更多路径)", 2.5f, ImVec4(1,0.4f,0.2f,1)); break; }

                    // 回溯节点序列
                    std::vector<int> seq;
                    for (int cur = 1; cur >= 0; cur = prv[cur]) seq.push_back(cur);
                    std::reverse(seq.begin(), seq.end());

                    // 展开为完整路径点
                    for (size_t si = 0; si < seq.size(); si++) {
                        int ni = seq[si];
                        if (ni <= 1) continue; // 跳过玩家/目的地节点
                        if (si + 1 < seq.size() && seq[si+1] == (ni ^ 1)) {
                            // 同路径内移动：起点→终点 或 终点→起点
                            auto& p = g_saved_paths[rn[ni].pi];
                            int a = (rn[ni].ep == 0) ? 0 : (int)p.size()-1;
                            int b = (rn[ni].ep == 0) ? (int)p.size()-1 : 0;
                            int step = (a <= b) ? 1 : -1;
                            for (int k = a; k != b + step; k += step)
                                g_nav_render_path.push_back(p[k]);
                            si++; // 跳过配对的终点节点
                        } else {
                            // 单端点/跨路径跳
                            g_nav_render_path.push_back(rn[ni].p);
                        }
                    }
                } while (0);
                if (g_nav_render_path.empty()) {
                    // 回退：单路径直连
                    int best_path = -1; size_t best_s=0, best_e=0; float best_d=1e9f;
                    for (size_t pi=0; pi<g_saved_paths.size(); pi++) {
                        auto& pth=g_saved_paths[pi]; if(pth.size()<2)continue;
                        size_t ps=0,pe=0; float pd=1e9f,ed=1e9f;
                        for(size_t k=0;k<pth.size();k++){
                            float d1=sqrtf((pth[k].X-playerPos.X)*(pth[k].X-playerPos.X)+(pth[k].Y-playerPos.Y)*(pth[k].Y-playerPos.Y));
                            float d2=sqrtf((pth[k].X-destPos.X)*(pth[k].X-destPos.X)+(pth[k].Y-destPos.Y)*(pth[k].Y-destPos.Y));
                            if(d1<pd){pd=d1;ps=k;} if(d2<ed){ed=d2;pe=k;}
                        }
                        if(pd+ed<best_d){best_d=pd+ed;best_path=(int)pi;best_s=ps;best_e=pe;}
                    }
                    if(best_path>=0){
                        auto& pth=g_saved_paths[best_path];
                        size_t a=std::min(best_s,best_e),b=std::max(best_s,best_e);
                        for(size_t k=a;k<=b;k++)g_nav_render_path.push_back(pth[k]);
                    }
                }
                g_show_nav_line = true;
                g_map_display_size = g_draw_map_size_bak;
                g_map_pos_x = g_draw_map_posx_bak; g_map_pos_y = g_draw_map_posy_bak;
                g_dest_select_mode = false;
                AddNotification("目的地已设置", 2.0f, ImVec4(0.3f,1.0f,0.3f,1.0f));
            }
        }

        // ── 导航路径2D渲染 + 目的地红点 ──
        if (g_show_nav_line && !g_nav_render_path.empty() && g_dest_world_x!=0) {
            const auto& act_cfg = GetActiveMapConfig();
            for (size_t k=1;k<g_nav_render_path.size();k++) {
                float u1=act_cfg.offsetU+g_nav_render_path[k-1].X*act_cfg.scaleX;
                float v1=act_cfg.offsetV+g_nav_render_path[k-1].Y*act_cfg.scaleY;
                float u2=act_cfg.offsetU+g_nav_render_path[k].X*act_cfg.scaleX;
                float v2=act_cfg.offsetV+g_nav_render_path[k].Y*act_cfg.scaleY;
                Draw->AddLine(ImVec2(mp_d.x+u1*mw_d,mp_d.y+v1*map_h_d),
                              ImVec2(mp_d.x+u2*mw_d,mp_d.y+v2*map_h_d),
                              IM_COL32(50,255,50,220),5.0f);
            }
            float du=act_cfg.offsetU+g_dest_world_x*act_cfg.scaleX;
            float dv=act_cfg.offsetV+g_dest_world_y*act_cfg.scaleY;
            ImVec2 dp(mp_d.x+du*mw_d,mp_d.y+dv*map_h_d);
            Draw->AddCircleFilled(dp,8.0f,IM_COL32(255,50,50,255));
            Draw->AddCircle(dp,12.0f,IM_COL32(255,255,255,180),0,2.0f);
        }
    }
}

void Draw_Main_Optimized(ImDrawList *Draw) {
    // ★ v3.1 安全加固: 验证失败/心跳降级 → 禁止绘制核心功能
    if (!SECURE_GUARD()) return;
    float secure_alpha = SECURE_ALPHA;
    uintptr_t Step1_Addr = GlobalMemory::libbase + GlobalMemory::MatrixOffset;
    uintptr_t Ptr1 = getPtr64(Step1_Addr);
    if (!Ptr1) return;
    uintptr_t Step2_Addr = Ptr1 + GAME_OFFSET(chain_step1_a58, 0xA58);
    uintptr_t Ptr2 = getPtr64(Step2_Addr);
    if (!Ptr2) return;
    GlobalMemory::Matrix = Ptr2 + GAME_OFFSET(chain_step2_2c0, 0x2C0);
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

    // 重置诊断计数
    g_debug_scanned_count = maxDrawCount;
    g_debug_player_count = 0;
    g_debug_boss_count = 0;
    g_debug_self_found = false;
    g_debug_last_cam_z = 0.0f;

    for (int i = 0; i < maxDrawCount; ++i) {
        const auto &item = current_data[i];
        if (item.阵营 != 1 && item.阵营 != 2) continue;
        // 诊断计数
        if (item.阵营 == 2) g_debug_player_count++;
        if (item.阵营 == 1) g_debug_boss_count++;
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
        // 诊断：记录最后计算的 cam_z
        g_debug_last_cam_z = cam_z;
        if (dist_from_center < min_center_dist && dist_from_center < 150.0f) {
            min_center_dist = dist_from_center;
            best_candidate_index = i;
        }
    }

    if (best_candidate_index != -1) {
        const auto& self_item = current_data[best_candidate_index];
        GlobalMemory::自身 = self_item.obj;
        Z = getObjectCoordinates(self_item.objcoor, false);
        found_self = true;
        g_selfAction.store(self_item.action);
        g_debug_self_found = true;
        snprintf(g_debug_self_cls, sizeof(g_debug_self_cls), "%s", self_item.类名);
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
                float yaw_cos = getFloat(item.objcoor + GAME_OFFSET(coord_yaw_cos, 0xB8));
                float yaw_sin = getFloat(item.objcoor + GAME_OFFSET(coord_yaw_sin, 0xC0));
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
        float now = ImGui::GetTime();
        float elapsed = now - g_last_touch_time;
        bool animating = (elapsed < 0.8f && g_last_touch_time > 0);

        // ★ DPI 缩放: 所有渲染尺寸按 g_ui_density 适配不同屏幕
        // ★ 配色: 暖金主题, 与整体 UI 协调
        const ImU32 gold_fill   = IM_COL32(242, 199, 56, 35);   // 暖金填充
        const ImU32 gold_ring   = IM_COL32(242, 199, 56, 110);  // 暖金圆环
        const ImU32 gold_center = IM_COL32(242, 199, 56, 210);  // 暖金实心
        const ImU32 gold_cross  = IM_COL32(242, 199, 56, 155);  // 十字线
        float dpi = g_ui_density;
        float base_r = 30.0f * dpi;
        Draw->AddCircleFilled(touch_center, base_r, gold_fill);
        Draw->AddCircle(touch_center, base_r, gold_ring, 32, 2.5f * dpi);

        // 触摸触发动画：扩散波纹
        if (animating) {
            float t = elapsed / 0.8f; // 0→1
            float wave_r = base_r + t * 35.0f * dpi;
            int wave_a = (int)(180 * (1.0f - t*t));
            Draw->AddCircle(touch_center, wave_r, IM_COL32(242, 199, 56, wave_a), 32, 3.0f * dpi);

            // 第二道波纹（延迟）
            float t2 = (elapsed - 0.1f) / 0.7f;
            if (t2 > 0 && t2 < 1.0f) {
                float wave_r2 = base_r + t2 * 25.0f * dpi;
                int wave_a2 = (int)(120 * (1.0f - t2*t2));
                Draw->AddCircle(touch_center, wave_r2, IM_COL32(242, 199, 56, wave_a2), 32, 2.0f * dpi);
            }
        }

        // 中心十字准星
        float cross_half = 18.0f * dpi;
        Draw->AddCircleFilled(touch_center, 6.0f * dpi, gold_center);
        Draw->AddLine(ImVec2(touch_center.x - cross_half, touch_center.y),
                      ImVec2(touch_center.x + cross_half, touch_center.y),
                      gold_cross, 2.0f * dpi);
        Draw->AddLine(ImVec2(touch_center.x, touch_center.y - cross_half),
                      ImVec2(touch_center.x, touch_center.y + cross_half),
                      gold_cross, 2.0f * dpi);
    }

    // ========== 摸金导航地图 ==========
    if (g_map_enabled) {
        FlushTextures();  // 上传后台线程解码完成的纹理
        // ★ 未识别时自动选择第一个地图
        if (g_current_map_index < 0 && !g_all_maps.empty() && !g_all_maps[0].empty()) {
            g_current_map_index = 0;
            g_current_floor_index = 0;
            LoadMapTexture(0, 0);
        }
        TryAutoDetectMap(current_data);
        // ★ 楼层自动检测（防抖：仅自动识别模式下启用，手动模式下不覆盖用户选择）
        if (g_current_map_index >= 0 && g_map_auto_detect) {
            static int g_floor_debounce = 0;
            static int g_floor_target = -1;
            int rawFloor = GetFloorFromPlayerZ(Z);
            if (rawFloor != g_current_floor_index) {
                if (g_floor_target != rawFloor) {
                    g_floor_target = rawFloor;
                    g_floor_debounce = 0;
                }
                g_floor_debounce++;
                if (g_floor_debounce >= 30) {
                    printf("[Floor] Z=%.1f 持续%d帧 → %d楼→%d楼\n",
                        Z.Z, g_floor_debounce, g_current_floor_index + 1, rawFloor + 1);
                    g_current_floor_index = rawFloor;
                    LoadMapTexture(g_current_map_index, rawFloor);
                    g_floor_target = -1;
                    g_floor_debounce = 0;
                }
            } else {
                g_floor_target = -1;
                g_floor_debounce = 0;
            }
        }
        Draw_MapOverlay(Draw, current_data);
    }

    // ========== 3D立体路径渲染 ==========
    if (g_show_3d_paths && !g_saved_paths.empty()) {
        const float fade_cm = g_3d_path_fade_dist * 100.0f;
        static float g_foot_anim_t = 0; g_foot_anim_t += ImGui::GetIO().DeltaTime * 3.0f;
        const float flow_speed = g_3d_flow_speed;

        const float sw = (float)displayInfo.width, sh = (float)displayInfo.height;
        const float safe_l = -sw * 0.5f, safe_r = sw * 1.5f, safe_t = -sh * 0.5f, safe_b = sh * 1.5f;
        
        const ImU32 default_colors[] = {
            IM_COL32(0, 255, 255, 255), IM_COL32(255, 100, 255, 255),
            IM_COL32(255, 255, 0, 255), IM_COL32(100, 255, 100, 255),
            IM_COL32(255, 150, 50, 255), IM_COL32(100, 150, 255, 255),
            IM_COL32(255, 255, 255, 255),
        };
        
        for (size_t pi = 0; pi < g_saved_paths.size(); pi++) {
            if (pi < g_path_visible.size() && !g_path_visible[pi]) continue;
            // ★ 目的地导航：3D只显示导航路径
            if (g_show_nav_line && !g_nav_render_path.empty() && g_dest_world_x != 0) continue;
            auto& path = g_saved_paths[pi];
            if (path.size() < 2) continue;
            
            ImU32 base;
            if (pi < g_path_colors.size() && g_path_colors[pi] != 0)
                base = g_path_colors[pi];
            else
                base = default_colors[pi % 7];
            
            if (g_3d_path_style == 0) {
                // === 线条模式 (带流光脉冲) ===
                for (size_t i = 1; i < path.size(); i++) {
                    Vector3A p1 = path[i-1]; p1.Z += g_3d_path_height;
                    Vector3A p2 = path[i];   p2.Z += g_3d_path_height;
                    float mx=(p1.X+p2.X)*0.5f,my=(p1.Y+p2.Y)*0.5f,mz=(p1.Z+p2.Z)*0.5f;
                    float dist=sqrtf((mx-Z.X)*(mx-Z.X)+(my-Z.Y)*(my-Z.Y)+(mz-Z.Z)*(mz-Z.Z));
                    if(dist>fade_cm*1.5f)continue;
                    float ft=std::clamp(dist/fade_cm,0.0f,1.0f);
                    int alpha=(int)(255.0f*(1.0f-ft*ft)); if(alpha<6)continue;
                    float s1x,s1y,s1w,s2x,s2y,s2w;
                    if(!optimizedWorldToScreen(p1,matrix,px,py,s1x,s1y,s1w)||!optimizedWorldToScreen(p2,matrix,px,py,s2x,s2y,s2w))continue;
                    if(s1x<safe_l||s1x>safe_r||s1y<safe_t||s1y>safe_b||s2x<safe_l||s2x>safe_r||s2y<safe_t||s2y>safe_b)continue;
                    Draw->AddLine(ImVec2(s1x,s1y),ImVec2(s2x,s2y),IM_COL32((base>>0)&0xFF,(base>>8)&0xFF,(base>>16)&0xFF,alpha),g_3d_line_width);
                    // 流光脉冲: 单向流动(不环绕)
                    float seg_dx=s2x-s1x,seg_dy=s2y-s1y,seg_len=sqrtf(seg_dx*seg_dx+seg_dy*seg_dy);
                    float pulse_len=std::min(seg_len*0.25f,100.0f);
                    float pulse_spacing=pulse_len+80.0f;
                    float phase=g_foot_anim_t*flow_speed;
                    int first=(int)ceilf(-phase/pulse_spacing);
                    for(int k=first;;k++){
                        float c=phase+(float)k*pulse_spacing;
                        if(c<-pulse_len)continue; if(c>seg_len+pulse_len)break;
                        float t1=std::clamp((c-pulse_len*0.5f)/seg_len,0.0f,1.0f);
                        float t2=std::clamp((c+pulse_len*0.5f)/seg_len,0.0f,1.0f);
                        if(t1>=t2)continue;
                        int ga=(int)(alpha*(1.0f-(float)std::max(0,k-first)*0.25f));if(ga<10)continue;
                        Draw->AddLine(ImVec2(s1x+seg_dx*t1,s1y+seg_dy*t1),ImVec2(s1x+seg_dx*t2,s1y+seg_dy*t2),IM_COL32((base>>0)&0xFF,(base>>8)&0xFF,(base>>16)&0xFF,ga),g_3d_line_width+4.0f);
                    }
                }
            } else if (g_3d_path_style == 1) {
                // === 圆点模式 ===
                const float sp=60.0f;
                for (size_t i=1;i<path.size();i++){
                    Vector3A p1=path[i-1];p1.Z+=g_3d_path_height; Vector3A p2=path[i];p2.Z+=g_3d_path_height;
                    float mx=(p1.X+p2.X)*0.5f,my=(p1.Y+p2.Y)*0.5f,mz=(p1.Z+p2.Z)*0.5f;
                    float dist=sqrtf((mx-Z.X)*(mx-Z.X)+(my-Z.Y)*(my-Z.Y)+(mz-Z.Z)*(mz-Z.Z));
                    if(dist>fade_cm*1.5f)continue;
                    float ft=std::clamp(dist/fade_cm,0.0f,1.0f);
                    int ba=(int)(255.0f*(1.0f-ft*ft));if(ba<8)continue;
                    float s1x,s1y,s1w,s2x,s2y,s2w;
                    if(!optimizedWorldToScreen(p1,matrix,px,py,s1x,s1y,s1w)||!optimizedWorldToScreen(p2,matrix,px,py,s2x,s2y,s2w))continue;
                    if(s1x<safe_l||s1x>safe_r||s1y<safe_t||s1y>safe_b||s2x<safe_l||s2x>safe_r||s2y<safe_t||s2y>safe_b)continue;
                    float seg_sx=s2x-s1x,seg_sy=s2y-s1y,seg_len=sqrtf(seg_sx*seg_sx+seg_sy*seg_sy);
                    if(seg_len<sp*0.5f)continue;
                    float phase=g_foot_anim_t*flow_speed;
                    int first=(int)ceilf(-phase/sp);
                    for(int j=first;;j++){
                        float pos=phase+(float)j*sp;
                        if(pos<0)continue;if(pos>seg_len)break;
                        float t=pos/seg_len;
                        ImVec2 c(s1x+seg_sx*t,s1y+seg_sy*t);
                        int da=ba;if(pos>seg_len*0.7f)da=(int)(ba*1.3f);
                        float outer_r=g_3d_dot_radius,inner_r=outer_r*0.47f;
                        Draw->AddCircle(c,outer_r,IM_COL32((base>>0)&0xFF,(base>>8)&0xFF,(base>>16)&0xFF,da/4),0,outer_r*0.2f);
                        Draw->AddCircleFilled(c,inner_r,IM_COL32((base>>0)&0xFF,(base>>8)&0xFF,(base>>16)&0xFF,da));
                    }
                }
            } else {
                // === 箭头模式 ===
                const float asp=80.0f;
                for (size_t i=1;i<path.size();i++){
                    Vector3A p1=path[i-1];p1.Z+=g_3d_path_height; Vector3A p2=path[i];p2.Z+=g_3d_path_height;
                    float mx=(p1.X+p2.X)*0.5f,my=(p1.Y+p2.Y)*0.5f,mz=(p1.Z+p2.Z)*0.5f;
                    float dist=sqrtf((mx-Z.X)*(mx-Z.X)+(my-Z.Y)*(my-Z.Y)+(mz-Z.Z)*(mz-Z.Z));
                    if(dist>fade_cm*1.5f)continue;
                    float ft=std::clamp(dist/fade_cm,0.0f,1.0f);
                    int ba=(int)(255.0f*(1.0f-ft*ft));if(ba<8)continue;
                    float s1x,s1y,s1w,s2x,s2y,s2w;
                    if(!optimizedWorldToScreen(p1,matrix,px,py,s1x,s1y,s1w)||!optimizedWorldToScreen(p2,matrix,px,py,s2x,s2y,s2w))continue;
                    if(s1x<safe_l||s1x>safe_r||s1y<safe_t||s1y>safe_b||s2x<safe_l||s2x>safe_r||s2y<safe_t||s2y>safe_b)continue;
                    float seg_sx=s2x-s1x,seg_sy=s2y-s1y,seg_len=sqrtf(seg_sx*seg_sx+seg_sy*seg_sy);
                    if(seg_len<asp*0.5f)continue;
                    float nx=seg_sx/seg_len,ny=seg_sy/seg_len,pxp=-ny,pyp=nx;
                    float phase=g_foot_anim_t*flow_speed;
                    int first=(int)ceilf(-phase/asp);
                    for(int j=first;;j++){
                        float pos=phase+(float)j*asp;
                        if(pos<0)continue;if(pos>seg_len)break;
                        float t=pos/seg_len;
                        float ax=s1x+seg_sx*t,ay=s1y+seg_sy*t;
                        int da=ba;if(pos>seg_len*0.7f)da=(int)(ba*1.3f);
                        float as=g_3d_dot_radius;
                        ImVec2 tip(ax+nx*as,ay+ny*as);
                        ImVec2 lw(ax-nx*as*0.5f+pxp*as*0.55f,ay-ny*as*0.5f+pyp*as*0.55f);
                        ImVec2 rw(ax-nx*as*0.5f-pxp*as*0.55f,ay-ny*as*0.5f-pyp*as*0.55f);
                        Draw->AddTriangleFilled(tip,lw,rw,IM_COL32((base>>0)&0xFF,(base>>8)&0xFF,(base>>16)&0xFF,da));
                    }
                }
            }
        }
    }

    // ========== 导航路径3D渲染 (玩家→目的地) ==========
    if (g_show_3d_paths && g_show_nav_line && !g_nav_render_path.empty() && g_dest_world_x != 0) {
        const float fcn = g_3d_path_fade_dist * 100.0f;
        const float swn = (float)displayInfo.width, shn = (float)displayInfo.height;
        ImU32 nc = IM_COL32(50, 255, 50, 255);
        for (size_t i = 1; i < g_nav_render_path.size(); i++) {
            Vector3A p1 = g_nav_render_path[i-1]; p1.Z += g_3d_path_height;
            Vector3A p2 = g_nav_render_path[i];   p2.Z += g_3d_path_height;
            float mx=(p1.X+p2.X)*0.5f,my=(p1.Y+p2.Y)*0.5f,mz=(p1.Z+p2.Z)*0.5f;
            float dist=sqrtf((mx-Z.X)*(mx-Z.X)+(my-Z.Y)*(my-Z.Y)+(mz-Z.Z)*(mz-Z.Z));
            if(dist>fcn*1.5f)continue;
            float ft=std::clamp(dist/fcn,0.0f,1.0f);
            int alpha=(int)(255.0f*(1.0f-ft*ft)); if(alpha<6)continue;
            float s1x,s1y,s1w,s2x,s2y,s2w;
            if(!optimizedWorldToScreen(p1,matrix,px,py,s1x,s1y,s1w)||!optimizedWorldToScreen(p2,matrix,px,py,s2x,s2y,s2w))continue;
            if(s1x<-swn*0.5f||s1x>swn*1.5f||s1y<-shn*0.5f||s1y>shn*1.5f||s2x<-swn*0.5f||s2x>swn*1.5f||s2y<-shn*0.5f||s2y>shn*1.5f)continue;
            Draw->AddLine(ImVec2(s1x,s1y),ImVec2(s2x,s2y), IM_COL32((nc>>0)&0xFF,(nc>>8)&0xFF,(nc>>16)&0xFF,alpha), g_3d_line_width+4.0f);
        }
    }

    // ========== 地图识别状态 & 新地图提示 ==========
    if (g_show_map_status) {
        ImVec2 st_pos(20, displayInfo.height * 0.12f);
        ImU32 st_col = IM_COL32(255, 255, 255, 220);
        const char* phase_str = "?";
        ImU32 phase_col = IM_COL32(180, 180, 180, 220);
        switch (g_detect_phase) {
            case MapDetectPhase::LOCKED: phase_str = "LOCKED"; phase_col = IM_COL32(100, 255, 100, 220); break;
            case MapDetectPhase::SWITCH_DETECTED: phase_str = "SWITCH"; phase_col = IM_COL32(255, 200, 50, 220); break;
            case MapDetectPhase::IDENTIFYING: phase_str = "SCANNING"; phase_col = IM_COL32(100, 200, 255, 220); break;
            case MapDetectPhase::LOW_CONFIDENCE: phase_str = "LOW_CONF"; phase_col = IM_COL32(255, 100, 50, 220); break;
        }
        char status_line[256];
        if (g_current_map_index >= 0 && g_current_map_index < (int)g_all_maps.size() && !g_all_maps[g_current_map_index].empty()) {
            const char* map_name = g_all_maps[g_current_map_index][0].name;
            if (!map_name) map_name = "?";
            snprintf(status_line, sizeof(status_line), "[%s] %s - %s fp=%d floor=%d Z=%.0f",
                phase_str, map_name,
                g_detect_status_text, g_detect_best_fp_id, g_current_floor_index, Z.Z);
        } else {
            snprintf(status_line, sizeof(status_line), "[%s] - %s fp=%d",
                phase_str, g_detect_status_text, g_detect_best_fp_id);
        }
        ImVec2 sz = ImGui::CalcTextSize(status_line);
        // Dark background for readability
        Draw->AddRectFilled(ImVec2(st_pos.x - 4, st_pos.y - 4),
            ImVec2(st_pos.x + sz.x + 8, st_pos.y + sz.y + 8),
            IM_COL32(0, 0, 0, 160), 6.0f);
        Draw->AddText(st_pos, phase_col, status_line);
    }

    // LOW_CONFIDENCE → 新地图提示（连续3秒低分）
    {
        static int g_low_conf_popup_counter = 0;
        if (g_detect_phase == MapDetectPhase::LOW_CONFIDENCE && !g_new_map_prompt_shown) {
            g_low_conf_popup_counter++;
            if (g_low_conf_popup_counter > 120) { // ~2秒
                g_new_map_prompt_shown = true;

                float alpha = 1.0f;
                const char* line1 = "未识别到已知地图！";
                const char* line2 = "请切换到「地图管理」→「设为新地图」";
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
                if (g_font_ui && g_font_ui->IsLoaded()) {
                ImGui::PushFont(g_font_ui);
                ImVec2 title_size = ImGui::CalcTextSize(line1);
                ImVec2 text1_pos(box_min.x + (box_width - title_size.x) * 0.5f, box_min.y + 14.0f);
                Draw->AddText(g_font_ui, ImGui::GetFontSize() * 1.1f, text1_pos, accent_color, line1);
                ImVec2 text2_pos(box_min.x + (box_width - line2_size.x) * 0.5f, text1_pos.y + title_size.y + 10.0f);
                Draw->AddText(g_font_ui, ImGui::GetFontSize() * 0.9f, text2_pos, ImColor(230, 232, 240, (int)(255 * alpha)), line2);
                ImGui::PopFont();
            }
            }
        } else {
            g_low_conf_popup_counter = 0;
            if (g_detect_phase == MapDetectPhase::LOCKED) {
                g_new_map_prompt_shown = false;
            }
        }
    }

    // ========== SoHook 骨骼/进度叠加绘制 ==========
    {
        int kernel_player_count = 0;
        for (int i = 0; i < maxDrawCount; ++i) {
            if (current_data[i].阵营 == 1 || current_data[i].阵营 == 2) kernel_player_count++;
        }
        SoHook::DrawOverlay(Draw, matrix, px, py, kernel_player_count, Z.X, Z.Z, Z.Y, 距离比例);
    }

    // ========== ★ 盖板触发弹窗（居中置顶，3秒后淡出消失） ==========
    {
        float elapsed = ImGui::GetTime() - g_wood_popup_time;
        if (elapsed < 3.0f && g_wood_popup_time > 0) {
            float alpha = 1.0f;
            if (elapsed > 2.0f) alpha = (3.0f - elapsed) / 1.0f;  // 最后1秒渐隐

            const char* msg = "自动盖板已触发";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            float padX = 24.0f * g_ui_density, padY = 12.0f * g_ui_density;
            float bw = ts.x + padX * 2.0f;
            float bh = ts.y + padY * 2.0f;
            ImVec2 bmin((displayInfo.width - bw) * 0.5f, displayInfo.height * 0.08f);
            ImVec2 bmax(bmin.x + bw, bmin.y + bh);
            float r = bh * 0.5f;  // 圆角

            // 弹窗背景 + 描边
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            fg->AddRectFilled(bmin, bmax, IM_COL32(255, 240, 210, (int)(230 * alpha)), r);
            fg->AddRect(bmin, bmax, IM_COL32(220, 170, 80, (int)(200 * alpha)), r, 0, 2.5f);
            // 文字
            fg->AddText(ImVec2(bmin.x + padX, bmin.y + padY),
                        IM_COL32(100, 60, 20, (int)(255 * alpha)), msg);
        }
    }

    // ========== 通知中心（暖金 Toast 风格） ==========
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

            // ★ 暖金宣纸配色（提高不透明度以便看清）
            ImU32 accent = IM_COL32((int)(notif.color.x*255), (int)(notif.color.y*255), (int)(notif.color.z*255), (int)(255 * alpha));
            ImU32 bg = IM_COL32(255, 245, 225, (int)(240 * alpha));       // 宣纸暖白 (提高alpha)
            ImU32 border = IM_COL32(220, 175, 100, (int)(200 * alpha));  // 暖金边框 (更显眼)
            ImU32 textCol = IM_COL32(80, 45, 15, (int)(255 * alpha));    // 深棕文字
            ImU32 shadow = IM_COL32(160, 130, 90, (int)(100 * alpha));   // 暖调阴影 (加深)

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
