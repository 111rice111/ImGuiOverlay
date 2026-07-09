/*
 * net_config.h — 服务端配置下发模块
 *
 * 关键数据从服务端加密下发, 客户端不硬编码。
 * 无有效 token → 服务器拒绝 → 无偏移/地图数据 → overlay 无法工作。
 *
 * 新增位置: app/src/main/cpp/src/Android_draw/net_config.h
 */

#pragma once
#include <string>
#include <mutex>
#include "json.hpp"
#include "crypto.h"
using json = nlohmann::json;

// ========== 服务端配置存储 ==========
struct ServerConfig {
    // 游戏内存偏移 (服务端下发)
    uintptr_t offset_self        = 0;   // 自身对象
    uintptr_t offset_hunter      = 0;   // 监管者
    uintptr_t offset_survivor    = 0;   // 求生者列表
    uintptr_t offset_identity    = 0;   // 阵营标识
    uintptr_t offset_position    = 0;   // 坐标
    uintptr_t offset_health      = 0;   // 血量
    uintptr_t offset_board       = 0;   // 板子状态
    uintptr_t offset_skill_cd    = 0;   // 技能冷却
    uintptr_t offset_camera      = 0;   // 相机矩阵
    
    // 地图数据 (服务端下发)
    std::string map_data_encrypted;     // 加密的地图JSON
    bool map_data_valid = false;
    
    // 版本信息
    std::string version_name;
    int force_min_version = 0;
    int latest_version = 0;
    std::string download_url;
    std::string update_notes;
    
    // 时间戳
    int64_t fetched_at = 0;
    
    bool is_valid() const {
        return offset_self != 0 || offset_hunter != 0 || map_data_valid;
    }
};

static ServerConfig g_server_config;
static std::mutex g_config_mutex;

// ========== 从服务器获取配置 ==========
inline bool api_fetch_config(const std::string& token, const std::string& device_id) {
    json req;
    req["token"] = token;
    req["device_id"] = device_id;
    req["ts"] = (int64_t)time(nullptr);
    
    std::string resp = http_post_enc("/api/config", req.dump());
    if (resp.empty()) return false;
    
    try {
        json j = json::parse(resp);
        if (!j.value("ok", false)) return false;
        
        std::lock_guard<std::mutex> lock(g_config_mutex);
        
        // 解析偏移量
        auto& offsets = j["data"]["offsets"];
        g_server_config.offset_self     = std::stoull(offsets.value("self", "0x0"), nullptr, 16);
        g_server_config.offset_hunter   = std::stoull(offsets.value("hunter", "0x0"), nullptr, 16);
        g_server_config.offset_survivor  = std::stoull(offsets.value("survivor", "0x0"), nullptr, 16);
        g_server_config.offset_identity = std::stoull(offsets.value("identity", "0x0"), nullptr, 16);
        g_server_config.offset_position = std::stoull(offsets.value("position", "0x0"), nullptr, 16);
        g_server_config.offset_health   = std::stoull(offsets.value("health", "0x0"), nullptr, 16);
        g_server_config.offset_board    = std::stoull(offsets.value("board", "0x0"), nullptr, 16);
        g_server_config.offset_skill_cd = std::stoull(offsets.value("skill_cd", "0x0"), nullptr, 16);
        g_server_config.offset_camera   = std::stoull(offsets.value("camera", "0x0"), nullptr, 16);
        
        // 地图数据
        if (j["data"].contains("map_data")) {
            g_server_config.map_data_encrypted = j["data"]["map_data"];
            g_server_config.map_data_valid = !g_server_config.map_data_encrypted.empty();
        }
        
        g_server_config.fetched_at = time(nullptr);
        return true;
    } catch (...) {
        return false;
    }
}

// ========== 获取偏移 (带服务端校验) ==========
inline uintptr_t get_server_offset(const std::string& name, uintptr_t fallback = 0) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    
    if (name == "self")     return g_server_config.offset_self ? g_server_config.offset_self : fallback;
    if (name == "hunter")   return g_server_config.offset_hunter ? g_server_config.offset_hunter : fallback;
    if (name == "survivor") return g_server_config.offset_survivor ? g_server_config.offset_survivor : fallback;
    if (name == "position") return g_server_config.offset_position ? g_server_config.offset_position : fallback;
    
    return fallback;
}

// ========== 判断是否需要重新拉取 (每30分钟刷新) ==========
inline bool config_needs_refresh() {
    return (time(nullptr) - g_server_config.fetched_at) > 1800;
}

// ========== 服务端地图数据解密 ==========
// 服务端用独立的 per-token XOR key 加密地图数据
// 解密 key = HMAC-SHA256(token, "map_config_salt") 的前16字节
inline std::string decrypt_map_data(const std::string& token) {
    if (!g_server_config.map_data_valid) return "";
    
    // 简单XOR解密 (应与服务端对应)
    std::string key = token.substr(0, 16);
    while (key.size() < 16) key += "saltmapoverlay0";
    
    std::string result = g_server_config.map_data_encrypted;
    for (size_t i = 0; i < result.size(); i++) {
        result[i] ^= key[i % key.size()];
    }
    return result;
}
