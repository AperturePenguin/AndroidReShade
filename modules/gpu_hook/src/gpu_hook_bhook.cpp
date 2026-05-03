/**
 * gpu_hook_bhook.cpp
 * GPU Hook 增强版 - 基于 bhook (ByteDance PLT Hook)
 *
 * 相比手动 ELF PLT/GOT 解析，bhook 有以下优势：
 *   - 支持 Android 5.0 ~ 14（API 21+）
 *   - 自动处理 ASLR / multiple .so 映射
 *   - 线程安全，hook/unhook 稳定
 *   - 自动处理 linker namespace 隔离问题
 *
 * 依赖：
 *   真实 bhook：https://github.com/bytedance/bhook
 *   本文件当前使用 bytehook_stub.h（编译通过，运行期 no-op）
 *   替换为真实 libbytehook.so 后即可生效。
 */

#include "gpu_hook_bhook.h"
#include "../third_party/bhook/include/bytehook_stub.h"
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <pthread.h>
#include <atomic>
#include <string>

#define TAG "AndroidReShade/BHook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// ============================================================
// 全局状态
// ============================================================
namespace {

struct HookState {
    // 原始函数指针
    using eglSwapBuffers_t = EGLBoolean (*)(EGLDisplay, EGLSurface);
    using glDrawArrays_t   = void       (*)(GLenum, GLint, GLsizei);
    using glDrawElements_t = void       (*)(GLenum, GLsizei, GLenum, const void*);

    eglSwapBuffers_t orig_eglSwapBuffers = nullptr;
    glDrawArrays_t   orig_glDrawArrays   = nullptr;
    glDrawElements_t orig_glDrawElements = nullptr;

    // bhook stubs（用于 unhook）
    bytehook_stub_t  stub_eglSwapBuffers = nullptr;
    bytehook_stub_t  stub_glDrawArrays   = nullptr;
    bytehook_stub_t  stub_glDrawElements = nullptr;

    // 帧回调
    FrameCallback    onFrame = nullptr;
    DrawCallback     onDraw  = nullptr;
    void*            userdata = nullptr;

    // 状态
    std::atomic<bool> hooked{false};
    std::atomic<bool> enabled{true};

    pthread_mutex_t  mutex = PTHREAD_MUTEX_INITIALIZER;
};

static HookState g_state;

} // namespace

// ============================================================
// Hook 回调：eglSwapBuffers
// ============================================================
static EGLBoolean hooked_eglSwapBuffers(EGLDisplay display, EGLSurface surface)
{
    if (!g_state.enabled.load(std::memory_order_relaxed)) {
        if (g_state.orig_eglSwapBuffers)
            return g_state.orig_eglSwapBuffers(display, surface);
        return EGL_FALSE;
    }

    // 调用帧回调（后处理）
    if (g_state.onFrame) {
        g_state.onFrame(display, surface, g_state.userdata);
    }

    // 调用原始函数
    if (g_state.orig_eglSwapBuffers) {
        return g_state.orig_eglSwapBuffers(display, surface);
    }

    // bhook 方式调用原始
    return BYTEHOOK_CALL_PREV(eglSwapBuffers, display, surface);
}

// ============================================================
// Hook 回调：glDrawArrays
// ============================================================
static void hooked_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (g_state.onDraw) {
        g_state.onDraw(DRAW_ARRAYS, mode, count, g_state.userdata);
    }
    if (g_state.orig_glDrawArrays) {
        g_state.orig_glDrawArrays(mode, first, count);
    }
    BYTEHOOK_POP_STACK();
}

// ============================================================
// Hook 回调：glDrawElements
// ============================================================
static void hooked_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices)
{
    if (g_state.onDraw) {
        g_state.onDraw(DRAW_ELEMENTS, mode, count, g_state.userdata);
    }
    if (g_state.orig_glDrawElements) {
        g_state.orig_glDrawElements(mode, count, type, indices);
    }
    BYTEHOOK_POP_STACK();
}

