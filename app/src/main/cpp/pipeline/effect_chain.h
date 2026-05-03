#pragma once
#include <GLES3/gl3.h>

struct EffectParams {
    float brightness{0.0f};   // -1.0 ~ 1.0
    float contrast{1.0f};     //  0.0 ~ 3.0
    float saturation{1.0f};   //  0.0 ~ 3.0
    float sharpness{0.0f};    //  0.0 ~ 5.0
    float vignette{0.0f};     //  0.0 ~ 1.0
    float gamma{1.0f};        //  0.1 ~ 3.0
    float lutStrength{1.0f};  //  0.0 ~ 1.0
};

struct EffectChainImpl;

class EffectChain {
public:
    explicit EffectChain(int width, int height);
    ~EffectChain();

    void render(GLint originalFBO);
    void resize(int w, int h);
    void setParams(const EffectParams& params);
    void loadShader(const char* path);
    void loadLUT(const char* path);
    void loadDefaultEffects();

private:
    void createFBOs();
    void buildBuiltinShaders();

    EffectChainImpl* impl{nullptr};
};
