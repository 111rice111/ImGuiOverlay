/*
 * net_client.h — 联网控制 / 卡密验证 / 热更新 客户端
 * 
 * 用原生 socket 实现最小 HTTP 客户端，无外部依赖。
 * 服务端 API:
 *   POST /api/verify  {key, device_id, hwid} → {ok, expire, msg}
 *   POST /api/command {device_id, token}      → {cmd, params}
 *   GET  /api/version                         → {version, url, md5}
 *   GET  /api/download                        → 二进制数据
 */
#include <string>
#include <cstring>

namespace _nc {
template<size_t N>
struct XS { char d[N]; constexpr XS(const char(&s)[N]){ for(size_t i=0;i<N;i++)d[i]=s[i]^0x5A; } };
template<size_t N> inline std::string D(const XS<N>& xs){ char b[N]; for(size_t i=0;i<N;i++)b[i]=xs.d[i]^0x5A; return std::string(b,N-1); }
}
#define _S(s) _nc::D(_nc::XS<sizeof(s)>(s))

#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sstream>
#include "json.hpp"
#include "crypto.h"
using json = nlohmann::json;

// ========== 配置 (运行时解密/配置文件) ==========
#define NET_SERVER_PORT_DEFAULT 8080
#define NET_TIMEOUT_SEC 10

#include <fstream>
#include <cstdlib>
#include <cstring>
#include <map>

// ========== v3.0 服务器配置读取 ==========
// 从 /data/local/bin/overlay_config.txt 读取服务器配置
// 格式: key=value (每行一个)
//   server_host=your-server.com
//   server_port=8080
inline std::map<std::string, std::string> load_server_config() {
    std::map<std::string, std::string> cfg;
    // v3.x 加固: 默认 IP 通过 _S() 编译时异或混淆
    cfg["server_host"] = _S("43.136.183.71");
    cfg["server_port"] = "80";                  // 宝塔默认端口
    
    std::ifstream f("/data/local/bin/overlay_config.txt");
    if (!f.is_open()) return cfg;
    
    std::string line;
    while (std::getline(f, line)) {
        // 去首尾空白
        size_t s = line.find_first_not_of(" \t\r\n");
        if (s == std::string::npos || line[s] == '#') continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        line = line.substr(s, e - s + 1);
        
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            cfg[k] = v;
        }
    }
    return cfg;
}

inline std::string get_server_host() {
    static std::string cached;
    static bool loaded = false;
    if (!loaded) {
        auto cfg = load_server_config();
        cached = cfg["server_host"];
        loaded = true;
    }
    return cached;
}

inline int get_server_port() {
    static int cached = -1;
    static bool loaded = false;
    if (!loaded) {
        // 1. 先读本地配置文件
        auto cfg = load_server_config();
        int p = std::atoi(cfg["server_port"].c_str());
        if (p > 0 && p < 65536) { cached = p; loaded = true; return p; }
        
        // 2. 回退: 尝试从 GitHub 获取 bore 端口 (v2兼容)
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0) {
            struct sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(443);
            struct hostent* he = gethostbyname(_S("raw.githubusercontent.com").c_str());
            if (he) {
                memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
                struct timeval tv = {3, 0};
                setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                    std::string req = 
                        std::string("GET ") + _S("/111rice111/ImGuiOverlay/main/port.txt") + " HTTP/1.1\r\n"
                        "Host: " + _S("raw.githubusercontent.com") + "\r\n"
                        "Connection: close\r\n\r\n";
                    send(sock, req.c_str(), req.size(), 0);
                    char buf[1024] = {};
                    int n = recv(sock, buf, sizeof(buf)-1, 0);
                    if (n > 0) {
                        buf[n] = 0;
                        char* body = strstr(buf, "\r\n\r\n");
                        if (body) {
                            p = atoi(body + 4);
                            if (p > 0 && p < 65536) { close(sock); cached = p; loaded = true; return p; }
                        }
                    }
                }
            }
            close(sock);
        }
        
        // 3. 最后回退
        cached = NET_SERVER_PORT_DEFAULT;
        loaded = true;
    }
    return cached;
}

