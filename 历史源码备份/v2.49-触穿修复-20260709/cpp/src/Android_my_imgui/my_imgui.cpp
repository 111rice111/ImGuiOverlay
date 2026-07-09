#include "my_imgui.h"
#include "imgui_internal.h"
#include <unistd.h>
#include <string>
namespace ImGui {
ImFont *SystemFont = NULL;

// v2.47优化: 字体文件内存缓存, 避免主字体+UI字体重复读盘
// 同一字体文件只 ImFileLoadToMemory 一次, 后续从缓存复用
struct FontFileCache {
    std::string path;
    void* data = nullptr;
    size_t size = 0;
};
static FontFileCache g_font_cache;

static void* LoadFontFileCached(const char* filename, size_t* out_size) {
    // 命中缓存 (同一文件路径)
    if (g_font_cache.data && g_font_cache.path == filename) {
        *out_size = g_font_cache.size;
        return g_font_cache.data;
    }
    // 释放旧缓存
    if (g_font_cache.data) {
        free(g_font_cache.data);
        g_font_cache.data = nullptr;
        g_font_cache.size = 0;
        g_font_cache.path.clear();
    }
    // 读取新文件
    size_t data_size = 0;
    void* data = ImFileLoadToMemory(filename, "rb", &data_size, 0);
    if (!data) return nullptr;
    // 缓存 (FontDataOwnedByAtlas=false, 内存由我们管理, 不会被 ImGui 释放)
    g_font_cache.data = data;
    g_font_cache.size = data_size;
    g_font_cache.path = filename;
    *out_size = data_size;
    return data;
}

    bool My_Android_LoadSystemFont(float SizePixels) {
        // 直接指定一个已知存在的系统字体路径，避免遍历和加载坏文件
        const char *fontPath = "/data/local/bin/与辅助放同一目录.ttf";

        // 如果上面路径不存在，可以尝试备用路径
        if (access(fontPath, R_OK) != 0) {
            fontPath = "/data/local/bin/与辅助放同一目录.ttf";
        }

        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        config.SizePixels = SizePixels;
        config.OversampleH = 1;

        ImFont *tryFont = My_AddFontFromFileTTF(fontPath, 0, &config, nullptr);
        if (tryFont) { SystemFont = tryFont; return true; }

        // 如果还是失败，则尝试其他几个常见路径
        const char *fallbackPaths[] = {
                "/system/fonts/NotoSerifCJK-Regular.ttc",
                "/system/fonts/NotoSansSC-Regular.otf",
                "/system/fonts/NotoSansTC-Regular.otf",
                "/system/fonts/DroidSans.ttf"
        };
        for (const char *path : fallbackPaths) {
            if (access(path, R_OK) != 0) continue;
            tryFont = My_AddFontFromFileTTF(path, 0, &config, nullptr);
            if (tryFont) { SystemFont = tryFont; return true; }
        }

        // 所有路径都失败，直接报错
        IM_ASSERT_USER_ERROR(0, "All system fonts failed to load!");
        return false;
    }
ImFont *My_AddFontFromFileTTF(const char *filename, float size_pixels,
                              const ImFontConfig *font_cfg_template,
                              const ImWchar *glyph_ranges) {
  ImGuiIO &io = ImGui::GetIO();
  // v2.47优化: 使用缓存加载, 主字体+UI字体来自同一文件时只读盘一次
  // v2.47修复: 原代码有变量遮蔽bug (内层 void* data 遮蔽外层), 加载成功仍返回 NULL
  size_t data_size = 0;
  void *data = LoadFontFileCached(filename, &data_size);
  if (!data) {
      return nullptr;   // 加载失败，直接返回空，尝试下一个字体
  }
  ImFontConfig font_cfg =
      font_cfg_template ? *font_cfg_template : ImFontConfig();
  if (font_cfg.Name[0] == '\0') {
    // Store a short copy of filename into into the font name for convenience
    const char *p;
    for (p = filename + strlen(filename);
         p > filename && p[-1] != '/' && p[-1] != '\\'; p--) {
    }
    ImFormatString(font_cfg.Name, IM_ARRAYSIZE(font_cfg.Name), "%s, %.0fpx", p,
                   size_pixels);
  }
  ImFont *local_Font = io.Fonts->AddFontFromMemoryTTF(
      data, (int)data_size, size_pixels, &font_cfg, glyph_ranges);
  // 注意: 不释放 data, 因为 FontDataOwnedByAtlas=false, 缓存供下次复用
  // 缓存会在下次加载不同文件时自动释放
  return local_Font;
}
} // namespace ImGui
