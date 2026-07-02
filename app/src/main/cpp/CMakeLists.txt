cmake_minimum_required(VERSION 3.10)
project(ImGuiOverlay)
# 默认用 Release 模式，优化体积和速度
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -fvisibility=hidden -flto")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG -fvisibility=hidden -flto")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动查找当前目录及子目录下所有 .cpp 和 .c 文件
file(GLOB_RECURSE ALL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

# 移除不必要的 Demo 文件，减小体积
list(REMOVE_ITEM ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/ImGui/imgui_demo.cpp")
add_executable(overlay_user ${ALL_SOURCES})


# 头文件搜索路径
target_include_directories(overlay_user PRIVATE
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
target_include_directories(overlay_user PRIVATE ${ALL_INCLUDE_DIRS})

SUBDIRLIST(INCLUDE_SUBDIRS "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(overlay_user PRIVATE ${INCLUDE_SUBDIRS})

SUBDIRLIST(IMGUI_SUBDIRS "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
target_include_directories(overlay_user PRIVATE ${IMGUI_SUBDIRS})

# 关键：定义 VK_NO_PROTOTYPES，避免与 NDK 自带的 Vulkan 函数原型冲突
target_compile_definitions(overlay_user PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID_KHR IMGUI_DISABLE_DEBUG_TOOLS)
# 链接系统库
find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(glesv3-lib GLESv3)
find_library(vulkan-lib vulkan)
find_library(dl-lib dl)

target_link_libraries(overlay_user
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${glesv3-lib}
        ${vulkan-lib}
        ${dl-lib}
)

set_target_properties(overlay_user PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)
# 编译后自动 strip，去除符号，减小体积
add_custom_command(TARGET overlay_user POST_BUILD
        COMMAND "${CMAKE_STRIP}" --strip-all "$<TARGET_FILE:overlay_user>"
)