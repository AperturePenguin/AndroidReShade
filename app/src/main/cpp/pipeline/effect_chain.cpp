/**
 * effect_chain.cpp — 后处理效果链
 *
 * 管理多个 Pass 的执行顺序：
 *   游戏帧 → FBO0 → Pass1(shader) → FBO1 → Pass2(shader) → 默认FBO
 *
 * 内置 Pass：亮度 / 对比度 / 饱和度 / 锐化 / LUT
 * 外置 Pass：用户加载的 .fx / .hlsl 文件
 */

#include "effect_chain.h"
#include "../shader/fx_parser.h"
#include "../shader/hlsl_transpiler.h"
#include "../lut/lut_loader.h"
#include <android/log.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <vector>
#include <memory>

#define LOG_TAG "ReShadeChain"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─────────────────────────────────────────────────────────
// 内置 Shader 源码
// ─────────────────────────────────────────────────────────

// 全屏三角形顶点 shader
static const char* FULLSCREEN_VERT = R"GLSL(
#version 300 es
out vec2 vTexCoord;

void main() {
    // 用三角形覆盖全屏（无需 VBO）
    vec2 pos = vec2(
        float((gl_VertexID & 1) << 2) - 1.0,
        float((gl_VertexID & 2) << 1) - 1.0
    );
    vTexCoord = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";

// 亮度/对比度/饱和度/锐化一体 Pass
static const char* BUILTIN_FRAG = R"GLSL(
#version 300 es
precision highp float;
in vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uTexture;
uniform vec2      uTexelSize;
uniform float     uBrightness;   // -1.0 ~ 1.0
uniform float     uContrast;     // 0.0 ~ 3.0
uniform float     uSaturation;   // 0.0 ~ 3.0
uniform float     uSharpness;    // 0.0 ~ 5.0
uniform float     uVignette;     // 0.0 ~ 1.0
uniform float     uGamma;        // 0.1 ~ 3.0

// 亮度/对比度/饱和度
vec3 adjustBCS(vec3 col) {
    // 亮度
    col += uBrightness;
    // 对比度
    col = (col - 0.5) * uContrast + 0.5;
    // 饱和度（YCbCr 方式）
    float gray = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(gray), col, uSaturation);
    return col;
}

// Unsharp Mask 锐化
vec3 sharpen(vec3 center) {
    if (uSharpness < 0.001) return center;
    vec3 blur = vec3(0.0);
    blur += texture(uTexture, vTexCoord + vec2(-1,  0) * uTexelSize).rgb;
    blur += texture(uTexture, vTexCoord + vec2( 1,  0) * uTexelSize).rgb;
    blur += texture(uTexture, vTexCoord + vec2( 0, -1) * uTexelSize).rgb;
    blur += texture(uTexture, vTexCoord + vec2( 0,  1) * uTexelSize).rgb;
    blur *= 0.25;
    return center + (center - blur) * uSharpness;
}

// 暗角
float vignette(vec2 uv) {
    vec2 d = (uv - 0.5) * 2.0;
    return 1.0 - dot(d, d) * uVignette * 0.5;
}

void main() {
    vec4 color = texture(uTexture, vTexCoord);
    vec3 col   = color.rgb;

    col = adjustBCS(col);
    col = sharpen(col);

    // Gamma 矫正
    if (uGamma != 1.0)
        col = pow(max(col, vec3(0.0)), vec3(1.0 / uGamma));

    // 暗角
    col *= vignette(vTexCoord);

    fragColor = vec4(clamp(col, 0.0, 1.0), color.a);
}
)GLSL";

// LUT Pass
static const char* LUT_FRAG = R"GLSL(
#version 300 es
precision highp float;
precision highp sampler3D;
in vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uTexture;
uniform sampler3D uLUT;
uniform float     uLUTStrength;  // 0.0 ~ 1.0

void main() {
    vec4 orig  = texture(uTexture, vTexCoord);
    // LUT 尺寸 32x32x32，归一化坐标 = value / 31.0 * (31.0/32.0) + 0.5/32.0
    vec3 scale = vec3(31.0 / 32.0);
    vec3 bias  = vec3(0.5 / 32.0);
    vec3 lutColor = texture(uLUT, orig.rgb * scale + bias).rgb;
    fragColor = vec4(mix(orig.rgb, lutColor, uLUTStrength), orig.a);
}
)GLSL";

