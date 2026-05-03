/**
 * Module 5: ShaderRuntime — 实现
 */
#include "shader_runtime.h"
#include "../../../modules/hlsl_converter/include/hlsl_converter.h"
#include "../../../modules/shader_loader/include/shader_loader.h"
#include <android/log.h>
#include <algorithm>
#include <chrono>

#define TAG "RS::ShaderRuntime"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace reshade {

// ════════════════════════════════════════════════════════
// 内置 Vertex Shader（全屏三角，无 VBO）
// ════════════════════════════════════════════════════════
static const char* FULLSCREEN_VS = R"GLSL(
#version 300 es
out vec2 vTexCoord;
void main() {
    // 用顶点 ID 驱动全屏三角（不需要任何 VBO）
    float x = float((gl_VertexID & 1) << 2) - 1.0;
    float y = float((gl_VertexID & 2) << 1) - 1.0;
    vTexCoord = vec2(x, y) * 0.5 + 0.5;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)GLSL";

// ════════════════════════════════════════════════════════
// FboPool
// ════════════════════════════════════════════════════════

void ShaderRuntime::FboPool::init(int w, int h) {
    width = w; height = h;
    glGenFramebuffers(2, fbo);
    glGenTextures(2, tex);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShaderRuntime::FboPool::resize(int w, int h) {
    if (width == w && height == h) return;
    destroy(); init(w, h);
}

void ShaderRuntime::FboPool::destroy() {
    if (fbo[0]) glDeleteFramebuffers(2, fbo);
    if (tex[0]) glDeleteTextures(2, tex);
    fbo[0] = fbo[1] = tex[0] = tex[1] = 0;
}

// ════════════════════════════════════════════════════════
// ShaderRuntime
// ════════════════════════════════════════════════════════

ShaderRuntime::ShaderRuntime() {}

ShaderRuntime::~ShaderRuntime() {
    fboPool_.destroy();
    if (vao_) glDeleteVertexArrays(1, &vao_);
    for (auto& [id, eff] : effects_)
        for (auto& pass : eff.passes)
            if (pass.program) glDeleteProgram(pass.program);
}

void ShaderRuntime::ensureVAO() {
    if (vao_) return;
    glGenVertexArrays(1, &vao_);
}

// ── 加载 Effect ────────────────────────────────────────

int ShaderRuntime::loadEffect(const std::string& path) {
    ShaderLoader loader;
    ShaderFile sf = loader.load(path);
    if (!sf.valid) {
        LOGE("loadEffect failed: %s — %s", path.c_str(), sf.errorLog.c_str());
        return -1;
    }
    return addEffect(sf);
}

int ShaderRuntime::addEffect(const ShaderFile& sf) {
    CompiledEffect eff;
    eff.name       = sf.path.substr(sf.path.find_last_of("/\\") + 1);
    eff.sourcePath = sf.path;
    eff.uniforms   = sf.uniforms;

    // 用 HlslConverter 转译每个 pass 的 VS/PS
    HlslConverter conv;
    ConvertOptions opts;
    opts.wrapWithMain = true;

    for (auto& tech : sf.techniques) {
        for (auto& pd : tech.passes) {
            CompiledPass cp;
            cp.name = tech.name + "." + pd.name;

            // 转译 VS
            auto vsRes = conv.convert(sf.rawSource, pd.vertexEntry,  ShaderStage::Vertex);
            auto fsRes = conv.convert(sf.rawSource, pd.pixelEntry,   ShaderStage::Fragment);

            if (!vsRes.success || !fsRes.success) {
                LOGE("Shader convert failed for pass %s", cp.name.c_str());
                // 使用内置全屏 VS + 直通 FS 作为 fallback
                vsRes.glsl = FULLSCREEN_VS;
                fsRes.glsl = R"GLSL(
#version 300 es
precision highp float;
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D uTexture;
void main() { fragColor = texture(uTexture, vTexCoord); }
)GLSL";
            }

            cp.program = linkProgram(vsRes.glsl, fsRes.glsl);
            if (!cp.program) continue;

            // 缓存 uniform 位置
            for (auto& u : sf.uniforms) {
                GLint loc = glGetUniformLocation(cp.program, u.name.c_str());
                if (loc >= 0) cp.uniformLocs[u.name] = loc;
                // 初始化为默认值
                if (std::holds_alternative<float>(u.defaultVal))
                    cp.floatUniforms[u.name] = std::get<float>(u.defaultVal);
            }
            // 内置 uniform 位置
            for (const char* name : {"uTexture","uTexelSize","uTimer","uFrameCount"}) {
                GLint loc = glGetUniformLocation(cp.program, name);
                if (loc >= 0) cp.uniformLocs[name] = loc;
            }

            eff.passes.push_back(std::move(cp));
        }
    }

    int id = nextId_++;
    effects_[id] = std::move(eff);
    LOGI("Effect added: id=%d name=%s passes=%zu",
         id, effects_[id].name.c_str(), effects_[id].passes.size());
    return id;
}

