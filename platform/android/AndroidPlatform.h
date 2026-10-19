#pragma once

#include <android/asset_manager.h>
#include <android/native_activity.h>

// ============================================================================
// Android 平台内部接口（只在本目录使用）
// ============================================================================
namespace quiz::platform::android {

/// 由 AndroidMain.cpp 在启动时调用，把 AssetManager 与内部存储路径交给平台层
void setAndroidContext(AAssetManager* assetManager, ANativeActivity* activity);

/// 请求退出应用（Android 上调用 ANativeActivity_finish）
void requestExit();

/// 跟随 ImGui 的输入焦点：拉起 / 收起系统软键盘（want = ImGui 想要文字输入）
void syncSoftKeyboard(bool wantTextInput);

}  // namespace quiz::platform::android
