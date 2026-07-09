LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := IdentityV_灾荒年.rc
LOCAL_CPPFLAGS += -std=c++20 -fvisibility=hidden
LOCAL_CPPFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR
LOCAL_CPPFLAGS += -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES
LOCAL_CPPFLAGS += -DIMGUI_DISABLE_DEBUG_TOOLS
LOCAL_CFLAGS += -O3 -ffast-math -fomit-frame-pointer
LOCAL_CFLAGS += -fno-stack-protector -fvisibility=hidden
LOCAL_CFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
LOCAL_CFLAGS += -fno-rtti -fno-exceptions
LOCAL_CFLAGS += -frandom-seed=build_id -fno-ident -fno-builtin
LOCAL_CFLAGS += -march=armv8-a+crc+crypto -mtune=cortex-a75
LOCAL_CFLAGS += -D_FORTIFY_SOURCE=2
LOCAL_LDFLAGS += -O3 -Wl,--gc-sections -Wl,--exclude-libs,ALL
LOCAL_LDFLAGS += -Wl,-Bsymbolic -Wl,--build-id=none
LOCAL_LDFLAGS += -Wl,--hash-style=gnu
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_draw
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_Graphics
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_my_imgui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_touch
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/My_Utils
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/My_Utils/stb_image
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/backends
LOCAL_SRC_FILES := src/main.cpp
LOCAL_SRC_FILES += src/Android_draw/draw_Gui.cpp
LOCAL_SRC_FILES += src/Android_touch/TouchHelperA.cpp
LOCAL_SRC_FILES += src/Android_Graphics/GraphicsManager.cpp
LOCAL_SRC_FILES += src/Android_Graphics/OpenGLGraphics.cpp
LOCAL_SRC_FILES += src/Android_Graphics/VulkanGraphics.cpp
LOCAL_SRC_FILES += src/Android_Graphics/vulkan_wrapper.cpp
LOCAL_SRC_FILES += src/Android_my_imgui/AndroidImgui.cpp
LOCAL_SRC_FILES += src/Android_my_imgui/my_imgui.cpp
LOCAL_SRC_FILES += src/Android_my_imgui/my_imgui_impl_android.cpp
LOCAL_SRC_FILES += src/ImGui/imgui.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_demo.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_draw.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_tables.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_widgets.cpp
LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_android.cpp
LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_opengl3.cpp
LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_vulkan.cpp
LOCAL_SRC_FILES += src/My_Utils/stb_image.cpp
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -lz
include $(BUILD_EXECUTABLE)