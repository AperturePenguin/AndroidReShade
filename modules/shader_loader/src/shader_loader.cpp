/**
 * Module 1: ShaderLoader — 实现
 */
#include "shader_loader.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <algorithm>
#include <android/log.h>

#define TAG "RS::ShaderLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace reshade {

// ════════════════════════════════════════════════════════
// 公开接口
// ════════════════════════════════════════════════════════

ShaderFile ShaderLoader::load(const std::string& path) {
    ShaderFile sf;
    sf.path = path;

    std::string raw = readFile(path);
    if (raw.empty()) {
        sf.errorLog = "Cannot read file: " + path;
        return sf;
    }

    // 确定 baseDir 用于解析 #include
    std::string baseDir = path.substr(0, path.find_last_of("/\\"));
    return loadFromSource(preprocessIncludes(raw, baseDir), path);
}

ShaderFile ShaderLoader::loadFromSource(const std::string& source,
                                         const std::string& name) {
    ShaderFile sf;
    sf.path = name;
    sf.rawSource = stripComments(source);

    try {
        parseUniforms  (sf.rawSource, sf);
        parseTextures  (sf.rawSource, sf);
        parseSamplers  (sf.rawSource, sf);
        parseTechniques(sf.rawSource, sf);
        sf.valid = true;
        LOGI("Loaded '%s': %zu uniforms, %zu textures, %zu techniques",
             name.c_str(),
             sf.uniforms.size(), sf.textures.size(), sf.techniques.size());
    } catch (const std::exception& e) {
        sf.errorLog = e.what();
        LOGE("Parse error in '%s': %s", name.c_str(), e.what());
    }
    return sf;
}

std::vector<std::string> ShaderLoader::scanDirectory(const std::string& dir) {
    std::vector<std::string> result;
    try {
        for (auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".fx" || ext == ".hlsl")
                result.push_back(entry.path().string());
        }
    } catch (...) {}
    std::sort(result.begin(), result.end());
    return result;
}

// ════════════════════════════════════════════════════════
// 文件读取 & 预处理
// ════════════════════════════════════════════════════════

std::string ShaderLoader::readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return { std::istreambuf_iterator<char>(f),
             std::istreambuf_iterator<char>() };
}

std::string ShaderLoader::stripComments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    size_t i = 0, n = src.size();
    while (i < n) {
        // 行注释
        if (i+1 < n && src[i]=='/' && src[i+1]=='/') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        // 块注释
        if (i+1 < n && src[i]=='/' && src[i+1]=='*') {
            i += 2;
            while (i+1 < n && !(src[i]=='*' && src[i+1]=='/')) {
                if (src[i]=='\n') out += '\n';
                ++i;
            }
            i += 2;
            continue;
        }
        out += src[i++];
    }
    return out;
}

