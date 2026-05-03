#pragma once
#include <string>
#include <GLES3/gl3.h>

class LutLoader {
public:
    // 加载 .cube 或 .png LUT 文件，返回 GL_TEXTURE_3D 纹理 id
    GLuint load(const std::string& path);
};
