# ============================================================================
# Android 端：编译成 libaxtquiz.so（清单里的 android.app.lib_name 要与此一致）
# ============================================================================

if(NOT ANDROID_NDK)
    set(ANDROID_NDK ${CMAKE_ANDROID_NDK})
endif()
if(NOT ANDROID_NDK)
    message(FATAL_ERROR "找不到 Android NDK 路径（ANDROID_NDK 未定义）")
endif()

# NDK 自带的“胶水层”：把 Java 的 NativeActivity 与我们的 android_main() 连起来。
#
# ⚠ 这个 .c 必须**直接编进 libaxtquiz.so**，不能做成静态库再链接：
#   ANativeActivity_onCreate 是系统在启动时用 dlsym 找的，编译期没有任何代码引用它，
#   链接器因此不会把静态库里的那个目标文件拉进来 → 生成的 .so 里没有这个符号 →
#   表现是「APK 装得上、一点开就崩」：
#     java.lang.UnsatisfiedLinkError: undefined symbol: ANativeActivity_onCreate
#   （这个坑真实发生过：Android 端一直没真正跑起来过。）
add_library(axtquiz SHARED
    AndroidMain.cpp
    PlatformAndroid.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

target_include_directories(axtquiz PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(axtquiz PRIVATE
    quiz_ui        # 界面（会带上 core 与 render）
    android
    log
)
