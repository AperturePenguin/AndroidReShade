/**
 * hlsl_transpiler.cpp — HLSL → GLSL ES 3.0 转译器
 *
 * 策略：
 *   1. 词法级替换（类型名、内置函数）
 *   2. 语义级处理（SV_Position, TEXCOORD0 等）
 *   3. 生成 #version 300 es 前导 + precision qualifier
 *   4. 处理 cbuffer / uniform struct
 */

#include "hlsl_transpiler.h"
#include <android/log.h>
#include <regex>
#include <sstream>
#include <algorithm>
#include <unordered_map>

#define LOG_TAG "ReShadeHLSL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

// ─────────────────────────────────────────────────────────
// 类型替换表（完整版）
// ─────────────────────────────────────────────────────────
static const std::unordered_map<std::string, std::string> TYPE_MAP = {
    {"float4",    "vec4"},   {"float3",    "vec3"},   {"float2",    "vec2"},
    {"float1",    "float"},  {"int4",      "ivec4"},  {"int3",      "ivec3"},
    {"int2",      "ivec2"},  {"int1",      "int"},    {"bool4",     "bvec4"},
    {"bool3",     "bvec3"},  {"bool2",     "bvec2"},  {"bool1",     "bool"},
    {"uint4",     "uvec4"},  {"uint3",     "uvec3"},  {"uint2",     "uvec2"},
    {"uint1",     "uint"},   {"half4",     "vec4"},   {"half3",     "vec3"},
    {"half2",     "vec2"},   {"half",      "float"},
    {"float4x4",  "mat4"},   {"float3x3",  "mat3"},   {"float2x2",  "mat2"},
    {"float3x4",  "mat3x4"}, {"float4x3",  "mat4x3"},
    {"Texture2D", "sampler2D"}, {"texture",  "sampler2D"},
    {"sampler2D", "sampler2D"},
};

// ─────────────────────────────────────────────────────────
// 内置函数替换表
// ─────────────────────────────────────────────────────────
static const std::vector<std::pair<std::string, std::string>> FUNC_MAP = {
    {"saturate(",    "clamp("},
    {"lerp(",        "mix("},
    {"frac(",        "fract("},
    {"rsqrt(",       "(1.0/sqrt("},   // 需要额外加 )
    {"ddx(",         "dFdx("},
    {"ddy(",         "dFdy("},
    {"atan2(",       "atan("},
    {"clip(",        "if("},          // 不完美，需进一步处理
    {"sincos(",      "/* sincos */"},
    {"isnan(",       "isnan("},
    {"isinf(",       "isinf("},
};

// ─────────────────────────────────────────────────────────
// 语义 → GLSL 输入/输出变量名映射
// ─────────────────────────────────────────────────────────
static const std::unordered_map<std::string, std::string> SEMANTIC_VERT_OUT = {
    {"SV_POSITION", "gl_Position"},
    {"SV_Position", "gl_Position"},
    {"POSITION",    "gl_Position"},
};

static const std::unordered_map<std::string, std::string> SEMANTIC_FRAG_IN = {
    {"TEXCOORD",   "vTexCoord"},
    {"TEXCOORD0",  "vTexCoord0"},
    {"TEXCOORD1",  "vTexCoord1"},
    {"TEXCOORD2",  "vTexCoord2"},
    {"COLOR",      "vColor"},
    {"COLOR0",     "vColor0"},
    {"NORMAL",     "vNormal"},
};

// ─────────────────────────────────────────────────────────
// 主转译流程
// ─────────────────────────────────────────────────────────
HlslTranspiler::Result HlslTranspiler::transpile(
    const std::string& hlslSrc,
    ShaderStage stage,
    const std::string& entryPoint)
{
    Result result;
    std::string src = hlslSrc;

    // Step 1: 移除 HLSL 特有预处理器指令
    src = removeHlslPragmas(src);

    // Step 2: 替换类型名
    src = replaceTypes(src);

    // Step 3: 替换内置函数
    src = replaceFunctions(src);

    // Step 4: 处理 cbuffer → uniform struct
    src = transformCBuffer(src);

    // Step 5: 处理 sampler + texture → sampler2D
    src = transformSamplers(src);

    // Step 6: 处理语义注解，生成 in/out 声明
    std::string ioDecls;
    src = transformSemantics(src, stage, entryPoint, ioDecls);

    // Step 7: 处理 mul() 矩阵乘法（HLSL 行主序 → GLSL 列主序）
    src = transformMul(src);

    // Step 8: 替换 SV_Target 输出
    if (stage == ShaderStage::Fragment) {
        src = transformFragOutput(src);
    }

    // Step 9: 组装最终 GLSL
    std::ostringstream out;
    out << "#version 300 es\n";
    out << "precision highp float;\n";
    out << "precision highp int;\n";
    out << "precision highp sampler2D;\n\n";
    out << ioDecls << "\n";
    out << src;

    result.glsl     = out.str();
    result.success  = true;
    result.stage    = stage;

    LOGI("Transpiled %s shader (entry: %s), GLSL size: %zu bytes",
         stage == ShaderStage::Vertex ? "vertex" : "fragment",
         entryPoint.c_str(), result.glsl.size());

    return result;
}

