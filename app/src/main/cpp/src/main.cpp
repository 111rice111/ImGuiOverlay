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
#include <pthread.h>   // 新增

extern void 音量();
// 不再需要 SaveConfig()，配置由按钮退出时保存

char extractedString[64]{};
std::atomic<int> pid;
Timer DrawFPS;
float fps = 60;
long int value1 = 970061201, value2 = 16384, value3 = 257;

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

    // ── 等待用户按 Enter ──
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
    std::cin.ignore(); // 清除换行符

    // ── 根据选择加载驱动 ──
    if (choice >= 1 && choice <= (int)probes.size()) {
        const char* chosen = probes[choice-1].name.c_str();
        std::cout << "\n\033[36m[*] 正在加载 " << chosen << " 驱动...\033[0m" << std::endl;
        if (!driver_init_by_name(chosen)) {
            // 指定驱动失败, 尝试备选
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
        // 自动探测
        std::cout << "\n\033[36m[*] 自动探测驱动...\033[0m" << std::endl;
        driver_init();
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
    stealth_init();  // ★ 进程隐身: 伪名 + OOM保护
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