#include "my_imgui.h"
#include "imgui_internal.h"
#include <unistd.h>
namespace ImGui {
ImFont *SystemFont = NULL;
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
        if (tryFont) {
            SystemFont = tryFont;
            return true;
        }

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
            if (tryFont) {
                SystemFont = tryFont;
                return true;
            }
        }

        // 所有路径都失败，直接报错
        IM_ASSERT_USER_ERROR(0, "All system fonts failed to load!");
        return false;
    }
ImFont *My_AddFontFromFileTTF(const char *filename, float size_pixels,
                              const ImFontConfig *font_cfg_template,
                              const ImWchar *glyph_ranges) {
  ImGuiIO &io = ImGui::GetIO();
  size_t data_size = 0;
  void *data = ImFileLoadToMemory(filename, "rb", &data_size, 0);
  if (!data) {
      void *data = ImFileLoadToMemory(filename, "rb", &data_size, 0);
      if (!data) {
          return NULL;   // 加载失败，直接返回空，尝试下一个字体
      }
    return NULL;
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
  free(data);
  data = NULL;
  return local_Font;
}
} // namespace ImGui