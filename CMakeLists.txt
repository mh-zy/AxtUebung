# ============================================================================
# Axt 刷题 —— 一套 C++ 代码，Windows 与 Android 双端
# ----------------------------------------------------------------------------
# 目录职责：
#   core/      平台无关的业务逻辑（题库解析、答题会话、进度、语言包）
#   ui/        界面（基于 Dear ImGui，只依赖 core 与 render）
#   render/    ImGui 的软件渲染后端（不用 OpenGL，安卓端因此不需要 Java）
#   platform/  平台层（Windows：Win32 + GDI；Android：NativeActivity + ANativeWindow）
#   app/       Android 打包壳（Gradle + AndroidManifest.xml）
# ============================================================================

cmake_minimum_required(VERSION 3.22)

# 需要 C 语言：Android NDK 自带的 android_native_app_glue 是 C 文件
project(AxtQuiz LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
endif()

if(MSVC)
    # /utf-8 必须加：源码里有大量中文注释与中文字符串
    add_compile_options(/utf-8 /W4 /permissive- /wd4996)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---- Dear ImGui（第三方，源码在 third_party/imgui）----
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})
target_compile_features(imgui PUBLIC cxx_std_17)

add_subdirectory(core)
add_subdirectory(render)
add_subdirectory(ui)

if(ANDROID)
    message(STATUS "构建目标：Android（NDK + Gradle）")
    add_subdirectory(platform/android)
else()
    message(STATUS "构建目标：Windows 桌面端")
    add_subdirectory(platform/windows)
    enable_testing()
    add_subdirectory(tests)
endif()
