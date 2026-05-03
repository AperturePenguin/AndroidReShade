/**
 * gpu_hook_bhook.h
 * GPU Hook 增强版（bhook）公共接口
 */

#pragma once
#include <EGL/egl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Draw call 类型
typedef enum {
    DRAW_ARRAYS   = 0,
    DRAW_ELEMENTS = 1
} DrawCallType;

// 帧回调：在 eglSwapBuffers 前触发，可在此执行后处理
typedef void (*FrameCallback)(EGLDisplay display, EGLSurface surface, void* userdata);

// Draw call 回调：在每次 glDraw* 时触发
typedef void (*DrawCallback)(DrawCallType type, int mode, int count, void* userdata);

/**
 * 安装 GPU Hook
 * @param onFrame   每帧回调（eglSwapBuffers 前）
 * @param onDraw    每次 draw call 回调（可为 nullptr）
 * @param userdata  透传指针
 * @return true = 安装成功
 */
bool GpuHookBhook_Install(FrameCallback onFrame, DrawCallback onDraw, void* userdata);

/** 卸载所有 hook */
void GpuHookBhook_Uninstall();

/** 运行时启用/禁用（不卸载 hook，只跳过回调）*/
void GpuHookBhook_SetEnabled(bool enabled);

/** 是否已安装 */
bool GpuHookBhook_IsHooked();

#ifdef __cplusplus
}
#endif
