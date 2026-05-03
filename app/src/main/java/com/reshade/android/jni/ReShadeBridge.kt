/**
 * ReShadeBridge.kt
 * Kotlin 侧 JNI 桥接对象
 *
 * 封装所有 native 方法，提供 Kotlin-friendly API。
 * 对应 C++ 实现：reshade_jni.cpp
 */

package com.reshade.android.jni

import android.content.Context
import android.util.Log

object ReShadeBridge {

    private const val TAG = "ReShadeBridge"

    // -------------------------------------------------------
    // 加载 native 库
    // -------------------------------------------------------
    init {
        try {
            System.loadLibrary("reshade_jni")
            Log.i(TAG, "libreshade_jni.so loaded")
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Failed to load libreshade_jni.so: ${e.message}")
        }
    }

    // -------------------------------------------------------
    // Native 方法声明
    // -------------------------------------------------------
    private external fun nativeInit(configDir: String): Boolean
    private external fun nativeDestroy()
    private external fun nativeInstallHook(): Boolean
    private external fun nativeUninstallHook()
    private external fun nativeLoadShader(path: String): Long
    private external fun nativeRemoveShader(effectId: Long): Boolean
    private external fun nativeSetShaderEnabled(effectId: Long, enabled: Boolean)
    private external fun nativeLoadLUT(path: String): Int
    private external fun nativeSetFloatParam(effectId: Long, name: String, value: Float)
    private external fun nativeSetIntParam(effectId: Long, name: String, value: Int)
    private external fun nativeSetVec4Param(effectId: Long, name: String, x: Float, y: Float, z: Float, w: Float)
    private external fun nativeSetEnabled(enabled: Boolean)
    private external fun nativeIsHooked(): Boolean
    private external fun nativeSavePreset(name: String): Boolean
    private external fun nativeLoadPreset(name: String): Boolean
    private external fun nativeListPresets(): Array<String>
    private external fun nativeGetVersion(): String
    private external fun nativeGetStatus(): String

    // -------------------------------------------------------
    // 生命周期
    // -------------------------------------------------------
    fun init(context: Context): Boolean {
        val dir = context.getExternalFilesDir(null)?.absolutePath
            ?: "/sdcard/ReShade"
        return nativeInit(dir)
    }

    fun destroy() = nativeDestroy()

    // -------------------------------------------------------
    // Hook 控制
    // -------------------------------------------------------
    fun installHook(): Boolean {
        val ok = nativeInstallHook()
        Log.i(TAG, "installHook: $ok")
        return ok
    }

    fun uninstallHook() {
        nativeUninstallHook()
        Log.i(TAG, "uninstallHook done")
    }

    val isHooked: Boolean get() = nativeIsHooked()

    // -------------------------------------------------------
    // Shader 管理
    // -------------------------------------------------------
    /**
     * 加载 .fx / .hlsl 文件
     * @return effectId（> 0 成功，0 失败）
     */
    fun loadShader(path: String): Long = nativeLoadShader(path)

    fun removeShader(effectId: Long) = nativeRemoveShader(effectId)

    fun setShaderEnabled(effectId: Long, enabled: Boolean) =
        nativeSetShaderEnabled(effectId, enabled)

    // -------------------------------------------------------
    // LUT 管理
    // -------------------------------------------------------
    /**
     * 加载 .cube / .png LUT
     * @return GL texture ID（0 失败）
     */
    fun loadLUT(path: String): Int = nativeLoadLUT(path)

    // -------------------------------------------------------
    // 参数控制
    // -------------------------------------------------------
    fun setFloat(effectId: Long, name: String, value: Float) =
        nativeSetFloatParam(effectId, name, value)

    fun setInt(effectId: Long, name: String, value: Int) =
        nativeSetIntParam(effectId, name, value)

    fun setVec4(effectId: Long, name: String, x: Float, y: Float, z: Float, w: Float) =
        nativeSetVec4Param(effectId, name, x, y, z, w)

    // -------------------------------------------------------
    // 全局开关
    // -------------------------------------------------------
    var enabled: Boolean
        get() = isHooked
        set(value) { nativeSetEnabled(value) }

    // -------------------------------------------------------
    // Preset 管理
    // -------------------------------------------------------
    fun savePreset(name: String): Boolean = nativeSavePreset(name)
    fun loadPreset(name: String): Boolean = nativeLoadPreset(name)
    fun listPresets(): List<String>        = nativeListPresets().toList()

    // -------------------------------------------------------
    // 诊断
    // -------------------------------------------------------
    val version: String get() = nativeGetVersion()
    val status:  String get() = nativeGetStatus()
}
