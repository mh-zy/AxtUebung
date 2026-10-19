// ============================================================================
// Gradle 设置：Android 端只负责把 C++ 编译出的 .so 打包成 APK
// （真正的代码在 core/、ui/、render/、platform/ 里，由根目录 CMakeLists.txt 组织）
// ============================================================================

pluginManagement {
    repositories {
        // 国内镜像优先，官方仓库兜底
        maven { url = uri("https://maven.aliyun.com/repository/public") }
        maven { url = uri("https://maven.aliyun.com/repository/gradle-plugin") }
        maven { url = uri("https://maven.aliyun.com/repository/google") }
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        maven { url = uri("https://maven.aliyun.com/repository/public") }
        maven { url = uri("https://maven.aliyun.com/repository/google") }
        google()
        mavenCentral()
    }
}

rootProject.name = "AxtQuiz"
include(":app")
