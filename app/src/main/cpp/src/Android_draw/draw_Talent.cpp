// ============================================================
// draw_Talent.cpp — 天赋查看器（draw_Gui 拆分 v2.47）
// 本文件由 draw_Gui.cpp 拆分而来，包含：
//   - parse_pickle_talents
//   - find_snapshot_file
//   - parse_talent_json
//   - show_talent_viewer
// 全局变量、struct 定义与 const map 定义
// （SURVIVOR_TALENT_MAP / BUTCHER_TALENT_MAP / SKILL_MAP）
// 已移至 draw_Gui_internal.h 头文件中声明。
// ============================================================

#include "draw_Gui_internal.h"

bool parse_pickle_talents(TalentState& state, const std::string& pickle_path) {
    state.players.clear();
    std::ifstream f(pickle_path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) { state.status = "无法打开pickle文件"; return false; }
    size_t sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    f.read((char*)buf.data(), sz);
    f.close();

    // Pickle VM state
    std::vector<json> stack;
    std::vector<int> marks;       // stack indices of MARK positions
    std::map<int, json> memo;     // BINPUT / BINGET memo
    size_t pc = 0;
    auto read_u8  = [&]() -> uint8_t  { return pc < sz ? buf[pc++] : 0; };
    auto read_u32 = [&]() -> uint32_t { uint32_t v = 0; for(int i=0;i<4&&pc<sz;i++) v |= ((uint32_t)buf[pc++]) << (i*8); return v; };
    auto read_u16 = [&]() -> uint16_t { uint16_t v = 0; for(int i=0;i<2&&pc<sz;i++) v |= ((uint16_t)buf[pc++]) << (i*8); return v; };
    auto push = [&](json v) { stack.push_back(v); };
    auto pop  = [&]() { auto v = stack.back(); stack.pop_back(); return v; };

    // Skip PROTO header
    if (sz >= 2 && buf[0] == 0x80) pc = 2;

    static const std::unordered_set<int> big_talents = {8, 16, 24, 32};

    try {
        while (pc < sz) {
            uint8_t op = read_u8();
            switch (op) {
                case '.':  // STOP
                    goto done;
                case '(':  // MARK
                    marks.push_back((int)stack.size());
                    break;
                case '}':  // EMPTY_DICT
                    push(json::object());
                    break;
                case ']':  // EMPTY_LIST
                    push(json::array());
                    break;
                case ')':  // EMPTY_TUPLE
                    push(json::array());
                    break;
                case 'N':  // NONE
                    push(json());
                    break;
                case 'K':  // BININT1
                    push((int)read_u8());
                    break;
                case 'M': { // BININT2
                    uint16_t v = read_u16();
                    push((int)v);
                    break;
                }
                case 'J': { // BININT (4 bytes)
                    uint32_t uv = read_u32();
                    push((int)(int32_t)uv);
                    break;
                }
                case 'X': { // BINUNICODE
                    uint32_t len = read_u32();
                    std::string s((char*)&buf[pc], len);
                    pc += len;
                    push(s);
                    break;
                }
                case 0x8c: { // SHORT_BINUNICODE
                    uint8_t len = read_u8();
                    std::string s((char*)&buf[pc], len);
                    pc += len;
                    push(s);
                    break;
                }
                case 'q': { // BINPUT
                    int idx = (int)read_u8();
                    memo[idx] = stack.back();
                    break;
                }
                case 'r': { // LONG_BINPUT
                    uint32_t idx = read_u32();
                    memo[(int)idx] = stack.back();
                    break;
                }
                case 'h': { // BINGET
                    int idx = (int)read_u8();
                    if (memo.count(idx)) push(memo[idx]);
                    break;
                }
                case 'j': { // LONG_BINGET
                    uint32_t idx = read_u32();
                    if (memo.count((int)idx)) push(memo[(int)idx]);
                    break;
                }
                case 'u': { // SETITEMS — pop mark, build dict from key-value pairs
                    if (marks.empty()) break;
                    int start = marks.back(); marks.pop_back();
                    json d = stack[start-1]; // the EMPTY_DICT (or result of REDUCE) is below the mark
                    // key-value pairs are in [start, stack.size())
                    for (size_t i = start; i + 1 < stack.size(); i += 2) {
                        std::string key = stack[i].is_string() ? stack[i].get<std::string>() : "";
                        d[key] = stack[i+1];
                    }
                    stack.erase(stack.begin() + start - 1, stack.end());
                    push(d);
                    break;
                }
                case 'e': { // APPENDS — pop mark, append items to list
                    if (marks.empty()) break;
                    int start = marks.back(); marks.pop_back();
                    json a = stack[start-1];
                    for (size_t i = start; i < stack.size(); i++)
                        a.push_back(stack[i]);
                    stack.erase(stack.begin() + start - 1, stack.end());
                    push(a);
                    break;
                }
                case 't': { // TUPLE — pop mark, build tuple (as array)
                    if (marks.empty()) break;
                    int start = marks.back(); marks.pop_back();
                    json t = json::array();
                    for (size_t i = start; i < stack.size(); i++)
                        t.push_back(stack[i]);
                    stack.erase(stack.begin() + start, stack.end());
                    push(t);
                    break;
                }
                case 0x85: case 0x86: case 0x87: { // TUPLE1/2/3
                    int n = (op == 0x85) ? 1 : (op == 0x86) ? 2 : 3;
                    json t = json::array();
                    for (int i = 0; i < n; i++) t.push_back(pop());
                    std::reverse(t.begin(), t.end());
                    push(t);
                    break;
                }
                case 0x88: push(true); break; // NEWTRUE
                case 0x89: push(false); break; // NEWFALSE
                case 's': { // SETITEM — pop key, pop value, set in top-of-stack dict
                    auto val = pop();
                    auto key = pop();
                    if (!stack.empty() && stack.back().is_object() && key.is_string())
                        stack.back()[key.get<std::string>()] = val;
                    break;
                }
                case 'a': { // APPEND — pop item, append to top-of-stack list
                    auto item = pop();
                    if (!stack.empty() && stack.back().is_array())
                        stack.back().push_back(item);
                    break;
                }
                case 0x81: { // NEWOBJ — like REDUCE for new-style classes
                    auto args = pop();  // constructor args
                    auto cls = pop();   // class name
                    if (cls.is_string()) {
                        std::string fq = cls.get<std::string>();
                        if (fq.find("bson") != std::string::npos || fq.find("objectid") != std::string::npos) {
                            if (args.is_array() && args.size() >= 1 && args[0].is_object())
                                push(args[0]);
                            else
                                push(json::object());
                        } else {
                            push(json::object());
                        }
                    } else {
                        push(json::object());
                    }
                    break;
                }
                case 'b': { // BUILD — apply state dict to object on stack (BSON: merge)
                    auto state_obj = pop();
                    if (!stack.empty() && stack.back().is_object() && state_obj.is_object()) {
                        for (auto& [k, v] : state_obj.items())
                            stack.back()[k] = v;
                    }
                    break;
                }
                case 'G': { // BINFLOAT (8 bytes)
                    double d;
                    memcpy(&d, &buf[pc], 8); pc += 8;
                    push(d);
                    break;
                }
                case 'c': { // GLOBAL — read module.name
                    std::string mod, name;
                    while (pc < sz && buf[pc] != '\n') mod += (char)buf[pc++];
                    pc++; // skip \n
                    while (pc < sz && buf[pc] != '\n') name += (char)buf[pc++];
                    pc++;
                    // If it's a bson class, mark it for REDUCE handling
                    push(mod + "." + name);
                    break;
                }
                case 'R': { // REDUCE — call the callable
                    auto callable = pop();  // "module.name"
                    auto args = pop();      // tuple of args
                    // If it's a bson object, reconstruct as dict
                    if (callable.is_string()) {
                        std::string fq = callable.get<std::string>();
                        if (fq.find("bson") != std::string::npos || fq.find("objectid") != std::string::npos) {
                            // BSON objects — treat as dict or leave as-is
                            if (args.is_array() && args.size() >= 1 && args[0].is_object())
                                push(args[0]);
                            else
                                push(json::object());
                        } else {
                            push(json::object());
                        }
                    } else {
                        push(json::object());
                    }
                    break;
                }
                case 'F': { // FLOAT — string float
                    std::string fs;
                    while (pc < sz && buf[pc] != '\n') fs += (char)buf[pc++];
                    pc++;
                    push(std::stod(fs));
                    break;
                }
                case 'l': { // LONG (long int as string)
                    std::string ls;
                    while (pc < sz && buf[pc] != '\n') ls += (char)buf[pc++];
                    pc++;
                    if (ls.size() >= 2 && ls[0] == 'L') ls = ls.substr(1);
                    push(std::stoll(ls));
                    break;
                }
                default:
                    // Unknown opcode, try to continue or break
                    break;
            }
        }
    done:;
        // The final result is the top of the stack
        if (stack.empty()) { state.status = "Pickle解析失败(空)"; return false; }
        json root = stack.back();
        if (!root.contains("data") || !root["data"].is_array() || root["data"].empty()) {
            state.status = "Pickle格式错误"; return false;
        }
        const auto& entities = root["data"][0]["snap_shot"]["event_data"]["entities"];
        if (!entities.is_object()) { state.status = "无entities"; return false; }

        for (auto& [uid, obj] : entities.items()) {
            if (!obj.contains("unit_type") || !obj.contains("player_name")) continue;
            int utype = obj["unit_type"].is_number() ? obj["unit_type"].get<int>() : 0;
            if (utype != 1 && utype != 2) continue;
            PlayerInfo info;
            info.unit_type = utype;
            info.name = obj["player_name"].is_string() ? obj["player_name"].get<std::string>() : "?";
            info.camp = (utype == 1) ? "监管者" : "求生者";
            const auto* talent_map = (utype == 1) ? &BUTCHER_TALENT_MAP : &SURVIVOR_TALENT_MAP;
            bool detailed = g_show_detailed;
            if (obj.contains("genius_id_lvs") && obj["genius_id_lvs"].is_array()) {
                for (auto& pair : obj["genius_id_lvs"]) {
                    if (pair.is_array() && pair.size() >= 1) {
                        int tid = pair[0].is_number() ? pair[0].get<int>() : 0;
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
                        int skill_id = sid.get<int>();
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
        return true;
    } catch (std::exception& e) {
        state.status = std::string("解析异常: ") + e.what();
        return false;
    } catch (...) {
        state.status = "解析异常(未知)";
        return false;
    }
}

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

void show_talent_viewer() {
    static TalentState state;
    static bool need_refresh = true;

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
            goto render_ui;
        }
        // ★ 方案3: 通过 su cp 绕过 Android 13+ 存储限制, pickle 存到 /data/local/bin/
        {
            std::string netease_root = "/storage/emulated/0/Android/data/" + game_package + "/files/netease/";
            auto newest = find_snapshot_file(netease_root);
            if (!newest.empty()) {
                // ★ C++ 直接解析 pickle — 零依赖, 不需要 Python/Termux
                parse_pickle_talents(state, newest.string());
            } else {
                state.status = "未找到天赋数据文件";
                state.players.clear();
            }
        }
    }  // closes if(need_refresh || ...)

render_ui:
    ImGui::SetNextWindowBgAlpha(0.45f);
    // ★ 方案6: 窗口锚定右上角, 避开异形屏
    {
        float margin = 16.0f * g_ui_density;
        ImGui::SetNextWindowPos(ImVec2(displayInfo.width - margin, margin),
                                 ImGuiCond_Once, ImVec2(1.0f, 0.0f));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 24.0f * g_ui_density);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * g_ui_density);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * g_ui_density, 14 * g_ui_density));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, g_theme.bg_dark);
    ImGui::PushStyleColor(ImGuiCol_Border, g_theme.border_strong);
    ImGui::PushStyleColor(ImGuiCol_Button, g_theme.primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_theme.primary_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_theme.primary_active);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, g_theme.check_mark);
    // ★ 方案5: 字体跟随 DPI
    ImGui::SetWindowFontScale(g_ui_density);
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
        // ★ 方案4: 小屏(≤1080)用2列, 大屏用3列
        int cols = (displayInfo.width <= 1080) ? 2 : 3;
        float colA_w = (cols==3) ? 70.0f * g_ui_density : 60.0f * g_ui_density;
        if (ImGui::BeginTable("天赋表", cols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("阵营", ImGuiTableColumnFlags_WidthFixed, colA_w);
            ImGui::TableSetupColumn("玩家", ImGuiTableColumnFlags_WidthStretch);
            if (cols >= 3) ImGui::TableSetupColumn("携带天赋", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& p : state.players) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(p.unit_type == 1 ? g_theme.danger : g_theme.success, "%s", p.camp.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", p.name.c_str());
                if (cols >= 3) ImGui::TableSetColumnIndex(2);
                else ImGui::TableNextColumn();
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
