plugins {
    id("com.android.application")
}

android {
    namespace = "com.example.imgui"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.example.imgui"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        externalNativeBuild {
            cmake {
                cppFlags("-std=c++17", "-frtti", "-fexceptions")
                arguments("-DANDROID_STL=c++_static")
            }
        }
        ndk {
            abiFilters += "arm64-v8a"   // 64位手机用这个，如果是老32位手机再加 "armeabi-v7a"
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
}

dependencies {
    // 无依赖
}