// ─────────────────────────────────────────────────────────
// Step 实现
// ─────────────────────────────────────────────────────────
std::string HlslTranspiler::removeHlslPragmas(const std::string& src) {
    // 移除 #pragma pack_matrix 等 HLSL 专属指令
    static const std::vector<std::string> toRemove = {
        "#pragma pack_matrix",
        "#pragma warning",
        "register(s",
        "register(b",
        "register(t",
    };
    std::string out = src;
    for (auto& s : toRemove) {
        size_t pos;
        while ((pos = out.find(s)) != std::string::npos) {
            size_t end = out.find('\n', pos);
            out.erase(pos, end == std::string::npos ? std::string::npos : end - pos);
        }
    }
    return out;
}

std::string HlslTranspiler::replaceTypes(const std::string& src) {
    std::string out = src;
    // 按长度从长到短替换，避免 float2 先匹配 float
    std::vector<std::pair<std::string, std::string>> sorted(TYPE_MAP.begin(), TYPE_MAP.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b){
        return a.first.size() > b.first.size();
    });
    for (auto& [hlsl, glsl] : sorted) {
        if (hlsl.empty() || glsl.empty()) continue;
        std::regex re("\\b" + hlsl + "\\b");
        out = std::regex_replace(out, re, glsl);
    }
    return out;
}

std::string HlslTranspiler::replaceFunctions(const std::string& src) {
    std::string out = src;
    for (auto& [hlsl, glsl] : FUNC_MAP) {
        size_t pos = 0;
        while ((pos = out.find(hlsl, pos)) != std::string::npos) {
            out.replace(pos, hlsl.size(), glsl);
            pos += glsl.size();
        }
    }
    return out;
}

std::string HlslTranspiler::transformCBuffer(const std::string& src) {
    // cbuffer Name { ... } → 展开为 uniform 变量
    std::regex re(R"(cbuffer\s+\w+\s*(?::\s*register\([^)]+\))?\s*\{([^}]*)\})");
    std::string out = std::regex_replace(src, re, [](const std::smatch& m) -> std::string {
        std::string body = m[1].str();
        // 给每行加上 uniform 前缀
        std::istringstream ss(body);
        std::ostringstream res;
        std::string line;
        while (std::getline(ss, line)) {
            // 跳过空行
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            if (!trimmed.empty() && trimmed != "{" && trimmed != "}")
                res << "uniform " << line << "\n";
        }
        return res.str();
    });
    return out;
}

std::string HlslTranspiler::transformSamplers(const std::string& src) {
    // SamplerState s; + Texture2D t; → 合并为 uniform sampler2D t;
    // 简化处理：直接移除 SamplerState 声明，Texture2D 已被替换为 sampler2D
    std::regex samplerDecl(R"(SamplerState\s+\w+\s*;)");
    std::string out = std::regex_replace(src, samplerDecl, "");

    // 将 t.Sample(s, uv) → texture(t, uv)
    std::regex sampleCall(R"((\w+)\.Sample\s*\(\s*\w+\s*,\s*([^)]+)\))");
    out = std::regex_replace(out, sampleCall, "texture($1, $2)");

    // .SampleLevel(s, uv, lod) → textureLod(t, uv, lod)
    std::regex sampleLevel(R"((\w+)\.SampleLevel\s*\(\s*\w+\s*,\s*([^,]+),\s*([^)]+)\))");
    out = std::regex_replace(out, sampleLevel, "textureLod($1, $2, $3)");

    return out;
}

std::string HlslTranspiler::transformSemantics(
    const std::string& src, ShaderStage stage,
    const std::string& entryPoint, std::string& ioDecls)
{
    // 找到入口函数的输入结构体或参数
    // 生成对应的 in/out varying 声明
    // 这是一个简化版，完整实现需要完整的语法分析

    std::ostringstream decls;
    int location = 0;

    if (stage == ShaderStage::Vertex) {
        // 标准 ReShade 顶点输入
        decls << "layout(location=0) in vec3 aPosition;\n";
        decls << "layout(location=1) in vec2 aTexCoord;\n";
        decls << "out vec2 vTexCoord;\n";
        decls << "out vec2 vTexCoord0;\n";
    } else {
        // fragment
        decls << "in vec2 vTexCoord;\n";
        decls << "in vec2 vTexCoord0;\n";
        decls << "out vec4 fragColor;\n";
    }

    // 处理 SV_Target 输出
    std::string out = src;
    std::regex svTarget(R"(:\s*SV_TARGET\d*)");
    out = std::regex_replace(out, svTarget, "");

    // 处理 SV_POSITION
    std::regex svPos(R"(:\s*SV_POSITION)");
    out = std::regex_replace(out, svPos, "");

    // 处理 TEXCOORD 语义
    std::regex texCoord(R"(:\s*TEXCOORD(\d*))");
    out = std::regex_replace(out, texCoord, "");

    ioDecls = decls.str();
    return out;
}

std::string HlslTranspiler::transformMul(const std::string& src) {
    // HLSL mul(A, B) 在 GLSL 中是 B * A（行列主序差异）
    // 简化处理：mul(a, b) → (b * a)
    std::regex mulRe(R"(mul\s*\(\s*([^,]+),\s*([^)]+)\))");
    return std::regex_replace(src, mulRe, "($2 * $1)");
}

std::string HlslTranspiler::transformFragOutput(const std::string& src) {
    // return float4(...) 在 fragment main 里 → fragColor = vec4(...)
    // 复杂情况需要完整 AST，这里做简化版
    std::regex retRe(R"(return\s+(vec4\s*\([^;]+\))\s*;)");
    return std::regex_replace(src, retRe, "fragColor = $1;");
}
