#include "AndroidImgui.h"
#include "Android_draw/ThreadAffinity.h"
#include "Android_draw/driver.h"
#include "Android_draw/stealth.h"
#include "Android_draw/anti_re.h"
#include "Android_draw/secure_runtime.h"
#include "Android_draw/net_client.h"
#include "Android_draw/net_config.h"
#include "Android_draw/game_offsets.h"
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
#include <sched.h>

extern void 音量();
char extractedString[64]{};

// ========== 日志控制: Release 构建 (-DNDEBUG) 自动屏蔽 ==========
#ifdef NDEBUG
  #define DPRINTF(...) ((void)0)
  #define DCOUT(x)     ((void)0)
#else
  #define DPRINTF(...) printf(__VA_ARGS__)
  #define DCOUT(x)     std::cout << x
#endif

// 卡密验证: 连接本地服务器 (旧手机 Termux @ 192.168.1.13:8080)
static bool doAuth() {
    DPRINTF("\033[36m[*] 正在连接本地验证服务器...\033[0m\n");
    
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
        DPRINTF("\033[36m[*] 检测到已保存授权，验证中...\033[0m\n");
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
                    g_license.card_type = j.value("card_type", "device");
                    g_license.device_id = g_device_id.empty() ? get_device_id() : g_device_id;
                    DPRINTF("\033[32m[+] 自动验证通过\033[0m\n");
                    if (!g_license.expire.empty()) {
                        DPRINTF("\033[32m[+] 卡密有效期: %s\033[0m\n", g_license.expire.c_str());
                    }
                    return true;
                }
            } catch (...) {}
        }
        DPRINTF("\033[33m[-] Token 已失效，需要重新验证\033[0m\n");
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
        DPRINTF("\033[36m[*] 非终端模式, 读取 %s ...\033[0m\n", key_file.c_str());
        FILE* f = fopen(key_file.c_str(), "r");
        if (f) {
            char buf[256] = {0};
            fgets(buf, sizeof(buf), f);
            fclose(f);
            key = buf;
        } else {
            DPRINTF("\033[31m[!] 未找到 %s，请先创建该文件存放卡密\033[0m\n", key_file.c_str());
        }
    }
    
    while (!key.empty() && key.back() <= ' ') key.pop_back();
    while (!key.empty() && key.front() <= ' ') key.erase(0,1);
    if (key.empty()) { DPRINTF("\033[31m[-] 卡密为空，验证失败\033[0m\n"); return false; }
    for (auto& c : key) c = toupper(c);

    DPRINTF("\033[36m[*] 正在验证卡密: %s...\033[0m\n", key.c_str());
    if (api_verify_key(key)) {
        // 保存 token 到本地，下次自动验证
        if (!g_license.token.empty()) {
            FILE* f = fopen(token_file.c_str(), "w");
            if (f) { fwrite(g_license.token.c_str(), 1, g_license.token.size(), f); fclose(f); }
            DPRINTF("\033[32m[+] 授权已绑定本设备，下次自动验证\033[0m\n");
        }
        if (!g_license.expire.empty()) {
            DPRINTF("\033[32m[+] 卡密有效期: %s\033[0m\n", g_license.expire.c_str());
            std::string es = g_license.expire;
            if (es.length() >= 10) {
                int ey = atoi(es.substr(0,4).c_str());
                int em = atoi(es.substr(5,2).c_str());
                int ed = atoi(es.substr(8,2).c_str());
                auto now = std::time(nullptr);
                struct tm* tm_now = std::localtime(&now);
                int ny = tm_now->tm_year + 1900, nm = tm_now->tm_mon + 1, nd = tm_now->tm_mday;
                int days = (ey - ny) * 365 + (em - nm) * 30 + (ed - nd);
                if (days > 365) DPRINTF("\033[32m[+] 永久有效\033[0m\n");
                else if (days > 0) DPRINTF("\033[32m[+] 剩余约 %d 天\033[0m\n", days);
                else if (days >= 0) DPRINTF("\033[33m[!] 今天到期！\033[0m\n");
                else DPRINTF("\033[31m[!] 已过期 %d 天\033[0m\n", -days);
            }
        }
        return true;
    }
    DPRINTF("\033[31m[-] 卡密验证失败\033[0m\n");
    return false;
}
std::atomic<int> pid;
Timer DrawFPS;
float fps = 60;
long int value1 = GAME_OFFSET(init_value1, 970061201), value2 = 16384, value3 = GAME_OFFSET(init_value3, 257);
bool g_stealth_mode = true;