std::string ShaderLoader::preprocessIncludes(const std::string& src,
                                              const std::string& baseDir) {
    // 简单处理 #include "file"（不递归防止循环）
    std::regex inc(R"(#include\s+"([^"]+)")");
    std::string out;
    out.reserve(src.size());
    auto it  = std::sregex_iterator(src.begin(), src.end(), inc);
    auto end = std::sregex_iterator();
    size_t pos = 0;
    for (auto m = it; m != end; ++m) {
        out += src.substr(pos, m->position() - pos);
        std::string incPath = baseDir + "/" + (*m)[1].str();
        std::string incSrc  = readFile(incPath);
        if (!incSrc.empty())
            out += stripComments(incSrc);
        else
            LOGE("Include not found: %s", incPath.c_str());
        pos = m->position() + m->length();
    }
    out += src.substr(pos);
    return out;
}

// ════════════════════════════════════════════════════════
// 解析 Uniforms
// ════════════════════════════════════════════════════════

/* 匹配模式示例：
   <ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_label = "Brightness">
   uniform float uBrightness = 0.5;
*/
void ShaderLoader::parseUniforms(const std::string& src, ShaderFile& out) {
    // 捕获可选注解块 + uniform 类型 + 名称 + 可选默认值
    static const std::regex re(
        R"((<[^>]*>)?\s*uniform\s+([\w]+(?:\d)?)\s+([\w]+)\s*(?:=\s*([^;]+))?\s*;)"
    );

    for (auto it = std::sregex_iterator(src.begin(), src.end(), re);
         it != std::sregex_iterator(); ++it)
    {
        UniformDesc ud;
        std::string annot = (*it)[1].str();
        ud.type           = (*it)[2].str();
        ud.name           = (*it)[3].str();
        std::string defStr= (*it)[4].str();

        if (!annot.empty())
            ud.ui = parseAnnotationBlock(annot);

        ud.defaultVal = parseDefaultValue(ud.type, defStr);

        // 内置语义检测
        if (ud.name == "Timer" || ud.name.find("TIMER") != std::string::npos)
            ud.semantic = "TIMER";
        else if (ud.name.find("FrameCount") != std::string::npos)
            ud.semantic = "FRAMECOUNT";

        out.uniforms.push_back(std::move(ud));
    }
}

// ════════════════════════════════════════════════════════
// 解析 Textures
// ════════════════════════════════════════════════════════

void ShaderLoader::parseTextures(const std::string& src, ShaderFile& out) {
    // texture2D Name <source="file.png"; width=512; height=512>;
    static const std::regex re(
        R"(texture(?:2D)?\s+([\w]+)\s*(?:<([^>]*)>)?\s*;)"
    );
    static const std::regex srcRe(R"(source\s*=\s*"([^"]*)")");
    static const std::regex wRe (R"(width\s*=\s*(\d+))");
    static const std::regex hRe (R"(height\s*=\s*(\d+))");
    static const std::regex fmtRe(R"(format\s*=\s*([\w]+))");

    for (auto it = std::sregex_iterator(src.begin(), src.end(), re);
         it != std::sregex_iterator(); ++it)
    {
        TextureDesc td;
        td.name = (*it)[1].str();
        std::string attrs = (*it)[2].str();

        std::smatch m;
        if (std::regex_search(attrs, m, srcRe)) td.source = m[1].str();
        if (std::regex_search(attrs, m, wRe))   td.width  = std::stoi(m[1].str());
        if (std::regex_search(attrs, m, hRe))   td.height = std::stoi(m[1].str());
        if (std::regex_search(attrs, m, fmtRe)) td.format = m[1].str();

        out.textures.push_back(std::move(td));
    }
}

// ════════════════════════════════════════════════════════
// 解析 Samplers
// ════════════════════════════════════════════════════════

void ShaderLoader::parseSamplers(const std::string& src, ShaderFile& out) {
    // sampler2D sName < Texture = <tName>; MagFilter=LINEAR; >
    static const std::regex re(
        R"(sampler(?:2D|State)?\s+([\w]+)\s*(?:<([^>]*)>)?\s*;)"
    );
    static const std::regex texRe(R"(Texture\s*=\s*[<\s]*([\w]+))");
    static const std::regex filtRe(R"((?:Mag|Min)Filter\s*=\s*([\w]+))");
    static const std::regex addrURe(R"(AddressU\s*=\s*([\w]+))");
    static const std::regex addrVRe(R"(AddressV\s*=\s*([\w]+))");

    for (auto it = std::sregex_iterator(src.begin(), src.end(), re);
         it != std::sregex_iterator(); ++it)
    {
        SamplerDesc sd;
        sd.name = (*it)[1].str();
        std::string attrs = (*it)[2].str();

        std::smatch m;
        if (std::regex_search(attrs, m, texRe))   sd.textureName = m[1].str();
        if (std::regex_search(attrs, m, filtRe))  sd.filter      = m[1].str();
        if (std::regex_search(attrs, m, addrURe)) sd.addressU    = m[1].str();
        if (std::regex_search(attrs, m, addrVRe)) sd.addressV    = m[1].str();

        // 若无明确绑定，采用同名纹理（ReShade 约定）
        if (sd.textureName.empty()) sd.textureName = sd.name;

        out.samplers.push_back(std::move(sd));
    }
}

// ════════════════════════════════════════════════════════
// 解析 Techniques / Passes
// ════════════════════════════════════════════════════════

void ShaderLoader::parseTechniques(const std::string& src, ShaderFile& out) {
    // technique Name { pass P0 { VertexShader = VS; PixelShader = PS; } }
    static const std::regex techRe(
        R"(technique\s+([\w]+)\s*\{((?:[^{}]|\{[^{}]*\})*)\})"
    );
    static const std::regex passRe(
        R"(pass\s*([\w]*)\s*\{([^}]*)\})"
    );
    static const std::regex vsRe (R"(VertexShader\s*=\s*(?:compile\s+\w+\s+)?([\w]+))");
    static const std::regex psRe (R"(PixelShader\s*=\s*(?:compile\s+\w+\s+)?([\w]+))");
    static const std::regex rtRe (R"(RenderTarget\s*=\s*([\w]+))");
    static const std::regex clrRe(R"(ClearRenderTargets\s*=\s*(true|false|1|0))");

    for (auto ti = std::sregex_iterator(src.begin(), src.end(), techRe);
         ti != std::sregex_iterator(); ++ti)
    {
        TechniqueDesc tech;
        tech.name = (*ti)[1].str();
        std::string techBody = (*ti)[2].str();

        for (auto pi = std::sregex_iterator(techBody.begin(), techBody.end(), passRe);
             pi != std::sregex_iterator(); ++pi)
        {
            PassDesc pass;
            pass.name = (*pi)[1].str();
            std::string passBody = (*pi)[2].str();

            std::smatch m;
            if (std::regex_search(passBody, m, vsRe))  pass.vertexEntry  = m[1].str();
            if (std::regex_search(passBody, m, psRe))  pass.pixelEntry   = m[1].str();
            if (std::regex_search(passBody, m, rtRe))  pass.renderTarget = m[1].str();
            if (std::regex_search(passBody, m, clrRe)) {
                std::string v = m[1].str();
                pass.clearTarget = (v == "true" || v == "1");
            }
            tech.passes.push_back(std::move(pass));
        }

        // 若没有明确 pass 块（纯 HLSL 文件），生成默认 pass
        if (tech.passes.empty()) {
            PassDesc defPass;
            defPass.name        = "Pass0";
            defPass.vertexEntry = "VS";
            defPass.pixelEntry  = "PS";
            tech.passes.push_back(defPass);
        }

        out.techniques.push_back(std::move(tech));
    }

    // 若完全没有 technique（纯 HLSL），生成默认 technique
    if (out.techniques.empty()) {
        TechniqueDesc def;
        def.name = "Default";
        PassDesc p; p.name = "Pass0";
        // 尝试推断入口名
        static const std::regex fnRe(R"(void\s+(VS|VertexShader|vs_main)\s*\()");
        std::smatch m;
        if (std::regex_search(src, m, fnRe)) p.vertexEntry = m[1].str();
        else p.vertexEntry = "VS";
        p.pixelEntry = "PS";
        def.passes.push_back(p);
        out.techniques.push_back(def);
    }
}

// ════════════════════════════════════════════════════════
// 辅助：注解 & 默认值解析
// ════════════════════════════════════════════════════════

UiAnnotation ShaderLoader::parseAnnotationBlock(const std::string& block) {
    UiAnnotation a;
    auto extract = [&](const std::string& key) -> std::string {
        std::regex re(key + R"(\s*=\s*"?([^",>;]+)"?)");
        std::smatch m;
        return std::regex_search(block, m, re) ? m[1].str() : "";
    };
    a.type  = extract("ui_type");
    a.label = extract("ui_label");
    std::string s;
    if (!(s = extract("ui_min")).empty())  a.min  = std::stof(s);
    if (!(s = extract("ui_max")).empty())  a.max  = std::stof(s);
    if (!(s = extract("ui_step")).empty()) a.step = std::stof(s);
    return a;
}

UniformValue ShaderLoader::parseDefaultValue(const std::string& type,
                                              const std::string& literal) {
    if (literal.empty()) return 0.f;
    try {
        if (type == "float")  return std::stof(literal);
        if (type == "int")    return std::stoi(literal);
        if (type == "bool")   return (literal == "true" || literal == "1");

        // float2/3/4：提取括号内的数值
        std::regex numRe(R"([-+]?[0-9]*\.?[0-9]+)");
        std::vector<float> vals;
        for (auto it = std::sregex_iterator(literal.begin(), literal.end(), numRe);
             it != std::sregex_iterator(); ++it)
            vals.push_back(std::stof(it->str()));

        if (type == "float2" && vals.size() >= 2)
            return std::array<float,2>{vals[0], vals[1]};
        if (type == "float3" && vals.size() >= 3)
            return std::array<float,3>{vals[0], vals[1], vals[2]};
        if (type == "float4" && vals.size() >= 4)
            return std::array<float,4>{vals[0], vals[1], vals[2], vals[3]};
    } catch (...) {}
    return 0.f;
}

} // namespace reshade
