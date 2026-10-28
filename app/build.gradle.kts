// ============================================================================
// :app —— 把 C++ 的 libaxtquiz.so 打包成 APK
// ============================================================================

plugins {
    alias(libs.plugins.android.application)
}

/**
 * Android 端的 assets 不能直接指向源码目录：里面有大量 *.bak-*、旧图标和
 * 设计源文件，AAPT 的 ignoreAssetsPatterns 在当前 AGP 版本里没有过滤掉它们。
 * 这里先 Sync 一个干净目录，再让 AGP 只打包这个目录。
 */
val filteredAssetsDir = layout.buildDirectory.dir("generated/filteredAssets")
val syncFilteredAssets = tasks.register<Sync>("syncFilteredAssets") {
    from(rootProject.file("assets")) {
        exclude(
            "**/*.bak*",
            "**/chevron-left.svg",
            "**/move-right.svg",
            "**/play-track-next.svg",
            "**/redo.svg",
            "**/software-download.svg",
            "**/software-upload.svg",
            "**/CN_title.svg",
            "**/LxgwMarkerGothic-v1.004/**"
        )
    }
    into(filteredAssetsDir)
}

android {
    namespace = "com.axt.quiz"
    compileSdk = 37

    /** 指定 NDK 版本，避免 AGP 再去下载别的版本 */
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "com.axt.quiz"
        minSdk = 24
        targetSdk = 37
        versionCode = 14
        versionName = "1.0.0"

        // x86_64 给模拟器，arm64-v8a 给真机
        ndk {
            abiFilters += listOf("x86_64", "arm64-v8a")
        }
        externalNativeBuild {
            cmake {
                // C++ 标准库静态链接，APK 里少一个 libc++_shared.so
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    /** 关键：指向项目根目录的 CMakeLists.txt */
    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
        }
    }

    /**
     * 侧载用签名：release 也用调试密钥签（不用额外生成 keystore，手机上「下载即装」）。
     * 以后要上应用商店再换成自己的 keystore。
     */
    signingConfigs {
        create("sideload") {
            storeFile = file(System.getProperty("user.home") + "/.android/debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("sideload")
        }
    }
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(syncFilteredAssets) {
            it.destinationDirectory
        }
    }
}
