#include "OpenGLGraphics.h"
#include "imgui_impl_opengl3.h"
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android/native_window.h>
#if !defined(EGL_OPENGL_ES3_BIT)
// 如果 EGL_OPENGL_ES3_BIT 未定义，则编译这部分代码
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif
bool OpenGLGraphics::Create() {
  const EGLint egl_attributes[] = {EGL_BLUE_SIZE,
                                   8,
                                   EGL_GREEN_SIZE,
                                   8,
                                   EGL_RED_SIZE,
                                   8,
                                   EGL_ALPHA_SIZE,
                                   8,
                                   EGL_DEPTH_SIZE,
                                   16,
                                   EGL_RENDERABLE_TYPE,
                                   EGL_OPENGL_ES3_BIT,
                                   EGL_SURFACE_TYPE,
                                   EGL_WINDOW_BIT,
                                   EGL_NONE};
  m_EglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  eglInitialize(m_EglDisplay, nullptr, nullptr);
  EGLint num_configs = 0;
  eglChooseConfig(m_EglDisplay, egl_attributes, nullptr, 0, &num_configs);
  eglChooseConfig(m_EglDisplay, egl_attributes, &m_EglConfig, 1, &num_configs);
  eglGetConfigAttrib(m_EglDisplay, m_EglConfig, EGL_NATIVE_VISUAL_ID,
                     &m_EglFormat);
  // ★ v2.45: 显式传尺寸，确保 buffer 与屏幕 1:1（修复部分设备 EGL 重建失败）
  ANativeWindow_setBuffersGeometry(m_Window, (int)m_Width, (int)m_Height, m_EglFormat);
  const EGLint egl_context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                           EGL_NONE};
  m_EglContext = eglCreateContext(m_EglDisplay, m_EglConfig, EGL_NO_CONTEXT,
                                  egl_context_attributes);
  m_EglSurface =
      eglCreateWindowSurface(m_EglDisplay, m_EglConfig, m_Window, nullptr);
  eglMakeCurrent(m_EglDisplay, m_EglSurface, m_EglSurface, m_EglContext);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  return true;
}
void OpenGLGraphics::Setup() { ImGui_ImplOpenGL3_Init("#version 300 es"); }
void OpenGLGraphics::PrepareFrame(bool resize) { ImGui_ImplOpenGL3_NewFrame(); }
void OpenGLGraphics::Render(ImDrawData *drawData) {
  // glViewport 由 ImGui_ImplOpenGL3_RenderDrawData 内部 SetupRenderState 设置
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(drawData);
  eglSwapBuffers(m_EglDisplay, m_EglSurface);
}
void OpenGLGraphics::PrepareShutdown() { ImGui_ImplOpenGL3_Shutdown(); }
void OpenGLGraphics::RecreateSurface(ANativeWindow *newWindow, float width, float height) {
  // ★ 旋转时重建 EGL surface，不销毁 context，保留所有纹理和 ImGui 状态
  if (m_EglSurface != EGL_NO_SURFACE) {
    eglMakeCurrent(m_EglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, m_EglContext);
    eglDestroySurface(m_EglDisplay, m_EglSurface);
    m_EglSurface = EGL_NO_SURFACE;
  }
  // ★ v2.45: 显式传宽高，确保 buffer 尺寸与屏幕一致
  ANativeWindow_setBuffersGeometry(newWindow, (int)width, (int)height, m_EglFormat);
  // 创建新 EGL surface
  m_EglSurface = eglCreateWindowSurface(m_EglDisplay, m_EglConfig, newWindow, nullptr);
  if (m_EglSurface == EGL_NO_SURFACE) {
    __android_log_print(ANDROID_LOG_ERROR, "ImGui", "[-] RecreateSurface: eglCreateWindowSurface failed");
    return;
  }
  // 绑定新 surface 到现有 context
  eglMakeCurrent(m_EglDisplay, m_EglSurface, m_EglSurface, m_EglContext);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  __android_log_print(ANDROID_LOG_INFO, "ImGui", "[+] RecreateSurface: %dx%d", (int)width, (int)height);
}
void OpenGLGraphics::Cleanup() {
  eglMakeCurrent(m_EglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(m_EglDisplay, m_EglContext);
  eglDestroySurface(m_EglDisplay, m_EglSurface);
  eglTerminate(m_EglDisplay);
  m_EglDisplay = EGL_NO_DISPLAY;
  m_EglSurface = EGL_NO_SURFACE;
  m_EglContext = EGL_NO_CONTEXT;
}
BaseTexData *OpenGLGraphics::LoadTexture(BaseTexData *tex, void *pixel_data) {
  auto tex_data = new OpenglTextureData();
  tex_data->Width = tex->Width;
  tex_data->Height = tex->Height;
  tex_data->Channels = tex->Channels;
  GLuint textureId;
  glGenTextures(1, &textureId);
  glBindTexture(GL_TEXTURE_2D, textureId);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_data->Width, tex_data->Height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, pixel_data);
  tex_data->DS = (void *)(intptr_t)textureId;
  return tex_data;
}
void OpenGLGraphics::RemoveTexture(BaseTexData *tex) {
  auto tex_data = (OpenglTextureData *)tex;
  auto textureId = (GLuint)(intptr_t)tex_data->DS;
  glDeleteTextures(1, &textureId);
  delete tex_data;
}
