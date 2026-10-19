// 根构建脚本：只声明 Android Gradle Plugin（纯 C++ 工程不需要 Kotlin/Compose 插件）
plugins {
    alias(libs.plugins.android.application) apply false
}