inline const char* NET_HOST() { static std::string h = get_server_host(); return h.c_str(); }
inline const char* API_VERIFY() { static std::string h = _S("/api/verify"); return h.c_str(); }
inline const char* API_CHECK()  { static std::string h = _S("/api/check"); return h.c_str(); }
inline const char* API_HEART()  { static std::string h = _S("/api/heartbeat"); return h.c_str(); }
inline const char* API_VER()    { static std::string h = _S("/api/version"); return h.c_str(); }
inline const char* API_CMD()    { static std::string h = _S("/api/command"); return h.c_str(); }
inline const char* API_DL()     { static std::string h = _S("/api/download"); return h.c_str(); }
inline const char* API_ANN_DISMISS() { static std::string h = _S("/api/ann/dismiss"); return h.c_str(); }

// ========== 卡密验证状态 (v3.0 增强) ==========
struct LicenseInfo {
    bool verified = false;
    std::string token;
    std::string card_key;      // 卡密密钥 (v3.1 新增)
    std::string expire;        // 到期时间
    std::string device_id;     // 设备标识
    std::string card_type;     // "device" 或 "public"
    std::string duration;      // 时长标签, 如 "30天" "永久"
};

// ========== 公告信息 (v3.0 新增) ==========
struct Announcement {
    int id = 0;
    std::string title;
    std::string content;
    int priority = 0;          // 0=普通 1=重要 2=紧急
    std::string mode;          // "banner" 或 "popup"
    bool dismissable = true;
    bool dismissed = false;    // 是否已关闭
    std::string start_time;
    std::string end_time;
};

// ========== 远程指令 ==========
struct RemoteCommand {
    bool valid = false;
    std::string cmd;        // "toggle_map","set_param","restart","exec"
    json params;
};

// ========== 更新信息 (v3.0 增强) ==========
struct UpdateInfo {
    bool available = false;
    bool force_update = false;         // 是否强制更新 (不可跳过)
    int new_version = 0;
    int force_min_version = 0;         // 强制最低版本
    std::string version_name;          // 显示版本名, 如 "v2.40"
    std::string url;
    std::string md5;
    std::string changelog;             // 更新说明
    std::vector<Announcement> announcements;  // 服务器公告
};

// ========== 工具函数 ==========

// 获取设备唯一 ID (Android ID + 硬件序列号组合)
inline std::string get_device_id() {
    std::string id;
    // Android ID
    FILE* f = popen("settings get secure android_id 2>/dev/null", "r");
    if (f) { char buf[128]={}; fgets(buf, sizeof(buf), f); pclose(f); id += buf; }
    // 硬件序列号
    f = popen("getprop ro.serialno 2>/dev/null", "r");
    if (f) { char buf[128]={}; fgets(buf, sizeof(buf), f); pclose(f); id += buf; }
    // Build 指纹
    f = popen("getprop ro.build.fingerprint 2>/dev/null", "r");
    if (f) { char buf[256]={}; fgets(buf, sizeof(buf), f); pclose(f); id += buf; }
    // 取前 32 字符的 hash
    unsigned long h = 5381;
    for (char c : id) h = ((h << 5) + h) + c;
    char result[32];
    snprintf(result, sizeof(result), "DEV-%08lX", h);
    return result;
}

// 底层 HTTP 请求 (原始)
inline std::string http_request(const std::string& host, int port,
                                 const std::string& path,
                                 const std::string& body,
                                 bool is_post = true) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";

    struct timeval tv = {NET_TIMEOUT_SEC, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct hostent* he = gethostbyname(host.c_str());
    if (!he) { close(sock); return ""; }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock); return "";
    }

    std::ostringstream req;
    req << (is_post ? "POST " : "GET ") << path << " HTTP/1.0\r\n";
    req << "Host: " << host << "\r\n";
    req << "Content-Type: application/json\r\n";
    req << "User-Agent: OverlayClient/2.0\r\n";
    if (!body.empty()) {
        req << "Content-Length: " << body.size() << "\r\n";
    }
    req << "Connection: close\r\n\r\n";
    if (!body.empty()) req << body;

    std::string r = req.str();
    send(sock, r.c_str(), r.size(), 0);

    std::string resp;
    char buf[4096];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = 0;
        resp += buf;
    }
    close(sock);

    // 分离 body
    size_t pos = resp.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return resp.substr(pos + 4);
}

