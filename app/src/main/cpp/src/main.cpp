#include "AndroidImgui.h"
#include "Android_draw/ThreadAffinity.h"
#include "Android_draw/driver.h"
#include "Android_draw/stealth.h"
#include "GraphicsManager.h"
#include "draw.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>
#include <sys/prctl.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <algorithm>

extern void 音量();

char extractedString[64]{};

// ============================================================
// 卡密授权系统 — 客户端集成
// ============================================================
static const char* AUTH_SERVER = "https://lazy-bulgur-unwell.ngrok-free.dev";
static const char* TOKEN_FILE = "/data/local/bin/auth_token.dat";

// 简单的 JSON 值提取（不用引入 json 库）
static std::string jsonGet(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;
    if (pos >= json.size()) return "";
    if (json[pos - 1] == '"') {
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }
    if (json.substr(pos, 4) == "true") return "true";
    if (json.substr(pos, 5) == "false") return "false";
    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n') end++;
    return json.substr(pos, end - pos);
}

// 生成设备标识
static std::string getDeviceId() {
    char buf[256] = {0};
    FILE* f = popen("getprop ro.product.model 2>/dev/null", "r");
    if (f) { fgets(buf, sizeof(buf), f); pclose(f); }
    std::string model(buf);
    model.erase(std::remove(model.begin(), model.end(), '\n'), model.end());
    if (model.empty()) model = "android-unknown";
    return model;
}

// 发送 HTTP POST 请求（用 curl）
static std::string httpPost(const char* path, const std::string& jsonBody) {
    std::string cmd = "curl -s --connect-timeout 5 -H 'ngrok-skip-browser-warning: true' -X POST '";
    cmd += AUTH_SERVER;
    cmd += path;
    cmd += "' -H 'Content-Type: application/json' -d '";
    // 转义 JSON 中的单引号
    std::string escaped = jsonBody;
    size_t p = 0;
    while ((p = escaped.find('\'', p)) != std::string::npos) {
        escaped.replace(p, 1, "'\\''");
        p += 4;
    }
    cmd += escaped;
    cmd += "' 2>/dev/null";

    char result[4096] = {0};
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return "";
    std::string out;
    while (fgets(result, sizeof(result), f)) out += result;
    pclose(f);
    return out;
}

// 保存 token 到文件
static void saveToken(const std::string& token) {
    FILE* f = fopen(TOKEN_FILE, "w");
    if (f) { fwrite(token.c_str(), 1, token.size(), f); fclose(f); }
}