void ShaderRuntime::removeEffect(int id) {
    auto it = effects_.find(id);
    if (it == effects_.end()) return;
    for (auto& p : it->second.passes)
        if (p.program) glDeleteProgram(p.program);
    effects_.erase(it);
}

void ShaderRuntime::setEffectEnabled(int id, bool on) {
    auto it = effects_.find(id);
    if (it != effects_.end()) it->second.enabled = on;
}

void ShaderRuntime::setEffectOrder(int id, int order) {
    auto it = effects_.find(id);
    if (it != effects_.end()) it->second.order = order;
}

// ── Uniform 更新 ──────────────────────────────────────

void ShaderRuntime::setUniform(int id, const std::string& name, float v) {
    auto it = effects_.find(id);
    if (it == effects_.end()) return;
    for (auto& pass : it->second.passes)
        pass.floatUniforms[name] = v;
}

void ShaderRuntime::setUniform(int id, const std::string& name,
                                float x, float y, float z, float w) {
    auto it = effects_.find(id);
    if (it == effects_.end()) return;
    for (auto& pass : it->second.passes) {
        glUseProgram(pass.program);
        GLint loc = glGetUniformLocation(pass.program, name.c_str());
        if (loc >= 0) glUniform4f(loc, x, y, z, w);
    }
}

void ShaderRuntime::applyParams(int id,
    const std::unordered_map<std::string,float>& params) {
    for (auto& [k,v] : params) setUniform(id, k, v);
}

void ShaderRuntime::bindLUT(int id, GLuint lutTex, float strength) {
    auto it = effects_.find(id);
    if (it == effects_.end()) return;
    it->second.textures["uLUT"] = lutTex;
    for (auto& pass : it->second.passes)
        pass.floatUniforms["uLUTStrength"] = strength;
}

// ── 尺寸变化 ──────────────────────────────────────────

void ShaderRuntime::onResize(int w, int h) {
    fboPool_.resize(w, h);
}

// ── 主渲染流程 ────────────────────────────────────────

void ShaderRuntime::render(GLint backFBO, int w, int h) {
    if (effects_.empty()) return;

    ensureVAO();

    // 确保 FBO 已初始化
    if (fboPool_.width != w || fboPool_.height != h) {
        if (fboPool_.fbo[0] == 0) fboPool_.init(w, h);
        else fboPool_.resize(w, h);
    }

    // ── Step 1: 将 backbuffer 内容 blit 到 FBO[current] ──
    glBindFramebuffer(GL_READ_FRAMEBUFFER, backFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboPool_.fbo[fboPool_.current]);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // ── Step 2: 按 order 排序，依次执行 Effect ────────────
    std::vector<int> orderedIds;
    for (auto& [id, eff] : effects_)
        if (eff.enabled) orderedIds.push_back(id);
    std::sort(orderedIds.begin(), orderedIds.end(), [&](int a, int b){
        return effects_[a].order < effects_[b].order;
    });

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(vao_);

    for (int id : orderedIds) {
        auto& eff = effects_[id];
        for (auto& pass : eff.passes) {
            if (!pass.enabled) continue;
            executePass(pass,
                        fboPool_.srcTex(),
                        fboPool_.dstFBO(),
                        w, h);
            fboPool_.swap();
        }
    }

    // ── Step 3: 将最终结果 blit 回 backbuffer ─────────────
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fboPool_.fbo[fboPool_.current]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, backFBO);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, backFBO);
    ++frameCount_;
}

