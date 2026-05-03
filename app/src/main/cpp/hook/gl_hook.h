#pragma once
#include "../pipeline/effect_chain.h"

/**
 * GL Hook 公开接口
 */
void installGLHook();
void setGLHookEnabled(bool enabled);
void updateEffectParams(const EffectParams& params);
void loadShaderFile(const char* path);
void loadLUTFile(const char* path);