// ─────────────────────────────────────────────────────────
// 辅助：编译 GLSL Shader
// ─────────────────────────────────────────────────────────
static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOGE("Shader compile error:\n%s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint linkProgram(const char* vertSrc, const char* fragSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER,   vertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint status;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        LOGE("Program link error:\n%s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ─────────────────────────────────────────────────────────
// EffectChain 实现
// ─────────────────────────────────────────────────────────
struct RenderPass {
    GLuint      program{0};
    std::string name;
    bool        enabled{true};
};

struct EffectChainImpl {
    int    width{0}, height{0};

    // FBO 双缓冲
    GLuint fbo[2]{0, 0};
    GLuint fbTex[2]{0, 0};
    int    currentFBO{0};

    // 内置 Pass
    GLuint builtinProgram{0};
    GLuint lutProgram{0};

    // 用户自定义 Pass 列表
    std::vector<RenderPass> userPasses;

    // LUT 纹理
    GLuint lutTex{0};
    bool   lutEnabled{false};

    // 参数 uniform 位置
    GLint locBrightness{-1}, locContrast{-1}, locSaturation{-1};
    GLint locSharpness{-1}, locVignette{-1}, locGamma{-1};
    GLint locTexelSize{-1};

    // 当前参数值
    EffectParams params;

    // VAO（全屏三角）
    GLuint vao{0};
};

EffectChain::EffectChain(int w, int h) : impl(new EffectChainImpl()) {
    impl->width  = w;
    impl->height = h;
    impl->params = {0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    createFBOs();
    buildBuiltinShaders();
}

EffectChain::~EffectChain() {
    if (impl) {
        glDeleteFramebuffers(2, impl->fbo);
        glDeleteTextures(2, impl->fbTex);
        if (impl->builtinProgram) glDeleteProgram(impl->builtinProgram);
        if (impl->lutProgram)     glDeleteProgram(impl->lutProgram);
        if (impl->lutTex)         glDeleteTextures(1, &impl->lutTex);
        if (impl->vao)            glDeleteVertexArrays(1, &impl->vao);
        delete impl;
    }
}

void EffectChain::createFBOs() {
    glGenFramebuffers(2, impl->fbo);
    glGenTextures(2, impl->fbTex);

    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, impl->fbTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     impl->width, impl->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, impl->fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, impl->fbTex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 创建 VAO（空，用于全屏三角）
    glGenVertexArrays(1, &impl->vao);
}

void EffectChain::buildBuiltinShaders() {
    impl->builtinProgram = linkProgram(FULLSCREEN_VERT, BUILTIN_FRAG);
    impl->lutProgram     = linkProgram(FULLSCREEN_VERT, LUT_FRAG);

    if (impl->builtinProgram) {
        glUseProgram(impl->builtinProgram);
        impl->locBrightness  = glGetUniformLocation(impl->builtinProgram, "uBrightness");
        impl->locContrast    = glGetUniformLocation(impl->builtinProgram, "uContrast");
        impl->locSaturation  = glGetUniformLocation(impl->builtinProgram, "uSaturation");
        impl->locSharpness   = glGetUniformLocation(impl->builtinProgram, "uSharpness");
        impl->locVignette    = glGetUniformLocation(impl->builtinProgram, "uVignette");
        impl->locGamma       = glGetUniformLocation(impl->builtinProgram, "uGamma");
        impl->locTexelSize   = glGetUniformLocation(impl->builtinProgram, "uTexelSize");
        LOGI("Builtin shader compiled OK");
    }
}

void EffectChain::resize(int w, int h) {
    impl->width  = w;
    impl->height = h;
    glDeleteFramebuffers(2, impl->fbo);
    glDeleteTextures(2, impl->fbTex);
    createFBOs();
}

void EffectChain::loadDefaultEffects() {
    // 默认只启用内置 Pass，无需额外操作
    LOGI("Default effects loaded");
}

void EffectChain::render(GLint originalFBO) {
    if (!impl->builtinProgram) return;

    const int w = impl->width, h = impl->height;

    // ── Step 1: 将当前帧内容读入 FBO[0] ────────────────
    glBindFramebuffer(GL_READ_FRAMEBUFFER, originalFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, impl->fbo[0]);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    int src = 0, dst = 1;

    // ── Step 2: 内置效果 Pass ────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, impl->fbo[dst]);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(impl->builtinProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl->fbTex[src]);
    glUniform1i(glGetUniformLocation(impl->builtinProgram, "uTexture"), 0);
    glUniform2f(impl->locTexelSize, 1.0f/w, 1.0f/h);
    glUniform1f(impl->locBrightness,  impl->params.brightness);
    glUniform1f(impl->locContrast,    impl->params.contrast);
    glUniform1f(impl->locSaturation,  impl->params.saturation);
    glUniform1f(impl->locSharpness,   impl->params.sharpness);
    glUniform1f(impl->locVignette,    impl->params.vignette);
    glUniform1f(impl->locGamma,       impl->params.gamma);

    glBindVertexArray(impl->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::swap(src, dst);

    // ── Step 3: 用户自定义 Pass ──────────────────────────
    for (auto& pass : impl->userPasses) {
        if (!pass.enabled || !pass.program) continue;
        glBindFramebuffer(GL_FRAMEBUFFER, impl->fbo[dst]);
        glUseProgram(pass.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, impl->fbTex[src]);
        glUniform1i(glGetUniformLocation(pass.program, "uTexture"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::swap(src, dst);
    }

    // ── Step 4: LUT Pass（如果启用）─────────────────────
    if (impl->lutEnabled && impl->lutTex && impl->lutProgram) {
        glBindFramebuffer(GL_FRAMEBUFFER, impl->fbo[dst]);
        glUseProgram(impl->lutProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, impl->fbTex[src]);
        glUniform1i(glGetUniformLocation(impl->lutProgram, "uTexture"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, impl->lutTex);
        glUniform1i(glGetUniformLocation(impl->lutProgram, "uLUT"), 1);
        glUniform1f(glGetUniformLocation(impl->lutProgram, "uLUTStrength"),
                    impl->params.lutStrength);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::swap(src, dst);
    }

    // ── Step 5: 将结果 blit 回原始 FBO ──────────────────
    glBindFramebuffer(GL_READ_FRAMEBUFFER, impl->fbo[src]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, originalFBO);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, originalFBO);
}

void EffectChain::setParams(const EffectParams& p) {
    impl->params = p;
}

void EffectChain::loadShader(const char* path) {
    FxParser parser;
    HlslTranspiler transpiler;

    FxEffect fx = parser.parse(path);
    if (fx.techniques.empty()) {
        LOGE("No techniques found in %s", path);
        return;
    }

    for (auto& tech : fx.techniques) {
        for (auto& pass : tech.passes) {
            // 转译 vertex shader
            auto vs = transpiler.transpile(fx.sourceCode, ShaderStage::Vertex, pass.vertexEntry);
            auto fs = transpiler.transpile(fx.sourceCode, ShaderStage::Fragment, pass.pixelEntry);

            if (!vs.success || !fs.success) {
                LOGE("Shader transpile failed for pass: %s", pass.name.c_str());
                continue;
            }

            GLuint prog = linkProgram(vs.glsl.c_str(), fs.glsl.c_str());
            if (!prog) continue;

            RenderPass rp;
            rp.program = prog;
            rp.name    = tech.name + "." + pass.name;
            impl->userPasses.push_back(rp);
            LOGI("Loaded shader pass: %s", rp.name.c_str());
        }
    }
}

void EffectChain::loadLUT(const char* path) {
    LutLoader loader;
    GLuint tex = loader.load(path);
    if (!tex) {
        LOGE("Failed to load LUT: %s", path);
        return;
    }
    if (impl->lutTex) glDeleteTextures(1, &impl->lutTex);
    impl->lutTex     = tex;
    impl->lutEnabled = true;
    LOGI("LUT loaded: %s", path);
}