// ============================================================
// hooked 回调（bhook 安装完成通知）
// ============================================================
static void on_hooked(
    bytehook_stub_t stub, int result,
    const char*     callee_path, const char* sym_name,
    void* new_func, void* prev_func, void* arg)
{
    (void)stub; (void)arg; (void)new_func;
    if (result == BYTEHOOK_STATUS_CODE_OK) {
        LOGI("Hooked: %s!%s -> prev=%p", callee_path, sym_name, prev_func);
        // 保存原始函数指针
        if (sym_name) {
            std::string sym(sym_name);
            if (sym == "eglSwapBuffers")
                g_state.orig_eglSwapBuffers = reinterpret_cast<HookState::eglSwapBuffers_t>(prev_func);
            else if (sym == "glDrawArrays")
                g_state.orig_glDrawArrays = reinterpret_cast<HookState::glDrawArrays_t>(prev_func);
            else if (sym == "glDrawElements")
                g_state.orig_glDrawElements = reinterpret_cast<HookState::glDrawElements_t>(prev_func);
        }
    } else {
        LOGE("Hook FAILED: %s!%s (result=%d)", callee_path, sym_name, result);
    }
}

// ============================================================
// 公共 API
// ============================================================
bool GpuHookBhook_Install(FrameCallback onFrame, DrawCallback onDraw, void* userdata)
{
    if (g_state.hooked.load()) {
        LOGD("Already hooked, skipping");
        return true;
    }

    // 初始化 bhook
    int ret = bytehook_init(BYTEHOOK_TYPE_ELF, false);
    if (ret != BYTEHOOK_STATUS_CODE_OK) {
        LOGE("bytehook_init failed: %d", ret);
        return false;
    }

    g_state.onFrame   = onFrame;
    g_state.onDraw    = onDraw;
    g_state.userdata  = userdata;

    // hook eglSwapBuffers（在所有 so 中）
    g_state.stub_eglSwapBuffers = bytehook_hook_all(
        "libEGL.so",           // callee lib
        "eglSwapBuffers",      // symbol
        (void*)hooked_eglSwapBuffers,
        on_hooked,
        nullptr
    );

    // hook glDrawArrays（可选，用于 draw call 级别拦截）
    g_state.stub_glDrawArrays = bytehook_hook_all(
        "libGLESv3.so",
        "glDrawArrays",
        (void*)hooked_glDrawArrays,
        on_hooked,
        nullptr
    );

    // hook glDrawElements
    g_state.stub_glDrawElements = bytehook_hook_all(
        "libGLESv3.so",
        "glDrawElements",
        (void*)hooked_glDrawElements,
        on_hooked,
        nullptr
    );

    bool success = (g_state.stub_eglSwapBuffers != nullptr);
    if (success) {
        g_state.hooked.store(true);
        LOGI("GPU Hook (bhook) installed successfully");
    } else {
        LOGE("GPU Hook (bhook) installation failed");
    }
    return success;
}

void GpuHookBhook_Uninstall()
{
    if (!g_state.hooked.load()) return;

    if (g_state.stub_eglSwapBuffers) {
        bytehook_unhook(g_state.stub_eglSwapBuffers);
        g_state.stub_eglSwapBuffers = nullptr;
    }
    if (g_state.stub_glDrawArrays) {
        bytehook_unhook(g_state.stub_glDrawArrays);
        g_state.stub_glDrawArrays = nullptr;
    }
    if (g_state.stub_glDrawElements) {
        bytehook_unhook(g_state.stub_glDrawElements);
        g_state.stub_glDrawElements = nullptr;
    }

    g_state.hooked.store(false);
    g_state.onFrame  = nullptr;
    g_state.onDraw   = nullptr;
    g_state.userdata = nullptr;

    LOGI("GPU Hook (bhook) uninstalled");
}

void GpuHookBhook_SetEnabled(bool enabled)
{
    g_state.enabled.store(enabled, std::memory_order_relaxed);
    LOGI("GPU Hook enabled: %s", enabled ? "true" : "false");
}

bool GpuHookBhook_IsHooked()
{
    return g_state.hooked.load();
}
