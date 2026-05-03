#pragma once
/**
 * Common: 模块间公用头文件
 * 包含日志宏、版本、公共类型别名
 */

#include <string>
#include <vector>
#include <android/log.h>

#define RS_VERSION_MAJOR 1
#define RS_VERSION_MINOR 0
#define RS_VERSION_PATCH 0
#define RS_VERSION_STR   "1.0.0"

// 统一日志宏（各模块直接 #include "reshade_common.h" + 定义各自的 TAG）
#ifndef RS_TAG
#  define RS_TAG "ReShade"
#endif
#define RS_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  RS_TAG, __VA_ARGS__)
#define RS_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  RS_TAG, __VA_ARGS__)
#define RS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, RS_TAG, __VA_ARGS__)
#define RS_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, RS_TAG, __VA_ARGS__)

namespace reshade {

/// 模块间传递的简单 float 参数包
struct ParamPack {
    std::string name;
    float       value = 0.f;
};

/// 通用错误结果
struct Error {
    std::string message;
    bool        ok = true;
    explicit operator bool() const { return ok; }
    static Error success()                    { return {"", true}; }
    static Error fail(const std::string& msg) { return {msg, false}; }
};

} // namespace reshade