// 加密 HTTP POST (AES + hex 编码)
inline std::string http_post_enc(const std::string& path, const std::string& plain_body) {
    std::string enc = xor_encrypt(plain_body);
    std::string resp = http_request(NET_HOST(), get_server_port(), path, enc, true);
    if (resp.empty()) return "";
    return xor_decrypt(resp);
}

// ========== 核心 API ==========
#define CURRENT_VERSION 251  // v2.51 客户端版本号 (用于强制更新检查) ← 发版时必须同步git tag
static LicenseInfo g_license;
static std::string g_device_id;

// 1. 卡密验证 (v3.0 增强 — 支持公用卡)
inline bool api_verify_key(const std::string& key) {
    if (g_device_id.empty()) g_device_id = get_device_id();
    json req;
    req["key"] = key;
    req["device_id"] = g_device_id;
    req["hwid"] = g_device_id;
    req["version"] = CURRENT_VERSION;
    req["ts"] = (int64_t)time(nullptr);

    std::string resp = http_post_enc(API_VERIFY(), req.dump());
    if (resp.empty()) {
        printf("[License] 服务器无响应\n");
        return false;
    }

    try {
        json j = json::parse(resp);
        if (j.value("ok", false)) {
            g_license.verified = true;
            g_license.token = j.value("token", "");
            g_license.card_key = key;  // 保存卡密用于心跳
            g_license.expire = j.value("expire", "");
            g_license.device_id = g_device_id;
            g_license.card_type = j.value("card_type", "device");
            g_license.duration = j.value("duration", "");
            printf("[License] 验证通过 类型:%s 时长:%s 到期:%s\n",
                   g_license.card_type.c_str(), g_license.duration.c_str(), g_license.expire.c_str());
            return true;
        } else {
            printf("[License] 验证失败: %s\n", j.value("msg", "未知").c_str());
            return false;
        }
    } catch (...) {
        printf("[License] 响应解析失败\n");
        return false;
    }
}

// 2. 综合检查 (v3.0 — 版本+公告一次性获取)
inline UpdateInfo api_check_v2() {
    UpdateInfo info;
    json req;
    req["device_id"] = g_device_id;
    req["ts"] = (int64_t)time(nullptr);

    std::string resp = http_post_enc(API_CHECK(), req.dump());
    if (resp.empty()) return info;

    try {
        json j = json::parse(resp);
        if (j.value("ok", false)) {
            // 版本信息（兼容 int/string 两种类型）
            auto force_min_val = j["force_min_version"];
            auto latest_val    = j["latest_version"];
            if (force_min_val.is_string()) info.force_min_version = std::stoi(force_min_val.get<std::string>());
            else info.force_min_version = force_min_val.get<int>();
            if (latest_val.is_string()) info.new_version = std::stoi(latest_val.get<std::string>());
            else info.new_version = latest_val.get<int>();
            info.version_name      = j.value("version_name", "");
            info.url               = j.value("download_url", "");
            info.changelog        = j.value("update_notes", "");
            info.available        = (info.new_version > 0);
            info.force_update     = (info.force_min_version > 0 && CURRENT_VERSION < info.force_min_version);

            // 公告
            if (j.contains("announcements") && j["announcements"].is_array()) {
                for (auto& a : j["announcements"]) {
                    Announcement ann;
                    // 兼容 int/string 两种类型 (PDO fetchAll 默认返回字符串)
                    auto aid = a["id"];
                    ann.id = aid.is_string() ? std::stoi(aid.get<std::string>()) : aid.get<int>();
                    ann.title      = a.value("title", "");
                    ann.content    = a.value("content", "");
                    auto apri = a["priority"];
                    ann.priority   = apri.is_string() ? std::stoi(apri.get<std::string>()) : apri.get<int>();
                    ann.mode       = a.value("mode", "banner");
                    ann.dismissable = a.value("dismissable", true);
                    ann.dismissed  = a.value("dismissed", false);
                    ann.start_time = a.value("start_time", "");
                    ann.end_time   = a.value("end_time", "");
                    info.announcements.push_back(ann);
                }
            }

            printf("[Check] 服务器版本: %s (强制最低: %d, 当前: %d)\n",
                   info.version_name.c_str(), info.force_min_version, CURRENT_VERSION);
            printf("[Check] 公告: %zu 条\n", info.announcements.size());
        } else {
            printf("[Check] 服务器返回 ok=false: %s\n", j.value("msg", "").c_str());
        }
    } catch (std::exception& e) {
        printf("[Check] 响应解析失败: %s\n", e.what());
        printf("[Check] 原始响应(前200字): %s\n", resp.substr(0,200).c_str());
    } catch (...) {
        printf("[Check] 响应解析失败(未知异常)\n");
        printf("[Check] 原始响应(前200字): %s\n", resp.substr(0,200).c_str());
    }

    return info;
}

