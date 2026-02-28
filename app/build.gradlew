plugins {
    id 'com.android.application'
}

android {
    namespace 'com.nfaralli.particleflow'
    compileSdk 34
    
    defaultConfig {
        applicationId "com.nfaralli.particleflow"
        minSdk 24  // Vulkan requires API 24+
        targetSdk 34
        versionCode 20000
        versionName "2.0.0"
        
        ndk {
            // Target modern architectures
            abiFilters 'arm64-v8a', 'armeabi-v7a', 'x86_64'
        }
        
        externalNativeBuild {
            cmake {
                cppFlags "-std=c++17 -frtti -fexceptions -Wall"
                arguments "-DANDROID_STL=c++_shared"
            }
        }
    }
    
    buildTypes {
        release {
            minifyEnabled false
            proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'), 'proguard-rules.pro'
        }
        debug {
            debuggable true
            jniDebuggable true
        }
    }
    
    externalNativeBuild {
        cmake {
            path file('src/main/cpp/CMakeLists.txt')
            version '3.22.1'
        }
    }
    
    compileOptions {
        sourceCompatibility JavaVersion.VERSION_1_8
        targetCompatibility JavaVersion.VERSION_1_8
    }
    
    buildFeatures {
        viewBinding false
    }
    
    packagingOptions {
        // Exclude duplicate files
        pickFirst 'lib/*/libc++_shared.so'
    }
}

dependencies {
    implementation 'androidx.appcompat:appcompat:1.6.1'
    implementation 'com.google.android.material:material:1.9.0'
}
