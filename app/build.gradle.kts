// ============================================================================
// :app —— 把 C++ 的 libaxtquiz.so 打包成 APK
// ============================================================================

plugins {
    alias(libs.plugins.android.application)
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
        versionCode = 7
        versionName = "0.2.2"

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

    /** 题库与语言包在项目根目录的 assets/，两端共用同一份 */
    sourceSets {
        getByName("main") {
            assets.srcDir(file("../assets"))
        }
    }

    /**
     * assets/ 是整目录拷的，里面混着几百个 .bak-* 备份文件，不该打进 APK。
     * 这一条是在 AGP 默认忽略规则之外再补一条：忽略所有 *.bak*。
     */
    androidResources {
        ignoreAssetsPatterns.add("*.bak*")
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
