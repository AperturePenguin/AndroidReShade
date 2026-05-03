/**
 * fx_parser.cpp — ReShade .fx / .hlsl 文件解析器
 *
 * 支持：
 *   - 解析 technique / pass / sampler / texture 声明
 *   - 提取 uniform 变量及 UI 注解（<ui_type> / <ui_min> / <ui_max> 等）
 *   - 将 HLSL 语义转译为 GLSL ES 3.0 等效代码
 */

#include "fx_parser.h"
#include <android/log.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>

#define LOG_TAG "ReShadeFX"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─────────────────────────────────────────────────────────
// HLSL → GLSL ES 关键字映射表
// ─────────────────────────────────────────────────────────
static const struct { const char* hlsl; const char* glsl; } TYPE_MAP[] = {
    // 基础类型
    {"float4",    "vec4"},   {"float3",    "vec3"},  {"float2",    "vec2"},
    {"float1",    "float"},  {"int4",      "ivec4"}, {"int3",      "ivec3"},
    {"int2",      "ivec2"},  {"uint4",     "uvec4"}, {"uint3",     "uvec3"},
    {"uint2",     "uvec2"},  {"half4",     "vec4"},  {"half3",     "vec3"},
    {"half2",     "vec2"},   {"half",      "float"},
    // 矩阵
    {"float4x4",  "mat4"},   {"float3x3",  "mat3"},  {"float2x2",  "mat2"},
    {"float4x3",  "mat4x3"}, {"float3x4",  "mat3x4"},
    // 纹理采样
    {"Texture2D", "sampler2D"}, {"texture2D", "sampler2D"},
    {"SamplerState", ""},    // GLSL 中与纹理合并
    // 内置函数
    {"saturate",  "clamp"},  {"lerp",      "mix"},   {"frac",      "fract"},
    {"mul(",      "(*"},     // 注意：矩阵乘法方向相反，需特殊处理
    {nullptr, nullptr}
};

// ─────────────────────────────────────────────────────────
// 解析 uniform 注解
// ─────────────────────────────────────────────────────────
static void parseAnnotations(const std::string& block, UniformInfo& info) {
    // <ui_type = "slider"> / <ui_min = 0.0> / <ui_max = 1.0> / <ui_label = "...">
    auto extract = [&](const std::string& key) -> std::string {
        std::regex re("<\\s*" + key + "\\s*=\\s*\"?([^\">,]+)\"?\\s*>");
        std::smatch m;
        if (std::regex_search(block, m, re)) return m[1].str();
        return "";
    };

    info.uiType    = extract("ui_type");
    info.uiLabel   = extract("ui_label");
    auto minStr    = extract("ui_min");
    auto maxStr    = extract("ui_max");
    auto stepStr   = extract("ui_step");
    auto defStr    = extract("ui_default");

    if (!minStr.empty())  info.uiMin  = std::stof(minStr);
    if (!maxStr.empty())  info.uiMax  = std::stof(maxStr);
    if (!stepStr.empty()) info.uiStep = std::stof(stepStr);
    if (!defStr.empty())  info.defaultValue = std::stof(defStr);
}

// ─────────────────────────────────────────────────────────
// 主解析流程
// ─────────────────────────────────────────────────────────
FxEffect FxParser::parse(const std::string& filePath) {
    FxEffect effect;
    effect.filePath = filePath;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOGE("Cannot open fx file: %s", filePath.c_str());
        return effect;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string src = ss.str();

    // 移除注释
    src = removeComments(src);

    // 解析 uniform 变量
    parseUniforms(src, effect);

    // 解析 technique / pass
    parseTechniques(src, effect);

    // 解析 texture 声明
    parseTextures(src, effect);

    LOGI("Parsed fx: %s | uniforms=%zu techniques=%zu",
         filePath.c_str(), effect.uniforms.size(), effect.techniques.size());

    return effect;
}

