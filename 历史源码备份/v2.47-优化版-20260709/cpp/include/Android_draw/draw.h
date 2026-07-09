#ifndef NATIVESURFACE_DRAW_H
#define NATIVESURFACE_DRAW_H
#include "AndroidImgui.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "TouchHelperA.h"
#include "my_imgui.h"
#include "native_surface/ANativeWindowCreator.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
extern ANativeWindow *window;
extern android::ANativeWindowCreator::DisplayInfo displayInfo;
extern ImGuiWindow *g_window;
extern int abs_ScreenX, abs_ScreenY;
extern int native_window_screen_x, native_window_screen_y;
extern std::unique_ptr<AndroidImgui> graphics;
struct Last_ImRect {
  float Pos_x;
  float Pos_y;
  float Size_x;
  float Size_y;
};
extern struct Last_ImRect LastCoordinate;
extern void screen_config();
extern void drawBegin();
extern void Layout_tick_UI(bool *main_thread_flag);
extern void init_My_drawdata();
extern void read_thread(long int PD1, long int PD2, long int PD3);
#endif