// ── 执行单个 Pass ─────────────────────────────────────

void ShaderRuntime::executePass(CompiledPass& pass,
                                 GLuint srcTex,
                                 GLuint dstFBO,
                                 int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
    glViewport(0, 0, w, h);
    glUseProgram(pass.program);

    // 绑定输入纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    auto it = pass.uniformLocs.find("uTexture");
    if (it != pass.uniformLocs.end())
        glUniform1i(it->second, 0);

    // 注入内置 uniform
    injectBuiltinUniforms(pass, w, h);

    // 用户 float uniform
    for (auto& [name, val] : pass.floatUniforms) {
        auto lit = pass.uniformLocs.find(name);
        if (lit != pass.uniformLocs.end())
            glUniform1f(lit->second, val);
    }

    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void ShaderRuntime::injectBuiltinUniforms(CompiledPass& pass, int w, int h) {
    // uTexelSize
    {
        auto it = pass.uniformLocs.find("uTexelSize");
        if (it != pass.uniformLocs.end())
            glUniform2f(it->second, 1.f/w, 1.f/h);
    }
    // uTimer（毫秒）
    {
        static auto t0 = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float ms = std::chrono::duration<float, std::milli>(now - t0).count();
        auto it = pass.uniformLocs.find("uTimer");
        if (it != pass.uniformLocs.end())
            glUniform1f(it->second, ms);
    }
    // uFrameCount
    {
        auto it = pass.uniformLocs.find("uFrameCount");
        if (it != pass.uniformLocs.end())
            glUniform1i(it->second, (int)frameCount_);
    }
}

// ── 查询接口 ──────────────────────────────────────────

std::vector<int> ShaderRuntime::effectIds() const {
    std::vector<int> ids;
    for (auto& [id, _] : effects_) ids.push_back(id);
    return ids;
}

const CompiledEffect* ShaderRuntime::getEffect(int id) const {
    auto it = effects_.find(id);
    return (it != effects_.end()) ? &it->second : nullptr;
}

std::vector<UniformDesc> ShaderRuntime::uniformsOf(int id) const {
    auto it = effects_.find(id);
    return (it != effects_.end()) ? it->second.uniforms : std::vector<UniformDesc>{};
}

// ── GL 编译工具 ───────────────────────────────────────

GLuint ShaderRuntime::compileShader(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Shader compile error (%s):\n%s",
             type == GL_VERTEX_SHADER ? "VS" : "FS", log);
        glDeleteShader(s); return 0;
    }
    return s;
}

GLuint ShaderRuntime::linkProgram(const std::string& vs, const std::string& fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER,   vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { glDeleteShader(v); glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048]; glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        LOGE("Program link error:\n%s", log);
        glDeleteProgram(p); return 0;
    }
    return p;
}

// ════════════════════════════════════════════════════════
// CompiledPass Uniform 辅助
// ════════════════════════════════════════════════════════

void CompiledPass::setFloat(const std::string& n, float v) {
    floatUniforms[n] = v;
}

void CompiledPass::applyUniforms() const {
    glUseProgram(program);
    for (auto& [name, val] : floatUniforms) {
        auto it = uniformLocs.find(name);
        if (it != uniformLocs.end())
            glUniform1f(it->second, val);
    }
}

} // namespace reshade
