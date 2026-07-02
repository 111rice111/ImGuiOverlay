#include "AndroidImgui.h"
#include "Android_draw/ThreadAffinity.h"
#include "Android_draw/driver.h"
#include "Android_draw/stealth.h"
#include "Android_draw/anti_re.h"
#include "Android_draw/net_client.h"
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

extern void 音量();
char extractedString[64]{};

// 卡密验证: 连接本地服务器 (旧手机 Termux @ 192.168.1.13:8080)
static bool doAuth() {
    printf("\033[36m[*] 正在连接本地验证服务器...\033[0m\n");
    
    // 1. 先尝试读取已保存的 token (快速验证)
    const std::string token_file = XORSTR("/data/local/bin/auth_token.dat");
    std::string saved_token;
    {
        FILE* f = fopen(token_file.c_str(), "r");
        if (f) {
            char buf[256] = {0};
            fgets(buf, sizeof(buf), f);
            fclose(f);
            saved_token = buf;
            while (!saved_token.empty() && saved_token.back() <= ' ') saved_token.pop_back();
        }
    }
    
    if (!saved_token.empty()) {
        printf("\033[36m[*] 检测到已保存授权，验证中...\033[0m\n");
        json req;
        req["token"] = saved_token;
        req["device_id"] = get_device_id();  // 必须传设备ID匹配绑定
        std::string resp = http_post_enc(API_CHECK(), req.dump());
        if (!resp.empty()) {
            try {
                json j = json::parse(resp);
                if (j.value("ok", false)) {
                    g_license.verified = true;
                    g_license.token = saved_token;
                    g_license.expire = j.value("expire", "");
                    g_license.device_id = g_device_id.empty() ? get_device_id() : g_device_id;
                    printf("\033[32m[+] 自动验证通过\033[0m\n");
                    if (!g_license.expire.empty()) {
                        printf("\033[32m[+] 卡密有效期: %s\033[0m\n", g_license.expire.c_str());
                    }
                    return true;
                }
            } catch (...) {}
        }
        printf("\033[33m[-] Token 已失效，需要重新验证\033[0m\n");
        unlink(token_file.c_str());
    }
    
    // 2. 获取卡密
    std::string key;
    bool has_tty = isatty(STDIN_FILENO);
    
    if (has_tty) {
        printf("\033[36m请输入卡密: \033[0m");
        fflush(stdout);
        std::getline(std::cin, key);
    } else {
        const std::string key_file = XORSTR("/data/local/bin/key.txt");
        printf("\033[36m[*] 非终端模式, 读取 %s ...\033[0m\n", key_file.c_str());
        FILE* f = fopen(key_file.c_str(), "r");
        if (f) {
            char buf[256] = {0};
            fgets(buf, sizeof(buf), f);
            fclose(f);
            key = buf;
        } else {
            printf("\033[31m[!] 未找到 %s，请先创建该文件存放卡密\033[0m\n", key_file.c_str());
        }
    }
    
    while (!key.empty() && key.back() <= ' ') key.pop_back();
    while (!key.empty() && key.front() <= ' ') key.erase(0,1);
    if (key.empty()) { printf("\033[31m[-] 卡密为空，验证失败\033[0m\n"); return false; }
    for (auto& c : key) c = toupper(c);

    printf("\033[36m[*] 正在验证卡密: %s...\033[0m\n", key.c_str());
    if (api_verify_key(key)) {
        // 保存 token 到本地，下次自动验证
        if (!g_license.token.empty()) {
            FILE* f = fopen(token_file.c_str(), "w");
            if (f) { fwrite(g_license.token.c_str(), 1, g_license.token.size(), f); fclose(f); }
            printf("\033[32m[+] 授权已绑定本设备，下次自动验证\033[0m\n");
        }
        if (!g_license.expire.empty()) {
            printf("\033[32m[+] 卡密有效期: %s\033[0m\n", g_license.expire.c_str());
            std::string es = g_license.expire;
            if (es.length() >= 10) {
                int ey = atoi(es.substr(0,4).c_str());
                int em = atoi(es.substr(5,2).c_str());
                int ed = atoi(es.substr(8,2).c_str());
                auto now = std::time(nullptr);
                struct tm* tm_now = std::localtime(&now);
                int ny = tm_now->tm_year + 1900, nm = tm_now->tm_mon + 1, nd = tm_now->tm_mday;
                int days = (ey - ny) * 365 + (em - nm) * 30 + (ed - nd);
                if (days > 365) printf("\033[32m[+] 永久有效\033[0m\n");
                else if (days > 0) printf("\033[32m[+] 剩余约 %d 天\033[0m\n", days);
                else if (days >= 0) printf("\033[33m[!] 今天到期！\033[0m\n");
                else printf("\033[31m[!] 已过期 %d 天\033[0m\n", -days);
            }
        }
        return true;
    }
    printf("\033[31m[-] 卡密验证失败\033[0m\n");
    return false;
}
std::atomic<int> pid;
Timer DrawFPS;
float fps = 60;
long int value1 = 970061201, value2 = 16384, value3 = 257;
bool g_stealth_mode = true;

