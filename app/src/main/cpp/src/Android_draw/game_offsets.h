/*
 * game_offsets.h — 游戏内存偏移集中存储 (服务端下发)
 *
 * 所有游戏相关的硬编码值都从这里读取, 不再在 draw_Gui.cpp 中硬编码。
 * 启动时从 /api/config 获取，无有效 token → 偏移全部为0 → signature扫描失败。
 *
 * 新增位置: app/src/main/cpp/src/Android_draw/game_offsets.h
 */

#pragma once
#include <string>
#include <atomic>
#include <cstdint>
#include "json.hpp"
#include "crypto.h"
#include "net_client.h"  // http_post_enc, g_license, g_device_id
using json = nlohmann::json;

// ========== 游戏偏移集中存储 ==========
struct GameOffsets {
    // --- 签名扫描魔术数字 (没有这些就无法找到矩阵/数组) ---
    uint32_t magic_matrix = 0;       // 矩阵签名: 原值442745336
    uint32_t magic_array = 0;        // 数组签名: 原值16384
    uint32_t magic_dword_check = 0;  // DWORD验证: 原值257
    float    magic_float_check = 0;  // FLOAT验证: 原值1.0
    
    // --- 指针链偏移 ---
    int32_t  chain_step1_a58 = 0;    // 矩阵链 Step1→Ptr2: 原值0xA58
    int32_t  chain_step2_2c0 = 0;   // 矩阵链 Ptr2→Matrix: 原值0x2C0
    int32_t  chain_array_start = 0;  // 数组起始偏移: 原值0
    int32_t  chain_array_end = 0;    // 数组结束偏移: 原值8
    
    // --- 坐标偏移 ---
    int32_t  coord_x = 0;            // 原值0xA0
    int32_t  coord_y = 0;            // 原值0xA8
    int32_t  coord_z = 0;            // 原值0xA4
    int32_t  coord_yaw_cos = 0;      // 原值0xB8 (镜子/木板朝向)
    int32_t  coord_yaw_sin = 0;      // 原值0xC0
    
    // --- 对象结构偏移 ---
    int32_t  obj_coor_base = 0;      // 对象→坐标基址: 原值0x28
    int32_t  obj_classname = 0;      // 对象→类名字符串: 原值0xF8
    int32_t  obj_action = 0;         // 对象→action表: 原值0x730
    int32_t  obj_action_id = 0;      // action表→ID: 原值0x30
    int32_t  obj_cachename = 0;      // 对象→缓存名: 原值0x78
    
    // --- 实体验证值 ---
    uint32_t entity_feature = 0;     // 有效实体特征码: 原值0x1000000
    float    entity_state = 0;       // 有效状态值: 原值450.0
    
    // --- 矩阵/数组计算偏移 ---
    int32_t  matrix_calc_offset = 0; // MatrixOffset 计算增量: 原值1224
    int32_t  array_calc_offset = 0;  // ArrayaddrOffset 计算增量: 原值56
    
    // --- 签名扫描额外验证偏移 ---
    int32_t  sig_verify_dword = 0;   // candidate+792处DWORD: 原值792
    int32_t  sig_verify_float = 0;   // candidate+320处FLOAT: 原值320
    int32_t  sig_array_fwd = 0;      // 数组签前16字节FLOAT: 原值-16
    int32_t  sig_array_bwd = 0;      // 数组签前8字节DWORD: 原值-8
    
    // --- 对象迭代初始值 ---
    uint32_t init_value1 = 0;        // read_thread value1: 原值970061201
    uint32_t init_value3 = 0;        // read_thread value3: 原值257
    
    // --- 扫描参数 ---
    int32_t  scan_step = 0;          // 页扫描步长: 原值4096
    int32_t  scan_bit32 = 0;         // 32位扫描偏移: 原值4
    
    // 标记是否已从服务器加载
    bool loaded = false;
};

static GameOffsets g_game_offsets;

// ========== 辅助: 兼容 JSON 值为字符串或数字 ==========
static std::string _json_as_string(const json& j, const char* key, const char* def = "0") {
    if (!j.contains(key)) return def;
    auto& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string((int64_t)v);
    if (v.is_number_float()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", (double)v);
        return buf;
    }
    return def;
}