void k_print(const std::string &text, int delay_ms) {
    // v2.47优化: 移除逐字符 sleep (原实现累计约700ms阻塞启动), 一次性输出保留文字
    (void)delay_ms;
    std::cout << text << std::endl;
}

int main(int argc, char *argv[]) {
    // ★ 安全检测 checkpoint 1: 环境 (模拟器/VPN) — 最先执行
    cp_environment();
    AntiBypassGuard::instance().set_checkpoint_ok();
    
    // ★ 动态CPU亲和性: 根据设备实际核心数分配, 兼容4核/8核/更多
    {
        int cores = sysconf(_SC_NPROCESSORS_CONF);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        if (cores > 4) {
            CPU_SET(4, &cpuset);
        } else if (cores > 2) {
            CPU_SET(cores - 1, &cpuset);
        }
        sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    }
    
    std::cout << "\033[2J\033[H";
    std::cout << "\033[35m";
    std::cout << "================================================" << std::endl;
    std::cout << "  大米饭先生 - " << OVERLAY_VERSION << std::endl;
    std::cout << "  " << XORSTR("https://t.me/+67uRf9NT_04xMGM1") << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "\033[0m" << std::endl;

    // ★ 第一步: 检查更新和公告 (在任何操作之前)
    // v2.47优化: 使用短超时 (3s), 避免弱网卡死启动; 超时跳过走降级路径
    DPRINTF("\033[36m[*] 正在检查更新和公告...\033[0m\n");
    g_device_id = get_device_id();
    UpdateInfo update_info = api_check_v2_fast();

    // 展示公告
    if (!update_info.announcements.empty()) {
        for (auto& ann : update_info.announcements) {
            const char* prio_color;
            const char* prio_tag;
            if (ann.priority >= 2) { prio_color = "\033[1;31m"; prio_tag = "紧急"; }
            else if (ann.priority >= 1) { prio_color = "\033[1;33m"; prio_tag = "重要"; }
            else { prio_color = "\033[33m"; prio_tag = "公告"; }
            printf("\n%s========== [%s] %s ==========\033[0m\n", prio_color, prio_tag, ann.title.c_str());
            printf("%s%s\033[0m\n", prio_color, ann.content.c_str());
            printf("\033[37m--------------------------------------------\033[0m\n");
        }
        printf("\n");
    }

    // 强制更新检查
    if (update_info.force_update) {
        printf("\n\033[1;31m");
        printf("╔══════════════════════════════════════════╗\n");
        printf("║          !! 强制更新通知 !!              ║\n");
        printf("╠══════════════════════════════════════════╣\n");
        printf("║  当前版本 %s 已过期，必须更新到 %-15s║\n", OVERLAY_VERSION_STR, update_info.version_name.c_str());
        printf("║  最低要求版本: v%-24d║\n", update_info.force_min_version);
        printf("╠══════════════════════════════════════════╣\n");
        if (!update_info.changelog.empty()) {
            printf("║  更新内容:                               ║\n");
            printf("║  %-39s║\n", update_info.changelog.c_str());
            printf("╠══════════════════════════════════════════╣\n");
        }
        if (!update_info.url.empty()) {
            printf("║  下载: %-32s║\n", update_info.url.c_str());
        }
        printf("╚══════════════════════════════════════════╝\n");
        printf("\033[0m\n");
        printf("\033[31m[!] 强制更新，程序退出。请更新后再运行。\033[0m\n");
        exit(1);
    }

    // 可选更新提示
    if (update_info.available && !update_info.force_update) {
        printf("\033[33m[!] 有新版本可用: %s\033[0m\n", update_info.version_name.c_str());
        if (!update_info.changelog.empty())
            printf("\033[33m    更新内容: %s\033[0m\n", update_info.changelog.c_str());
        printf("\n");
    }

    // ★ 第二步: 反调试检测 checkpoint 2 (在授权之前)
    cp_anti_debug();

    // ★ 第三步: 卡密授权验证 (v3.x 加固: 多路径防单指令 patch)
    bool auth_ok = doAuth();
    if (auth_ok) {
        AntiBypassGuard::instance().set_auth_ok();
        AntiBypassGuard::instance().set_auth2_ok();
    }
    if (!auth_ok) {
        printf("\033[31m[!] 授权失败，程序退出\033[0m\n");
        exit(1);
    }
    // 二次冗余检查 — 不依赖上面那条 if 的唯一性
    // 攻击者若只 patch 上面那个 !auth_ok 分支，这里仍会触发
    if (!auth_ok) { volatile int* p = nullptr; *p = 0; }
    // 更隐蔽的 guard: 将 auth_ok 结果编码到后续逻辑中
    // 如果 auth_ok==false，has_tty_check 跳过但 driver_init 位置会被跳过导致崩溃
    // (这是架构层级的反 patch，非简单条件跳转)

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
    // v2.45: 用真实屏幕尺寸创建窗口
    // 修复 v2.44 方形窗口在部分设备上因 SurfaceFlinger 缩放导致渲染/触摸坐标不一致
    // 显式 ANativeWindow_setBuffersGeometry 解决 v2.41 EGL 重建失败问题（见 OpenGLGraphics）
    ::native_window_screen_x = ::displayInfo.width;
    ::native_window_screen_y = ::displayInfo.height;
    ::abs_ScreenX = ::native_window_screen_x;
    ::abs_ScreenY = ::native_window_screen_y;
    ::window = android::ANativeWindowCreator::Create(
            "Surface", native_window_screen_x, native_window_screen_y, false);
    graphics->Init_Render(::window, native_window_screen_x,
                          native_window_screen_y);
    Touch::Init(
            {static_cast<float>(::displayInfo.width), static_cast<float>(::displayInfo.height)},
            true);
    Touch::setOrientation(displayInfo.orientation);
    Touch::setOrientation(displayInfo.orientation);
    Timer draw_timer("DrawThread");
    draw_timer.BindCurrentThreadToCores(true, "DrawThread");
    // ★ 卡屏修复: DrawThread 提升为实时优先级, 防 DataThread 抢 CPU
    {
        struct sched_param param;
        param.sched_priority = 1;
        sched_setscheduler(0, SCHED_RR, &param);
    }
    if (g_stealth_mode) stealth_init();
    // 心跳线程: 每60秒通知服务器在线, 同时做完整性检查
    std::thread([]{ while(true){ std::this_thread::sleep_for(std::chrono::seconds(60)); cp_integrity(); AntiBypassGuard::instance().set_integrity_ok(); if(api_heartbeat()) AntiBypassGuard::instance().set_hb_ok(); } }).detach();
    // 完整性监控线程 (v3.1 加固)
    start_integrity_monitor();
    // ★ 启动时立即执行一次完整性校验, 确保 FLAG_INTEGRITY 立即可用
    cp_integrity();
    AntiBypassGuard::instance().set_integrity_ok();
    // ★ 初始标记心跳OK (首次心跳还没跑, 但当前状态是健康的)
    AntiBypassGuard::instance().set_hb_ok();
    std::thread(read_thread, value1, value2, value3).detach();  // 先启扫描，不等待配置
    // v2.47优化: 配置拉取异步化 — read_thread 已用本地回退值运行, 配置就绪后自动切换
    // 原实现: 主线程同步阻塞最多20秒 (api_fetch_config 10s + api_fetch_game_offsets 10s)
    // 现: 后台线程拉取, 主线程立即进入渲染循环
    std::thread([]{
        if (!api_fetch_config(g_license.token, g_device_id)) {
            printf("\033[33m[WARN] 无法获取服务端配置，将使用降级模式\033[0m\n");
        }
        if (api_fetch_game_offsets()) {
            DPRINTF("\033[32m[+] 游戏偏移已从服务端加载\033[0m\n");
        } else {
            DPRINTF("\033[33m[!] 服务端偏移获取失败, 使用本地回退值\033[0m\n");
        }
    }).detach();
    std::thread(音量).detach();
    DrawFPS.SetFps(fps);
    bool main_thread_flag = true;
    DrawFPS.InitFpsControl();
    DrawFPS.GetCpuCoreCount();
    ::init_My_drawdata();
    
    DPRINTF("[MAIN] 主线程 ID: %lu\n", (unsigned long)pthread_self());
    
    static bool flag = true;
    static int draw_affinity_rebind_counter = 0;  // v2.39: 循环重绑计数器
    while (flag) {
        // v3.1 加固: 每30分钟刷新服务端配置 → 后台线程, 不阻塞渲染
        if (config_needs_refresh()) {
            std::thread([]{
                api_fetch_config(g_license.token, g_device_id);
                api_fetch_game_offsets();
            }).detach();
        }
        screen_config();  // v2.44: 确保 drawBegin 用最新 displayInfo
        drawBegin();
        graphics->NewFrame();
        Layout_tick_UI(&flag);
        graphics->EndFrame();
        DrawFPS.SetFps(fps);
        DrawFPS.ControlFps();
        // ★ v2.39+: 每1800帧(~30秒)重新强制执行亲和性, 防止内核调度器/EAS重置绑核
        if (++draw_affinity_rebind_counter >= 1800) {
            draw_affinity_rebind_counter = 0;
            draw_timer.BindCurrentThreadToCores(true, "DrawThread");
        }
    }
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(::window);
    return 0;
}
