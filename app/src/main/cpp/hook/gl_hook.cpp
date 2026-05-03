/**
 * gl_hook.cpp — OpenGL ES eglSwapBuffers 劫持
 *
 * 使用 PLT/GOT hook 拦截 eglSwapBuffers，在每帧结束前
 * 执行后处理 Pass 链，然后再交换缓冲。
 *
 * 依赖：bhook (bytedance/bhook) 或 xhook (iqiyi/xHook)
 */

#include "gl_hook.h"
#include "../pipeline/effect_chain.h"
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <pthread.h>
#include <atomic>
#include <string>

#define LOG_TAG "ReShadeGL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─────────────────────────────────────────────────────────
// 原始函数指针
// ─────────────────────────────────────────────────────────
using PFN_eglSwapBuffers = EGLBoolean (*)(EGLDisplay, EGLSurface);
static PFN_eglSwapBuffers orig_eglSwapBuffers = nullptr;

// ─────────────────────────────────────────────────────────
// 全局状态
// ─────────────────────────────────────────────────────────
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_enabled{true};
static EffectChain*      g_chain = nullptr;
static pthread_mutex_t   g_mutex = PTHREAD_MUTEX_INITIALIZER;

// 上次已知的 surface 尺寸
static int g_width  = 0;
static int g_height = 0;

// ─────────────────────────────────────────────────────────
// 初始化 — 在首次 swap 时懒加载
// ─────────────────────────────────────────────────────────
static void initOnce(EGLDisplay display, EGLSurface surface) {
    if (g_initialized.load()) return;
    pthread_mutex_lock(&g_mutex);
    if (!g_initialized.load()) {
        // 获取 surface 尺寸
        eglQuerySurface(display, surface, EGL_WIDTH,  &g_width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &g_height);
        LOGI("Surface: %d x %d", g_width, g_height);

        // 创建效果链
        g_chain = new EffectChain(g_width, g_height);
        g_chain->loadDefaultEffects();

        g_initialized.store(true);
        LOGI("GL Hook initialized");
    }
    pthread_mutex_unlock(&g_mutex);
}

// ─────────────────────────────────────────────────────────
// Hook 实现：替换原始 eglSwapBuffers
// ─────────────────────────────────────────────────────────
EGLBoolean hook_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!g_enabled.load() || !orig_eglSwapBuffers) {
        return orig_eglSwapBuffers ? orig_eglSwapBuffers(display, surface) : EGL_FALSE;
    }

    initOnce(display, surface);

    // 检查 surface 尺寸变化（旋转/resize）
    int w = 0, h = 0;
    eglQuerySurface(display, surface, EGL_WIDTH,  &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);
    if (w != g_width || h != g_height) {
        g_width  = w;
        g_height = h;
        pthread_mutex_lock(&g_mutex);
        if (g_chain) g_chain->resize(w, h);
        pthread_mutex_unlock(&g_mutex);
        LOGI("Surface resized: %d x %d", w, h);
    }

    // ── 执行后处理链 ──────────────────────────────────────
    if (g_chain && g_initialized.load()) {
        // 保存 GL 状态
        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);

        // 执行所有 Pass
        pthread_mutex_lock(&g_mutex);
        g_chain->render(prevFBO);
        pthread_mutex_unlock(&g_mutex);

        // 恢复状态
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }

    return orig_eglSwapBuffers(display, surface);
}

// ─────────────────────────────────────────────────────────
// 安装 Hook（使用 inline hook 或 PLT hook）
// ─────────────────────────────────────────────────────────

// ── 方法 A：用 dlsym 获取原始地址后用 bhook 替换 ──────
// #include "bhook.h"
// void installGLHook() {
//     bh_init(BH_MODE_FULL, true);
//     bh_hook(nullptr, "libEGL.so", "eglSwapBuffers",
//             (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers, nullptr, 0);
// }

// ── 方法 B：手动 PLT hook（无外部依赖版本）─────────────
#include <sys/mman.h>
#include <elf.h>
#include <link.h>

struct HookEntry {
    const char* symbol;
    void*       hook_func;
    void**      orig_func;
};

