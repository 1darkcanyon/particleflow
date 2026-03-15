// VulkanRenderer_JNI.cpp
// JNI Bridge: Connects Java (Android) to C++ (Vulkan Engine)
// This file translates Java method calls into C++ function calls

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include "VulkanParticleEngine.h"

#define LOG_TAG "VulkanJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ============================================================================
// JNI BRIDGE FUNCTIONS
// ============================================================================

extern "C" {

/**
 * Create a new VulkanParticleEngine instance
 * Java: nativeCreate()
 * Returns: Handle (pointer) to engine as long
 */
JNIEXPORT jlong JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeCreate(
    JNIEnv* env, jobject obj) {
    
    LOGI("Creating VulkanParticleEngine...");
    
    VulkanParticleEngine* engine = new VulkanParticleEngine();
    
    if (engine == nullptr) {
        LOGE("Failed to create VulkanParticleEngine!");
        return 0;
    }
    
    LOGI("VulkanParticleEngine created successfully");
    return reinterpret_cast<jlong>(engine);
}

/**
 * Destroy the VulkanParticleEngine instance
 * Java: nativeDestroy(long handle)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeDestroy(
    JNIEnv* env, jobject obj, jlong handle) {
    
    LOGI("Destroying VulkanParticleEngine...");
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine != nullptr) {
        delete engine;
        LOGI("VulkanParticleEngine destroyed");
    }
}

/**
 * Initialize Vulkan with Android surface
 * Java: nativeInitialize(long handle, Surface surface)
 * Returns: true if successful, false if failed
 */
JNIEXPORT jboolean JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeInitialize(
    JNIEnv* env, jobject obj, jlong handle, jobject surface) {
    
    LOGI("Initializing Vulkan...");
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return JNI_FALSE;
    }
    
    // Convert Java Surface to ANativeWindow
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        LOGE("Failed to get ANativeWindow from Surface!");
        return JNI_FALSE;
    }
    
    // Initialize Vulkan
    bool result = engine->initialize(window);
    
    // Release the window (engine has created its own surface)
    ANativeWindow_release(window);
    
    if (result) {
        LOGI("Vulkan initialized successfully");
        return JNI_TRUE;
    } else {
        LOGE("Vulkan initialization failed!");
        return JNI_FALSE;
    }
}

