cmake_minimum_required(VERSION 3.10)
project(ImGuiOverlay)

# ========================================
# ★ 版本号硬编码 — 每次发版前必须更新这 4 行！
# ========================================
set(OVERLAY_VERSION "v2.47-stable")
set(OVERLAY_VERSION_CODE 247)
set(OVERLAY_VERSION_STR "v2.47")

# 默认用 Release 模式，优化体积和速度
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 安全加固编译标志
# 基础: 优化 + 符号剥离 + 死代码消除
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -fvisibility=hidden -flto -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_RELEASE   "-O2 -DNDEBUG -fvisibility=hidden -flto -ffunction-sections -fdata-sections")

# 二进制加固: 立即绑定 + RELRO + 栈保护
set(SECURITY_FLAGS "-Wl,-z,relro -Wl,-z,now -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${SECURITY_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE} ${SECURITY_FLAGS}")

# 链接: 剥离符号表 + 移除未引用段 + 只读GOT
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-Wl,-s -Wl,--gc-sections -Wl,-z,relro -Wl,-z,now")

# ★ OLLVM 混淆 (可选 — 需先安装 OLLVM 工具链)
# 取消下面注释即可启用:
# set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mllvm -fla -mllvm -sub -mllvm -bcf -mllvm -split")
# set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE} -mllvm -fla -mllvm -sub -mllvm -bcf")

# ★ UPX 加壳 (可选 — 编译后执行: upx --best overlay)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动查找当前目录及子目录下所有 .cpp 和 .c 文件
file(GLOB_RECURSE ALL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

# 移除不必要的 Demo 文件，减小体积
list(REMOVE_ITEM ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/ImGui/imgui_demo.cpp")
# AutoAim.cpp 使用 include 模式（由 draw_Gui.cpp 末尾 #include），不独立编译
list(REMOVE_ITEM ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Android_draw/AutoAim.cpp")
add_executable(overlay ${ALL_SOURCES})


# 头文件搜索路径
target_include_directories(overlay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/stb
)

# 添加子目录搜索（之前的递归宏，如果不需要可以删除，但保留也没关系）
macro(SUBDIRLIST result curdir)
    file(GLOB children RELATIVE ${curdir} ${curdir}/*)
    foreach(child ${children})
        if(IS_DIRECTORY ${curdir}/${child})
            list(APPEND ${result} ${curdir}/${child})
            subdirlist(${result} ${curdir}/${child})
        endif()
    endforeach()
endmacro()

SUBDIRLIST(ALL_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(overlay PRIVATE ${ALL_INCLUDE_DIRS})

SUBDIRLIST(INCLUDE_SUBDIRS "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(overlay PRIVATE ${INCLUDE_SUBDIRS})

SUBDIRLIST(IMGUI_SUBDIRS "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
target_include_directories(overlay PRIVATE ${IMGUI_SUBDIRS})

# 关键：定义 VK_NO_PROTOTYPES，避免与 NDK 自带的 Vulkan 函数原型冲突
target_compile_definitions(overlay PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID_KHR IMGUI_DISABLE_DEBUG_TOOLS OVERLAY_VERSION="${OVERLAY_VERSION}" OVERLAY_VERSION_CODE=${OVERLAY_VERSION_CODE} OVERLAY_VERSION_STR="${OVERLAY_VERSION_STR}")
# 链接系统库
find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(glesv3-lib GLESv3)
find_library(vulkan-lib vulkan)
find_library(dl-lib dl)

target_link_libraries(overlay
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${glesv3-lib}
        ${vulkan-lib}
        ${dl-lib}
)

set_target_properties(overlay PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)
# 编译后自动 strip，去除符号，减小体积
add_custom_command(TARGET overlay POST_BUILD
        COMMAND "${CMAKE_STRIP}" --strip-all "$<TARGET_FILE:overlay>"
)

# ========================================
# ★ overlay_user (用户版: 含 AUTH_SERVER 卡密系统)
#    与 overlay 源码完全相同，仅多了 AUTH_SERVER 编译宏
# ========================================
add_executable(overlay_user ${ALL_SOURCES})
target_include_directories(overlay_user PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/stb
)
target_include_directories(overlay_user PRIVATE ${ALL_INCLUDE_DIRS})
target_include_directories(overlay_user PRIVATE ${INCLUDE_SUBDIRS})
target_include_directories(overlay_user PRIVATE ${IMGUI_SUBDIRS})
target_compile_definitions(overlay_user PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID_KHR IMGUI_DISABLE_DEBUG_TOOLS AUTH_SERVER OVERLAY_VERSION="${OVERLAY_VERSION}" OVERLAY_VERSION_CODE=${OVERLAY_VERSION_CODE} OVERLAY_VERSION_STR="${OVERLAY_VERSION_STR}")
target_link_libraries(overlay_user
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${glesv3-lib}
        ${vulkan-lib}
        ${dl-lib}
)
set_target_properties(overlay_user PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_custom_command(TARGET overlay_user POST_BUILD
        COMMAND "${CMAKE_STRIP}" --strip-all "$<TARGET_FILE:overlay_user>"
)