// ========== 从服务器拉取游戏偏移 ==========
inline bool api_fetch_game_offsets() {
#ifdef AUTH_SERVER
    if (!g_license.verified || g_license.token.empty()) return false;
    
    json req;
    req["token"] = g_license.token;
    req["device_id"] = g_device_id;
    req["ts"] = (int64_t)time(nullptr);
    
    std::string resp = http_post_enc("/api/config", req.dump());
    if (resp.empty()) return false;
    
    try {
        json j = json::parse(resp);
        if (!j.value("ok", false)) return false;
        
        auto& g = j["data"]["game_offsets"];
        
        // 签名扫描魔术数字
        g_game_offsets.magic_matrix      = (uint32_t)std::stoull(_json_as_string(g, "magic_matrix"), nullptr, 0);
        g_game_offsets.magic_array       = (uint32_t)std::stoull(_json_as_string(g, "magic_array"), nullptr, 0);
        g_game_offsets.magic_dword_check = (uint32_t)std::stoull(_json_as_string(g, "magic_dword_check"), nullptr, 0);
        g_game_offsets.magic_float_check = std::stof(_json_as_string(g, "magic_float_check", "0"));
        
        // 指针链
        g_game_offsets.chain_step1_a58   = (int32_t)std::stoll(_json_as_string(g, "chain_step1_a58"), nullptr, 0);
        g_game_offsets.chain_step2_2c0  = (int32_t)std::stoll(_json_as_string(g, "chain_step2_2c0"), nullptr, 0);
        
        // 坐标偏移
        g_game_offsets.coord_x          = (int32_t)std::stoll(_json_as_string(g, "coord_x"), nullptr, 0);
        g_game_offsets.coord_y          = (int32_t)std::stoll(_json_as_string(g, "coord_y"), nullptr, 0);
        g_game_offsets.coord_z          = (int32_t)std::stoll(_json_as_string(g, "coord_z"), nullptr, 0);
        g_game_offsets.coord_yaw_cos    = (int32_t)std::stoll(_json_as_string(g, "coord_yaw_cos"), nullptr, 0);
        g_game_offsets.coord_yaw_sin    = (int32_t)std::stoll(_json_as_string(g, "coord_yaw_sin"), nullptr, 0);
        
        // 对象结构
        g_game_offsets.obj_coor_base    = (int32_t)std::stoll(_json_as_string(g, "obj_coor_base"), nullptr, 0);
        g_game_offsets.obj_classname    = (int32_t)std::stoll(_json_as_string(g, "obj_classname"), nullptr, 0);
        g_game_offsets.obj_action       = (int32_t)std::stoll(_json_as_string(g, "obj_action"), nullptr, 0);
        g_game_offsets.obj_action_id    = (int32_t)std::stoll(_json_as_string(g, "obj_action_id"), nullptr, 0);
        g_game_offsets.obj_cachename    = (int32_t)std::stoll(_json_as_string(g, "obj_cachename"), nullptr, 0);
        
        // 实体验证
        g_game_offsets.entity_feature   = (uint32_t)std::stoull(_json_as_string(g, "entity_feature"), nullptr, 0);
        g_game_offsets.entity_state     = std::stof(_json_as_string(g, "entity_state", "0"));
        
        // 计算偏移
        g_game_offsets.matrix_calc_offset = (int32_t)std::stoll(_json_as_string(g, "matrix_calc_offset"), nullptr, 0);
        g_game_offsets.array_calc_offset  = (int32_t)std::stoll(_json_as_string(g, "array_calc_offset"), nullptr, 0);
        
        // 扫描验证
        g_game_offsets.sig_verify_dword = (int32_t)std::stoll(_json_as_string(g, "sig_verify_dword"), nullptr, 0);
        g_game_offsets.sig_verify_float = (int32_t)std::stoll(_json_as_string(g, "sig_verify_float"), nullptr, 0);
        g_game_offsets.sig_array_fwd    = (int32_t)std::stoll(_json_as_string(g, "sig_array_fwd"), nullptr, 0);
        g_game_offsets.sig_array_bwd    = (int32_t)std::stoll(_json_as_string(g, "sig_array_bwd"), nullptr, 0);
        
        // 初始值
        g_game_offsets.init_value1      = (uint32_t)std::stoull(_json_as_string(g, "init_value1"), nullptr, 0);
        g_game_offsets.init_value3      = (uint32_t)std::stoull(_json_as_string(g, "init_value3"), nullptr, 0);
        
        // 扫描参数
        g_game_offsets.scan_step        = (int32_t)std::stoll(_json_as_string(g, "scan_step"), nullptr, 0);
        g_game_offsets.scan_bit32       = (int32_t)std::stoll(_json_as_string(g, "scan_bit32"), nullptr, 0);
        
        g_game_offsets.loaded = true;
        static bool g_offsets_printed = false;
        if (!g_offsets_printed) {
            printf("\033[32m[+] 数据已加载\033[0m\n");
            fflush(stdout);
            g_offsets_printed = true;
        }
        return true;
    } catch (...) {
        return false;
    }
#else
    return false;
#endif
}

// ========== 便捷宏: 从服务端偏移读取，回退硬编码 ==========
// 带服务端偏移和本地回退的读取
#define GAME_OFFSET(field, fallback)  (g_game_offsets.loaded ? g_game_offsets.field : (fallback))