// ─────────────────────────────────────────────────────────
// 移除 C 风格注释
// ─────────────────────────────────────────────────────────
std::string FxParser::removeComments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    size_t i = 0;
    while (i < src.size()) {
        if (i + 1 < src.size() && src[i] == '/' && src[i+1] == '/') {
            while (i < src.size() && src[i] != '\n') i++;
        } else if (i + 1 < src.size() && src[i] == '/' && src[i+1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i+1] == '/')) {
                if (src[i] == '\n') out += '\n';
                i++;
            }
            i += 2;
        } else {
            out += src[i++];
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────
// 解析 uniform 声明
// ─────────────────────────────────────────────────────────
void FxParser::parseUniforms(const std::string& src, FxEffect& effect) {
    // 匹配：[注解块] uniform <类型> <名称> [= <默认值>];
    std::regex re(R"((<[^>]*>)?\s*uniform\s+(\w+)\s+(\w+)\s*(?:=\s*([^;]+))?\s*;)");
    auto begin = std::sregex_iterator(src.begin(), src.end(), re);
    auto end   = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        UniformInfo u;
        std::string annotBlock = (*it)[1].str();
        u.type         = (*it)[2].str();
        u.name         = (*it)[3].str();
        u.defaultValue = 0.0f;
        u.uiMin        = 0.0f;
        u.uiMax        = 1.0f;
        u.uiStep       = 0.01f;

        if (!annotBlock.empty())
            parseAnnotations(annotBlock, u);

        // 解析默认值
        std::string defStr = (*it)[4].str();
        if (!defStr.empty()) {
            try { u.defaultValue = std::stof(defStr); } catch (...) {}
        }

        effect.uniforms.push_back(u);
        LOGI("  uniform: %s %s (default=%.2f min=%.2f max=%.2f)",
             u.type.c_str(), u.name.c_str(),
             u.defaultValue, u.uiMin, u.uiMax);
    }
}

// ─────────────────────────────────────────────────────────
// 解析 technique / pass 块
// ─────────────────────────────────────────────────────────
void FxParser::parseTechniques(const std::string& src, FxEffect& effect) {
    std::regex techRe(R"(technique\s+(\w+)\s*\{([^}]*(?:\{[^}]*\}[^}]*)*)\})");
    std::regex passRe(R"(pass\s+(\w+)?\s*\{([^}]*)\})");

    auto tBegin = std::sregex_iterator(src.begin(), src.end(), techRe);
    auto tEnd   = std::sregex_iterator();

    for (auto it = tBegin; it != tEnd; ++it) {
        TechniqueInfo tech;
        tech.name = (*it)[1].str();
        std::string techBody = (*it)[2].str();

        auto pBegin = std::sregex_iterator(techBody.begin(), techBody.end(), passRe);
        for (auto pit = pBegin; pit != std::sregex_iterator(); ++pit) {
            PassInfo pass;
            pass.name = (*pit)[1].str();
            std::string passBody = (*pit)[2].str();

            // 提取 VertexShader / PixelShader 入口点
            std::regex vsRe(R"(VertexShader\s*=\s*compile\s+\w+\s+(\w+)\s*\(\s*\))");
            std::regex psRe(R"(PixelShader\s*=\s*compile\s+\w+\s+(\w+)\s*\(\s*\))");
            std::smatch vsm, psm;
            if (std::regex_search(passBody, vsm, vsRe)) pass.vertexEntry = vsm[1].str();
            if (std::regex_search(passBody, psm, psRe)) pass.pixelEntry  = psm[1].str();

            // 新式 ReShade 4.x 语法
            std::regex vsRe2(R"(VertexShader\s*=\s*(\w+)\s*;)");
            std::regex psRe2(R"(PixelShader\s*=\s*(\w+)\s*;)");
            if (pass.vertexEntry.empty() && std::regex_search(passBody, vsm, vsRe2))
                pass.vertexEntry = vsm[1].str();
            if (pass.pixelEntry.empty() && std::regex_search(passBody, psm, psRe2))
                pass.pixelEntry = psm[1].str();

            tech.passes.push_back(pass);
            LOGI("  pass: %s vs=%s ps=%s",
                 pass.name.c_str(), pass.vertexEntry.c_str(), pass.pixelEntry.c_str());
        }

        effect.techniques.push_back(tech);
        LOGI("technique: %s (%zu passes)", tech.name.c_str(), tech.passes.size());
    }
}

// ─────────────────────────────────────────────────────────
// 解析 texture 声明
// ─────────────────────────────────────────────────────────
void FxParser::parseTextures(const std::string& src, FxEffect& effect) {
    std::regex re(R"(texture\s+(\w+)\s*(?:<([^>]*)>)?\s*;)");
    auto begin = std::sregex_iterator(src.begin(), src.end(), re);
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        TextureInfo tex;
        tex.name   = (*it)[1].str();
        tex.source = "";

        // 解析 <source = "file.png">
        std::string attrs = (*it)[2].str();
        std::regex srcRe(R"(source\s*=\s*\"([^\"]+)\")");
        std::smatch sm;
        if (std::regex_search(attrs, sm, srcRe)) tex.source = sm[1].str();

        effect.textures.push_back(tex);
    }
}