// 读取已保存的 token
static std::string loadToken() {
    FILE* f = fopen(TOKEN_FILE, "r");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string s(sz, '\0');
    fread(&s[0], 1, sz, f);
    fclose(f);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

// 卡密验证主逻辑：返回 true = 放行，false = 退出
static bool doAuth() {
    std::cout << "\033[36m[*] 正在验证授权...\033[0m" << std::endl;

    // 1) 尝试 Token 复验
    std::string savedToken = loadToken();
    if (!savedToken.empty()) {
        std::string resp = httpPost("/api/check", "{\"token\":\"" + savedToken + "\"}");
        if (!resp.empty() && jsonGet(resp, "ok") == "true") {
            std::cout << "\033[32m[+] Token 验证通过 (到期: "
                      << jsonGet(resp, "expire") << ")\033[0m" << std::endl;
            return true;
        }
        std::cout << "\033[33m[-] Token 已失效，请重新输入卡密\033[0m" << std::endl;
        unlink(TOKEN_FILE);
    }

    // 2) 输入卡密
    std::cout << "\033[36m请输入卡密: \033[0m" << std::flush;
    std::string key;
    std::getline(std::cin, key);
    // trim
    while (!key.empty() && (key.back() == '\n' || key.back() == '\r' || key.back() == ' ')) key.pop_back();
    while (!key.empty() && (key.front() == ' ')) key.erase(0,1);

    if (key.empty()) {
        std::cout << "\033[31m[-] 卡密不能为空\033[0m" << std::endl;
        return false;
    }

    // 转大写
    for (auto& c : key) c = toupper(c);

    std::string devId = getDeviceId();
    std::cout << "\033[36m[*] 设备: " << devId << "\033[0m" << std::endl;

    std::string resp = httpPost("/api/verify",
        "{\"key\":\"" + key + "\",\"device_id\":\"" + devId + "\"}");

    if (resp.empty()) {
        std::cout << "\033[31m[!] 无法连接授权服务器\033[0m" << std::endl;
        return false;
    }

    std::string ok = jsonGet(resp, "ok");
    std::string msg = jsonGet(resp, "message");
    std::string token = jsonGet(resp, "token");

    if (ok == "true") {
        std::cout << "\033[32m[+] " << msg << "\033[0m" << std::endl;
        if (!token.empty()) {
            saveToken(token);
            std::cout << "\033[32m[+] 授权已绑定本设备，下次自动验证\033[0m" << std::endl;
        }
        return true;
    } else {
        std::cout << "\033[31m[-] " << msg << "\033[0m" << std::endl;
        return false;
    }
}
std::atomic<int> pid;
Timer DrawFPS;
float fps = 60;
long int value1 = 970061201, value2 = 16384, value3 = 257;
bool g_stealth_mode = true;  // 默认无后台隐身模式

void k_print(const std::string &text, int delay_ms) {
    for (char c : text) {
        std::cout << c << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    std::cout << std::endl;
}

int main(int argc, char *argv[]) {
    // ★ 主线程绑定小核 (省电稳定，跟哈基米一样)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    CPU_SET(4, &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    
    // ── 品牌横幅 ──
    std::cout << "\033[2J\033[H";
    std::cout << "\033[35m";
    std::cout << "================================================" << std::endl;
    std::cout << "  大米饭先生" << std::endl;
    std::cout << "  https://t.me/+67uRf9NT_04xMGM1" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "\033[0m" << std::endl;

    // ★ 卡密授权验证 — 必须通过才能继续
    if (!doAuth()) {
        std::cout << "\n\033[31m授权验证失败，程序退出。\033[0m" << std::endl;
        return 1;
    }

    // ── 检测是否有交互终端 ──
    //    MT管理器等 GUI 工具启动时没有 stdin, 直接走自动模式
    bool has_tty = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    if (has_tty) {
        // ── 等待用户确认 ──
        std::cout << "\033[33m按 Enter 键继续...\033[0m" << std::flush;
        std::cin.get();

        // ── 探测可用内核 ──
        std::cout << "\n\033[36m正在探测可用内核...\033[0m" << std::endl;
        auto probes = probe_all_drivers();

        // ── 内核选择菜单 ──
        std::cout << "\n\033[36m════════ 请选择加载的内核 ════════\033[0m" << std::endl;
        for (size_t i = 0; i < probes.size(); i++) {
            if (probes[i].found)
                std::cout << "  \033[32m[" << (i+1) << "]\033[0m " << probes[i].desc
                          << " \033[32m[可用]\033[0m" << std::endl;
            else
                std::cout << "  \033[31m[" << (i+1) << "]\033[0m " << probes[i].desc
                          << " \033[31m[不可用]\033[0m" << std::endl;
        }
        std::cout << "  \033[37m[0]\033[0m 自动探测 (按优先级)" << std::endl;
        std::cout << "\033[36m请输入选项 (0-" << probes.size() << "): \033[0m" << std::flush;

        int choice = 0;
        std::cin >> choice;
        std::cin.ignore();

        // ── 根据选择加载驱动 ──
        if (choice >= 1 && choice <= (int)probes.size()) {
            const char* chosen = probes[choice-1].name.c_str();
            std::cout << "\n\033[36m[*] 正在加载 " << chosen << " 驱动...\033[0m" << std::endl;
            if (!driver_init_by_name(chosen)) {
                std::cout << "\033[33m[-] " << chosen << " 加载失败, 尝试备选驱动...\033[0m" << std::endl;
                const char* fallback = nullptr;
                for (size_t i = 0; i < probes.size(); i++) {
                    if (probes[i].name != probes[choice-1].name) {
                        fallback = probes[i].name.c_str();
                        break;
                    }
                }
                if (fallback) {
                    if (!driver_init_by_name(fallback)) {
                        std::cout << "\033[31m[!!] 所有驱动加载失败, 程序退出\033[0m" << std::endl;
                        exit(1);
                    }
                } else {
                    std::cout << "\033[31m[!!] 无可用备选驱动, 程序退出\033[0m" << std::endl;
                    exit(1);
                }
            }
        } else {
            std::cout << "\n\033[36m[*] 自动探测驱动...\033[0m" << std::endl;
            driver_init();
        }

        // ── 后台模式选择 ──
        std::cout << "\n\033[36m════════ 请选择运行模式 ════════\033[0m" << std::endl;
        std::cout << "  \033[32m[1]\033[0m 无后台模式 (隐蔽, 进程伪装为系统线程)" << std::endl;
        std::cout << "  \033[33m[2]\033[0m 有后台模式 (普通, 进程名可见)" << std::endl;
        std::cout << "\033[36m请输入选项 (1-2): \033[0m" << std::flush;

        int bg_choice = 0;
        std::cin >> bg_choice;
        std::cin.ignore();
        g_stealth_mode = (bg_choice != 2);  // 默认无后台, 只有明确选2才退出隐身
    } else {
        // 无终端 → 默认无后台模式, 自动加载驱动
        driver_init();
        g_stealth_mode = true;
    }

    // 驱动就绪, 继续初始化
    std::cout << "\n\033[32m[√] 驱动就绪, 启动中...\033[0m\n" << std::endl;
    k_print(">>> 系统初始化中...", 10);
    k_print(">>> 核心载入中...", 30);
    k_print(">>> 运行状态: 正常", 10);

    ::graphics = GraphicsManager::getGraphicsInterface(GraphicsManager::OPENGL);
    ::screen_config();
    ::native_window_screen_x =
            (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height
                                                        : ::displayInfo.width);
    ::native_window_screen_y = ::native_window_screen_x;
    ::abs_ScreenX = ::native_window_screen_x;
    ::abs_ScreenY =
            (::displayInfo.height < ::displayInfo.width ? ::displayInfo.height
                                                        : ::displayInfo.width);
    ::window = android::ANativeWindowCreator::Create(
            "Surface", native_window_screen_x, native_window_screen_y, false);
    graphics->Init_Render(::window, native_window_screen_x,
                          native_window_screen_y);
    Touch::Init(
            {static_cast<float>(::abs_ScreenX), static_cast<float>(::abs_ScreenY)},
            true);
    Touch::setOrientation(displayInfo.orientation);
    Timer draw_timer("DrawThread");
    draw_timer.BindCurrentThreadToCores(true, "DrawThread");
    // 驱动已在上面交互选择中初始化, 无需重复调用
    if (g_stealth_mode) stealth_init();  // 用户选择了无后台模式才启用隐身
    std::thread(read_thread, value1, value2, value3).detach();
    std::thread(音量).detach();
    DrawFPS.SetFps(fps);
    DrawFPS.InitFpsControl();
    DrawFPS.GetCpuCoreCount();
    ::init_My_drawdata();

    // ==== 新增：打印主线程 ID ====
    printf("[MAIN] 主线程 ID: %lu\n", (unsigned long)pthread_self());

    static bool flag = true;
    while (flag) {
        drawBegin();
        graphics->NewFrame();
        Layout_tick_UI(&flag);
        graphics->EndFrame();
        DrawFPS.SetFps(fps);
        DrawFPS.ControlFps();
    }
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(::window);
    return 0;
}