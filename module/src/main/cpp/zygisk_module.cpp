/**
 * AndroidReShade - Zygisk Injection Module
 *
 * 在目标游戏进程中注入 libreshade_core.so
 * 需要 Magisk 24.0+ (Zygisk API v4)
 */

#include "zygisk.hpp"
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>

#define LOG_TAG "ReShadeZygisk"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// ─────────────────────────────────────────────────────────
// 目标进程白名单（从配置文件读取）
// ─────────────────────────────────────────────────────────
static const char* CONFIG_PATH = "/data/local/tmp/reshade_targets.txt";
static const char* CORE_LIB    = "/data/local/tmp/libreshade_core.so";

static std::vector<std::string> loadTargetList() {
    std::vector<std::string> targets;
    std::ifstream f(CONFIG_PATH);
    if (!f.is_open()) return targets;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line[0] != '#')
            targets.push_back(line);
    }
    return targets;
}

// ─────────────────────────────────────────────────────────
// Zygisk Module 实现
// ─────────────────────────────────────────────────────────
class ReShadeModule : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        // 获取目标包名
        const char* rawPkg = env->GetStringUTFChars(args->nice_name, nullptr);
        if (rawPkg) {
            targetPkg = rawPkg;
            env->ReleaseStringUTFChars(args->nice_name, rawPkg);
        }

        // 检查是否在白名单内
        auto targets = loadTargetList();
        bool matched = false;
        for (auto& t : targets) {
            if (targetPkg.find(t) != std::string::npos) {
                matched = true;
                break;
            }
        }

        if (!matched) {
            // 不是目标进程，跳过
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        LOGI("Target process detected: %s", targetPkg.c_str());

        // 请求 companion 进程发送核心库的文件描述符
        int companionFd = api->connectCompanion();
        if (companionFd >= 0) {
            // 发送包名，companion 返回库文件 fd
            uint32_t len = targetPkg.size();
            write(companionFd, &len, sizeof(len));
            write(companionFd, targetPkg.c_str(), len);

            // 接收 fd
            int libFd = -1;
            read(companionFd, &libFd, sizeof(libFd));
            close(companionFd);

            if (libFd >= 0) {
                this->libFd = libFd;
                shouldInject = true;
            }
        }
    }

    void postAppSpecialize(const AppSpecializeArgs* args) override {
        if (!shouldInject) return;

        LOGI("Injecting libreshade_core.so into %s", targetPkg.c_str());

        // 从 fd 映射并加载 so
        char fdPath[64];
        snprintf(fdPath, sizeof(fdPath), "/proc/self/fd/%d", libFd);

        void* handle = dlopen(fdPath, RTLD_NOW);
        if (!handle) {
            LOGE("dlopen failed: %s", dlerror());
        } else {
            // 触发注入初始化
            typedef void (*InitFn)(const char*);
            auto init = (InitFn)dlsym(handle, "reshade_inject_init");
            if (init) {
                init(targetPkg.c_str());
                LOGI("reshade_inject_init called successfully");
            } else {
                LOGE("reshade_inject_init not found: %s", dlerror());
            }
        }
        close(libFd);
    }

    void preServerSpecialize(ServerSpecializeArgs* args) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api* api{};
    JNIEnv* env{};
    std::string targetPkg;
    int libFd{-1};
    bool shouldInject{false};
};

// ─────────────────────────────────────────────────────────
// Companion 进程：在 root 权限下读取库文件并传递 fd
// ─────────────────────────────────────────────────────────
static void companionHandler(int clientFd) {
    // 读取包名（暂未使用，可做额外校验）
    uint32_t len = 0;
    read(clientFd, &len, sizeof(len));
    char pkg[256]{};
    if (len > 0 && len < sizeof(pkg))
        read(clientFd, pkg, len);

    LOGI("Companion: sending lib to process '%s'", pkg);

    // 以只读方式打开核心库，发送 fd 给目标进程
    int fd = open(CORE_LIB, O_RDONLY);
    write(clientFd, &fd, sizeof(fd));
    if (fd >= 0) close(fd);
}

REGISTER_ZYGISK_MODULE(ReShadeModule)
REGISTER_ZYGISK_COMPANION(companionHandler)
