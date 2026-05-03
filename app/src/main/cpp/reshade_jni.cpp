/**
 * reshade_jni.cpp
 * JNI Bridge - Kotlin ↔ C++ 接口层
 *
 * Kotlin 调用示例：
 *   ReShadeBridge.loadShader("/sdcard/ReShade/shaders/ColorCorrection.fx")
 *   ReShadeBridge.setParam("brightness", 0.2f)
 *   ReShadeBridge.setEnabled(true)
 */

#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>

// 模块头文件
#include "shader_loader.h"
#include "hlsl_converter.h"
#include "lut_loader.h"
#include "shader_runtime.h"
#include "config_system.h"
#include "gpu_hook.h"
#include "gpu_hook_bhook.h"

#define TAG    "AndroidReShade/JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// JNI 命名约定：Java_包名_类名_方法名
#define JNI_METHOD(rettype, name) \
    extern "C" JNIEXPORT rettype JNICALL \
    Java_com_reshade_android_jni_ReShadeBridge_##name

// ============================================================
// 全局 ShaderRuntime 实例
// ============================================================
static ShaderRuntime* g_runtime = nullptr;
static ConfigSystem*  g_config  = nullptr;

// ============================================================
// 初始化 / 销毁
// ============================================================
JNI_METHOD(jboolean, nativeInit)(JNIEnv* env, jclass, jstring configDir)
{
    const char* dir = env->GetStringUTFChars(configDir, nullptr);
    LOGI("nativeInit: configDir=%s", dir);

    if (!g_config)  g_config  = new ConfigSystem();
    if (!g_runtime) g_runtime = new ShaderRuntime();

    if (g_config && dir) {
        std::string preset = std::string(dir) + "/default.ini";
        g_config->load(preset);
    }

    env->ReleaseStringUTFChars(configDir, dir);
    return JNI_TRUE;
}

JNI_METHOD(void, nativeDestroy)(JNIEnv*, jclass)
{
    delete g_runtime; g_runtime = nullptr;
    delete g_config;  g_config  = nullptr;
    LOGI("nativeDestroy: released all resources");
}

