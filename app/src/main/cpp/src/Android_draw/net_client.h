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

// ========== 配置 ==========
#define NET_SERVER_HOST "api.example.com"
#define NET_SERVER_PORT 8080
#define NET_TIMEOUT_SEC 10

// ========== 卡密验证状态 ==========
struct LicenseInfo {
    bool verified = false;
    std::string token;
    std::string expire;     // 到期时间
    std::string device_id;  // 设备标识
};

// ========== 远程指令 ==========
struct RemoteCommand {
    bool valid = false;
    std::string cmd;        // "toggle_map","set_param","restart","exec"
    json params;
};

// ========== 更新信息 ==========
struct UpdateInfo {
    bool available = false;
    int new_version = 0;
    std::string url;
    std::string md5;
    std::string changelog;
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
    std::string enc = aes_encrypt(plain_body);
    std::string resp = http_request(NET_SERVER_HOST, NET_SERVER_PORT, path, enc, true);
    if (resp.empty()) return "";
    return aes_decrypt(resp);
}

// ========== 核心 API ==========
static LicenseInfo g_license;
static std::string g_device_id;

// 1. 卡密验证
inline bool api_verify_key(const std::string& key) {
    if (g_device_id.empty()) g_device_id = get_device_id();
    json req;
    req["key"] = key;
    req["device_id"] = g_device_id;
    req["hwid"] = g_device_id;
    req["version"] = 213;
    req["ts"] = (int64_t)time(nullptr);

    std::string resp = http_post_enc("/api/verify", req.dump());
    if (resp.empty()) {
        printf("[License] 服务器无响应\n");
        return false;
    }

    try {
        json j = json::parse(resp);
        if (j.value("ok", false)) {
            g_license.verified = true;
            g_license.token = j.value("token", "");
            g_license.expire = j.value("expire", "");
            g_license.device_id = g_device_id;
            printf("[License] 验证通过 到期:%s\n", g_license.expire.c_str());
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

// 2. 检查更新
inline UpdateInfo api_check_update() {
    UpdateInfo info;
    std::string resp = http_request(NET_SERVER_HOST, NET_SERVER_PORT,
                                     "/api/version", "", false);
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

// 3. 下载更新
inline bool api_download_update(const std::string& url, const std::string& out_path) {
    // 解析 URL
    std::string host = NET_SERVER_HOST;
    int port = NET_SERVER_PORT;
    std::string path = "/api/download";

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

// 4. 心跳包 (服务端可随时下发禁止指令)
inline bool api_heartbeat() {
    if (!g_license.verified) return false;
    json req;
    req["device_id"] = g_device_id;
    req["token"] = g_license.token;
    req["ts"] = (int64_t)time(nullptr);
    std::string resp = http_post_enc("/api/heartbeat", req.dump());
    if (resp.empty()) return false;
    try {
        json j = json::parse(resp);
        if (j.value("banned", false)) { printf("[Heart] 已封禁!\n"); g_license.verified = false; return false; }
    } catch(...) {}
    return true;
}

// 5. 轮询远程指令
inline RemoteCommand api_poll_command() {
    RemoteCommand cmd;
    if (!g_license.verified) return cmd;

    json req;
    req["device_id"] = g_device_id;
    req["token"] = g_license.token;

    std::string resp = http_request(NET_SERVER_HOST, NET_SERVER_PORT,
                                     "/api/command", req.dump());
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