void k_print(const std::string &text, int delay_ms) {
    for (char c : text) {
        std::cout << c << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    std::cout << std::endl;
}

int main(int argc, char *argv[]) {
    // ★ 安全检测（最先执行）
    security_checkpoint();
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    CPU_SET(4, &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    
    std::cout << "\033[2J\033[H";
    std::cout << "\033[35m";
    std::cout << "================================================" << std::endl;
    std::cout << "  大米饭先生" << std::endl;
    std::cout << "  https://t.me/+67uRf9NT_04xMGM1" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "\033[0m" << std::endl;

    // ★ 卡密授权验证 (在任何操作之前)
    if (!doAuth()) {
        printf("\033[31m[!] 授权失败，程序退出\033[0m\n");
        exit(1);
    }

    bool has_tty = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if (has_tty) {
        std::cout << "\033[33m按 Enter 键继续...\033[0m" << std::flush;
        std::cin.get();
        auto probes = probe_all_drivers();
        std::cout << "\n\033[36m════════ 请选择加载的内核 ════════\033[0m" << std::endl;
        for (size_t i = 0; i < probes.size(); i++) {
            if (probes[i].found)
                std::cout << "  \033[32m[" << (i+1) << "]\033[0m " << probes[i].desc << " \033[32m[可用]\033[0m" << std::endl;
            else
                std::cout << "  \033[31m[" << (i+1) << "]\033[0m " << probes[i].desc << " \033[31m[不可用]\033[0m" << std::endl;
        }
        std::cout << "  \033[37m[0]\033[0m 自动探测 (按优先级)" << std::endl;
        std::cout << "\033[36m请输入选项 (0-" << probes.size() << "): \033[0m" << std::flush;
        int choice = 0;
        std::cin >> choice;
        std::cin.ignore();
        if (choice >= 1 && choice <= (int)probes.size()) {
            if (!driver_init_by_name(probes[choice-1].name.c_str())) {
                for (size_t i = 0; i < probes.size(); i++) {
                    if (probes[i].name != probes[choice-1].name) {
                        if (driver_init_by_name(probes[i].name.c_str())) break;
                    }
                }
            }
        } else {
            driver_init();
        }
        std::cout << "\n\033[36m════════ 请选择运行模式 ════════\033[0m" << std::endl;
        std::cout << "  \033[32m[1]\033[0m 无后台模式 (隐蔽)" << std::endl;
        std::cout << "  \033[33m[2]\033[0m 有后台模式 (普通)" << std::endl;
        std::cout << "\033[36m请输入选项 (1-2): \033[0m" << std::flush;
        int bg_choice = 0;
        std::cin >> bg_choice;
        std::cin.ignore();
        g_stealth_mode = (bg_choice != 2);
    } else {
        driver_init();
        g_stealth_mode = true;
    }

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
    if (g_stealth_mode) stealth_init();
    // 心跳线程: 每60秒通知服务器在线
    std::thread([]{ while(true){ std::this_thread::sleep_for(std::chrono::seconds(60)); api_heartbeat(); } }).detach();
    std::thread(read_thread, value1, value2, value3).detach();
    std::thread(音量).detach();
    DrawFPS.SetFps(fps);
    bool main_thread_flag = true;
    DrawFPS.InitFpsControl();
    DrawFPS.GetCpuCoreCount();
    ::init_My_drawdata();
    
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
