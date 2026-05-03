/**
 * reshade_core.cpp — 注入库主入口
 *
 * 当被 Zygisk 注入到目标进程后，此函数被调用：
 *   1. 安装 GL/Vk Hook
 *   2. 启动 IPC 监听线程（接收 UI 参数更新）
 */

#include <android/log.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <string>

#include "hook/gl_hook.h"
#include "pipeline/effect_chain.h"

#define LOG_TAG "ReShadeCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─────────────────────────────────────────────────────────
// IPC 协议（与 Kotlin UI 通信）
// ─────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct IpcMessage {
    uint8_t  type;       // 0=params, 1=loadShader, 2=loadLUT, 3=enable/disable
    union {
        struct {         // type=0: 参数更新
            float brightness;
            float contrast;
            float saturation;
            float sharpness;
            float vignette;
            float gamma;
            float lutStrength;
        } params;
        struct {         // type=1/2: 文件加载
            char path[256];
        } file;
        struct {         // type=3
            uint8_t enabled;
        } toggle;
    };
};
#pragma pack(pop)

// ─────────────────────────────────────────────────────────
// IPC 监听线程
// ─────────────────────────────────────────────────────────
static std::string g_packageName;

static void* ipcThreadFunc(void*) {
    // socket 路径按包名区分，避免冲突
    std::string sockPath = "/data/local/tmp/reshade_" + g_packageName + ".sock";

    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) { LOGE("socket() failed"); return nullptr; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path)-1);
    unlink(sockPath.c_str());

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("bind() failed: %s", strerror(errno));
        close(serverFd);
        return nullptr;
    }
    chmod(sockPath.c_str(), 0666);
    listen(serverFd, 4);
    LOGI("IPC server listening: %s", sockPath.c_str());

    while (true) {
        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) continue;

        IpcMessage msg{};
        ssize_t n = recv(clientFd, &msg, sizeof(msg), 0);
        close(clientFd);
        if (n <= 0) continue;

        switch (msg.type) {
            case 0: {  // 参数更新
                EffectParams p;
                p.brightness  = msg.params.brightness;
                p.contrast    = msg.params.contrast;
                p.saturation  = msg.params.saturation;
                p.sharpness   = msg.params.sharpness;
                p.vignette    = msg.params.vignette;
                p.gamma       = msg.params.gamma;
                p.lutStrength = msg.params.lutStrength;
                updateEffectParams(p);
                LOGI("Params updated: brightness=%.2f contrast=%.2f",
                     p.brightness, p.contrast);
                break;
            }
            case 1:  // 加载 shader
                loadShaderFile(msg.file.path);
                break;
            case 2:  // 加载 LUT
                loadLUTFile(msg.file.path);
                break;
            case 3:  // 开关
                setGLHookEnabled(msg.toggle.enabled != 0);
                LOGI("ReShade %s", msg.toggle.enabled ? "enabled" : "disabled");
                break;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────
// 注入初始化入口（Zygisk 调用）
// ─────────────────────────────────────────────────────────
extern "C" __attribute__((visibility("default")))
void reshade_inject_init(const char* packageName) {
    g_packageName = packageName ? packageName : "unknown";
    LOGI("ReShade injected into: %s", g_packageName.c_str());

    // 安装 OpenGL ES Hook
    installGLHook();

    // 启动 IPC 监听线程
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, ipcThreadFunc, nullptr);
    pthread_attr_destroy(&attr);

    LOGI("IPC thread started");
}
