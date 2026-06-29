#include "AndroidImgui.h"
#include "Android_draw/ThreadAffinity.h"
#include "Android_draw/driver.h"
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
    
    std::cout << "\033[2J\033[H";
    std::cout << "\033[35m";
    std::cout << "================================================" << std::endl;
    std::cout << "  大米饭先生" << std::endl;
    std::cout << "  https://t.me/+67uRf9NT_04xMGM1" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "\033[0m" << std::endl;
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
            "FuckNetEase", native_window_screen_x, native_window_screen_y, false);
    graphics->Init_Render(::window, native_window_screen_x,
                          native_window_screen_y);
    Touch::Init(
            {static_cast<float>(::abs_ScreenX), static_cast<float>(::abs_ScreenY)},
            true);
    Touch::setOrientation(displayInfo.orientation);
    Timer draw_timer("DrawThread");
    draw_timer.BindCurrentThreadToCores(true, "DrawThread");
    driver_init(); // ★ 驱动必须在读线程之前初始化
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