/**
 * Set all particle engine parameters
 * Java: nativeSetParameters(long handle, float slowH, float slowS, ...)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeSetParameters(
    JNIEnv* env, jobject obj, jlong handle,
    jfloat slowH, jfloat slowS, jfloat slowV,
    jfloat fastH, jfloat fastS, jfloat fastV,
    jint hueDir, jfloat attract, jfloat drag,
    jfloat w, jfloat h, jint numPart, jint numTouch,
    jint partSize, jint bgColor) {
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return;
    }
    
    // Build parameter structure
    ParticleParams params;
    params.slowH = slowH;
    params.slowS = slowS;
    params.slowV = slowV;
    params.fastH = fastH;
    params.fastS = fastS;
    params.fastV = fastV;
    params.hueDir = hueDir;
    params.attract = attract;
    params.drag = drag;
    params.width = w;
    params.height = h;
    params.numPart = (uint32_t)numPart;
    params.numTouch = (uint32_t)numTouch;
    params.partSize = (uint32_t)partSize;
    params.bgColor = (uint32_t)bgColor;
    
    // Set parameters
    engine->setParameters(params.slowH, params.slowS, params.slowV, params.fastH, params.fastS, params.fastV, params.hueDir, params.attract, params.drag, params.width, params.height, params.numPart, params.numTouch, params.partSize, params.bgColor);
    
    LOGI("Parameters set: %d particles, %d touch points, attraction=%.1f, drag=%.3f",
         numPart, numTouch, attract, drag);
}

/**
 * Initialize particles (run init compute shader)
 * Java: nativeInitParticles(long handle)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeInitParticles(
    JNIEnv* env, jobject obj, jlong handle) {
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return;
    }
    
    engine->initParticles();
}

/**
 * Update particle physics (run update compute shader)
 * Java: nativeUpdateParticles(long handle)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeUpdateParticles(
    JNIEnv* env, jobject obj, jlong handle) {
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return;
    }
    
    engine->updateParticles();
}

/**
 * Set a touch point position
 * Java: nativeSetTouch(long handle, int index, float x, float y)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeSetTouch(
    JNIEnv* env, jobject obj, jlong handle, jint index, jfloat x, jfloat y) {
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return;
    }
    
    engine->setTouch((uint32_t)index, x, y);
}

/**
 * Render one frame
 * Java: nativeRender(long handle)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeRender(
    JNIEnv* env, jobject obj, jlong handle) {
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return;
    }
    
    engine->render();
}

/**
 * Handle surface size changes
 * Java: nativeOnSurfaceChanged(long handle, int width, int height)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeOnSurfaceChanged(
    JNIEnv* env, jobject obj, jlong handle, jint width, jint height) {
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return;
    }
    
    LOGI("Surface changed: %dx%d", width, height);
    engine->onSurfaceChanged(width, height);
}

/**
 * BONUS: Initialize Tesla 369 pattern
 * Java: nativeInitTesla369Pattern(long handle)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeInitTesla369Pattern(
    JNIEnv* env, jobject obj, jlong handle) {
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return;
    }
    
    LOGI("⚡ Initializing Tesla 369 Universe Pattern...");
    
    // Set 9 touch points in sacred geometry pattern
    // 3 outer (triangle) + 6 inner (hexagon) = 9 total
    
    // Get surface dimensions from engine
    // For now, assume centered pattern
    // You'll want to add getters to VulkanParticleEngine for width/height
    
    // This is a placeholder - implement Tesla369 pattern in engine
    LOGI("✨ Tesla 369 Pattern would be initialized here");
    LOGI("   (Add initTesla369Pattern() method to VulkanParticleEngine)");
}

/**
 * Sync touch buffer to GPU
 * Java: nativeSyncTouch(long handle)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeSyncTouch(
    JNIEnv* env, jobject obj, jlong handle) {
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return;
    }
    
    // Touch buffer is automatically synced in updateParticles()
    // But you can add explicit sync if needed
    // engine->syncTouchBuffer();
}

/**
 * Reset all attraction points
 * Java: nativeResetAttractionPoints(long handle)
 */
JNIEXPORT void JNICALL
Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeResetAttractionPoints(
    JNIEnv* env, jobject obj, jlong handle) {
    
    VulkanParticleEngine* engine = reinterpret_cast<VulkanParticleEngine*>(handle);
    if (engine == nullptr) {
        LOGE("Invalid engine handle!");
        return;
    }
    
    // Reset all touch points to disabled (-1, -1)
    // You'll need to get numTouchPoints from engine
    // For now, assume max of 16
    for (uint32_t i = 0; i < 16; i++) {
        engine->setTouch(i, -1.0f, -1.0f);
    }
    
    LOGI("All attraction points reset");
}

} // extern "C"

// ============================================================================
// JNI METHOD REGISTRATION (Optional - for performance)
// ============================================================================

/*
// If you want to register methods manually (faster lookup):

static JNINativeMethod methods[] = {
    {"nativeCreate", "()J", (void*)Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeCreate},
    {"nativeDestroy", "(J)V", (void*)Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeDestroy},
    {"nativeInitialize", "(JLandroid/view/Surface;)Z", (void*)Java_com_nfaralli_particleflow_VulkanParticlesRenderer_nativeInitialize},
    // ... add all methods here
};

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    
    jclass clazz = env->FindClass("com/nfaralli/particleflow/VulkanParticlesRenderer");
    if (clazz == nullptr) {
        return JNI_ERR;
    }
    
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) < 0) {
        return JNI_ERR;
    }
    
    return JNI_VERSION_1_6;
}
*/

// ============================================================================
// NOTES FOR TERMUX
// ============================================================================

/*
TO CREATE THIS FILE IN TERMUX:

nano app/src/main/cpp/VulkanRenderer_JNI.cpp

Then paste this entire file and save with Ctrl+O, Enter, Ctrl+X

This file bridges:
Java (VulkanParticlesRenderer.java) 
  ↓
JNI (this file)
  ↓
C++ (VulkanParticleEngine.cpp)

Every Java native method MUST have a matching JNI function here!
*/