// 3. 关闭公告 (上报已读)
inline bool api_dismiss_announcement(int ann_id) {
    if (g_device_id.empty()) g_device_id = get_device_id();
    json req;
    req["ann_id"] = ann_id;
    req["device_id"] = g_device_id;

    std::string resp = http_post_enc(API_ANN_DISMISS(), req.dump());
    if (resp.empty()) return false;
    try {
        json j = json::parse(resp);
        return j.value("ok", false);
    } catch (...) { return false; }
}

// 4. 心跳包 (v3.1 — 降级系统: 失败不直接退出, 逐步降级功能)
inline bool api_heartbeat() {
    if (!g_license.verified) return false;
    static int fail_count = 0;
    json req;
    req["device_id"] = g_device_id;
    req["token"] = g_license.token;
    req["card_type"] = g_license.card_type;
    req["card_key"] = g_license.card_key;
    req["ts"] = (int64_t)time(nullptr);
    std::string resp = http_post_enc(API_HEART(), req.dump());
    if (resp.empty()) {
        fail_count++;
        hb_on_fail();  // 调用降级系统
        if (fail_count >= 3) {
            printf("\033[33m[Heart] 网络异常, 进入降级模式 (连续%d次失败)\033[0m\n", fail_count);
        }
        return false;
    }
    fail_count = 0;
    hb_on_ok();  // 恢复正常
    try {
        json j = json::parse(resp);
        if (j.value("banned", false)) {
            g_license.verified = false;
            printf("\033[31m[Heart] 已被封禁，程序退出\033[0m\n");
            exit(1);
        }
        if (!j.value("ok", true)) {
            g_license.verified = false;
            printf("\033[31m[Heart] 授权已失效 (%s)，程序退出\033[0m\n", 
                   std::string(j.value("msg", "未知原因")).c_str());
            exit(1);
        }
    } catch(...) {
        fail_count++;
        hb_on_fail();
        if (fail_count >= 3) {
            return false;
        }
        return false;
    }
    return true;
}

// 5. 获取更新信息 (v2 兼容 — GET /api/version)
inline UpdateInfo api_check_update() {
    UpdateInfo info;
    std::string resp = http_request(NET_HOST(), get_server_port(),
                                     API_VER(), "", false);
    if (resp.empty()) return info;

    try {
        json j = json::parse(resp);
        info.available = j.value("available", false);
        info.new_version = j.value("version", 0);
        info.url = j.value("url", "");
        info.md5 = j.value("md5", "");
        info.changelog = j.value("changelog", "");
    } catch (...) {}

    return info;
}

// 6. 下载更新
inline bool api_download_update(const std::string& url, const std::string& out_path) {
    std::string host = NET_HOST();
    int port = get_server_port();
    std::string path = API_DL();

    std::string resp = http_request(host, port, path, "", false);
    if (resp.empty()) return false;

    FILE* f = fopen(out_path.c_str(), "wb");
    if (!f) return false;
    fwrite(resp.data(), 1, resp.size(), f);
    fclose(f);
    chmod(out_path.c_str(), 0777);
    printf("[Update] 下载完成 %s (%zu bytes)\n", out_path.c_str(), resp.size());
    return true;
}

// 7. 轮询远程指令
inline RemoteCommand api_poll_command() {
    RemoteCommand cmd;
    if (!g_license.verified) return cmd;

    json req;
    req["device_id"] = g_device_id;
    req["token"] = g_license.token;

    std::string resp = http_request(NET_HOST(), get_server_port(),
                                     API_CMD(), req.dump());
    if (resp.empty()) return cmd;

    try {
        json j = json::parse(resp);
        if (j.contains("cmd")) {
            cmd.valid = true;
            cmd.cmd = j["cmd"].get<std::string>();
            if (j.contains("params")) cmd.params = j["params"];
        }
    } catch (...) {}

    return cmd;
}