// ============================================================
// Hook 安装 / 卸载
// ============================================================
JNI_METHOD(jboolean, nativeInstallHook)(JNIEnv*, jclass)
{
    bool ok = GpuHookBhook_Install(
        [](EGLDisplay disp, EGLSurface surf, void* ud) {
            (void)disp; (void)surf;
            if (g_runtime) g_runtime->onFrame(0, 0);
        },
        nullptr, nullptr
    );
    LOGI("nativeInstallHook: %s", ok ? "OK" : "FAILED");
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNI_METHOD(void, nativeUninstallHook)(JNIEnv*, jclass)
{
    GpuHookBhook_Uninstall();
    LOGI("nativeUninstallHook: done");
}

// ============================================================
// Shader 管理
// ============================================================
JNI_METHOD(jlong, nativeLoadShader)(JNIEnv* env, jclass, jstring path)
{
    const char* p = env->GetStringUTFChars(path, nullptr);
    LOGI("nativeLoadShader: %s", p);

    ShaderLoader loader;
    ShaderFile sf = loader.load(p);
    env->ReleaseStringUTFChars(path, p);

    if (!sf.valid) {
        LOGE("nativeLoadShader: failed to load shader");
        return 0LL;
    }

    if (!g_runtime) return 0LL;
    uint64_t id = g_runtime->addEffect(sf);
    LOGI("nativeLoadShader: effectId=%" PRIu64, id);
    return (jlong)id;
}

JNI_METHOD(jboolean, nativeRemoveShader)(JNIEnv*, jclass, jlong effectId)
{
    if (!g_runtime) return JNI_FALSE;
    g_runtime->removeEffect((uint64_t)effectId);
    return JNI_TRUE;
}

JNI_METHOD(void, nativeSetShaderEnabled)(JNIEnv*, jclass, jlong effectId, jboolean enabled)
{
    if (g_runtime) {
        g_runtime->setEffectEnabled((uint64_t)effectId, enabled == JNI_TRUE);
    }
}

// ============================================================
// LUT 管理
// ============================================================
JNI_METHOD(jint, nativeLoadLUT)(JNIEnv* env, jclass, jstring path)
{
    const char* p = env->GetStringUTFChars(path, nullptr);
    LOGI("nativeLoadLUT: %s", p);

    LutLoader loader;
    GLuint texId = loader.load(p);
    env->ReleaseStringUTFChars(path, p);

    if (texId == 0) {
        LOGE("nativeLoadLUT: failed");
        return 0;
    }
    return (jint)texId;
}

// ============================================================
// 参数控制
// ============================================================
JNI_METHOD(void, nativeSetFloatParam)(JNIEnv* env, jclass,
    jlong effectId, jstring name, jfloat value)
{
    if (!g_runtime) return;
    const char* n = env->GetStringUTFChars(name, nullptr);
    g_runtime->setUniformFloat((uint64_t)effectId, n, value);
    env->ReleaseStringUTFChars(name, n);
}

JNI_METHOD(void, nativeSetIntParam)(JNIEnv* env, jclass,
    jlong effectId, jstring name, jint value)
{
    if (!g_runtime) return;
    const char* n = env->GetStringUTFChars(name, nullptr);
    g_runtime->setUniformInt((uint64_t)effectId, n, value);
    env->ReleaseStringUTFChars(name, n);
}

JNI_METHOD(void, nativeSetVec4Param)(JNIEnv* env, jclass,
    jlong effectId, jstring name, jfloat x, jfloat y, jfloat z, jfloat w)
{
    if (!g_runtime) return;
    const char* n = env->GetStringUTFChars(name, nullptr);
    float v[4] = {x, y, z, w};
    g_runtime->setUniformVec4((uint64_t)effectId, n, v);
    env->ReleaseStringUTFChars(name, n);
}

// ============================================================
// 全局开关
// ============================================================
JNI_METHOD(void, nativeSetEnabled)(JNIEnv*, jclass, jboolean enabled)
{
    GpuHookBhook_SetEnabled(enabled == JNI_TRUE);
    if (g_runtime) g_runtime->setEnabled(enabled == JNI_TRUE);
    LOGI("nativeSetEnabled: %s", enabled ? "ON" : "OFF");
}

JNI_METHOD(jboolean, nativeIsHooked)(JNIEnv*, jclass)
{
    return GpuHookBhook_IsHooked() ? JNI_TRUE : JNI_FALSE;
}

// ============================================================
// 配置 / Preset
// ============================================================
JNI_METHOD(jboolean, nativeSavePreset)(JNIEnv* env, jclass, jstring name)
{
    if (!g_config || !g_runtime) return JNI_FALSE;
    const char* n = env->GetStringUTFChars(name, nullptr);
    bool ok = g_config->savePreset(n, g_runtime->getParams());
    env->ReleaseStringUTFChars(name, n);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNI_METHOD(jboolean, nativeLoadPreset)(JNIEnv* env, jclass, jstring name)
{
    if (!g_config || !g_runtime) return JNI_FALSE;
    const char* n = env->GetStringUTFChars(name, nullptr);
    auto params = g_config->loadPreset(n);
    env->ReleaseStringUTFChars(name, n);
    if (params.empty()) return JNI_FALSE;
    g_runtime->applyParams(params);
    return JNI_TRUE;
}

JNI_METHOD(jobjectArray, nativeListPresets)(JNIEnv* env, jclass)
{
    if (!g_config) return nullptr;
    auto names = g_config->listPresets();
    jobjectArray arr = env->NewObjectArray(
        (jsize)names.size(),
        env->FindClass("java/lang/String"),
        nullptr
    );
    for (int i = 0; i < (int)names.size(); i++) {
        env->SetObjectArrayElement(arr, i, env->NewStringUTF(names[i].c_str()));
    }
    return arr;
}

// ============================================================
// 诊断信息
// ============================================================
JNI_METHOD(jstring, nativeGetVersion)(JNIEnv* env, jclass)
{
    return env->NewStringUTF("AndroidReShade v1.0.0");
}

JNI_METHOD(jstring, nativeGetStatus)(JNIEnv* env, jclass)
{
    std::string status = "Hook: ";
    status += GpuHookBhook_IsHooked() ? "active" : "inactive";
    status += " | Effects: ";
    status += g_runtime ? std::to_string(g_runtime->effectCount()) : "0";
    return env->NewStringUTF(status.c_str());
}