static bool hookPLT(const char* libName, HookEntry* entries, int count) {
    // 通过 /proc/self/maps 找到目标 so 的基地址
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return false;

    uintptr_t baseAddr = 0;
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, libName)) {
            sscanf(line, "%lx-", &baseAddr);
            break;
        }
    }
    fclose(maps);
    if (!baseAddr) return false;

    // 解析 ELF 头 → PLT/GOT → 替换函数指针
    auto* ehdr = reinterpret_cast<ElfW(Ehdr)*>(baseAddr);
    auto* phdr = reinterpret_cast<ElfW(Phdr)*>(baseAddr + ehdr->e_phoff);

    uintptr_t dynOffset = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dynOffset = phdr[i].p_vaddr;
            break;
        }
    }
    if (!dynOffset) return false;

    auto* dyn  = reinterpret_cast<ElfW(Dyn)*>(baseAddr + dynOffset);
    ElfW(Rela)* relaTable = nullptr;
    size_t relaCount = 0;
    const char* strTab  = nullptr;
    ElfW(Sym)*  symTab  = nullptr;

    for (; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
            case DT_JMPREL: relaTable = reinterpret_cast<ElfW(Rela)*>(baseAddr + dyn->d_un.d_ptr); break;
            case DT_PLTRELSZ: relaCount = dyn->d_un.d_val / sizeof(ElfW(Rela)); break;
            case DT_STRTAB: strTab = reinterpret_cast<const char*>(baseAddr + dyn->d_un.d_ptr); break;
            case DT_SYMTAB: symTab = reinterpret_cast<ElfW(Sym)*>(baseAddr + dyn->d_un.d_ptr); break;
            default: break;
        }
    }
    if (!relaTable || !strTab || !symTab) return false;

    for (size_t i = 0; i < relaCount; i++) {
        uint32_t symIdx = ELF64_R_SYM(relaTable[i].r_info);
        const char* symName = strTab + symTab[symIdx].st_name;

        for (int j = 0; j < count; j++) {
            if (strcmp(symName, entries[j].symbol) == 0) {
                void** gotEntry = reinterpret_cast<void**>(baseAddr + relaTable[i].r_offset);

                // 修改内存页为可写
                uintptr_t page = (uintptr_t)gotEntry & ~(getpagesize() - 1);
                mprotect((void*)page, getpagesize(), PROT_READ | PROT_WRITE);

                // 保存原始地址
                if (entries[j].orig_func)
                    *entries[j].orig_func = *gotEntry;

                // 写入 hook 地址
                *gotEntry = entries[j].hook_func;

                // 恢复页保护
                mprotect((void*)page, getpagesize(), PROT_READ);

                LOGI("Hooked %s @ %p → %p", symName, *entries[j].orig_func, entries[j].hook_func);
            }
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────
// 公开接口
// ─────────────────────────────────────────────────────────
void installGLHook() {
    // 先尝试从 libEGL.so 获取原始函数（fallback）
    void* eglLib = dlopen("libEGL.so", RTLD_NOLOAD | RTLD_NOW);
    if (eglLib) {
        orig_eglSwapBuffers = (PFN_eglSwapBuffers)dlsym(eglLib, "eglSwapBuffers");
        dlclose(eglLib);
    }

    // 对目标进程加载的所有相关库执行 PLT hook
    HookEntry entries[] = {
        { "eglSwapBuffers", (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers }
    };

    // 常见游戏引擎 so 名
    const char* gameLibs[] = {
        "libunity.so",
        "libcocos2djs.so",
        "libgame.so",
        "libUE4.so",
        nullptr
    };

    for (int i = 0; gameLibs[i]; i++) {
        if (hookPLT(gameLibs[i], entries, 1)) {
            LOGI("PLT hook installed in %s", gameLibs[i]);
        }
    }
}

void setGLHookEnabled(bool enabled) {
    g_enabled.store(enabled);
}

void updateEffectParams(const EffectParams& params) {
    pthread_mutex_lock(&g_mutex);
    if (g_chain) g_chain->setParams(params);
    pthread_mutex_unlock(&g_mutex);
}

void loadShaderFile(const char* path) {
    pthread_mutex_lock(&g_mutex);
    if (g_chain) g_chain->loadShader(path);
    pthread_mutex_unlock(&g_mutex);
}

void loadLUTFile(const char* path) {
    pthread_mutex_lock(&g_mutex);
    if (g_chain) g_chain->loadLUT(path);
    pthread_mutex_unlock(&g_mutex);
}
