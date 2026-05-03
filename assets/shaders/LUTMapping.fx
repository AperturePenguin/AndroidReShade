/**
 * LUTMapping.fx
 * LUT 颜色映射 shader
 * 支持：32x32x32 3D LUT（.cube 转换后）或 512x512 Hald CLUT (.png)
 * 兼容 ReShade 4.x / AndroidReShade
 */

// ============================================================
// Uniforms
// ============================================================
uniform float LUTStrength <
    ui_type  = "slider";
    ui_label = "LUT 强度 Blend Strength";
    ui_min   = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 1.0;

// LUT 尺寸（3D 纹理维度，通常 32）
#ifndef LUT_SIZE
#define LUT_SIZE 32
#endif

// ============================================================
// Textures & Samplers
// ============================================================
texture2D texColorBuffer : COLOR;
sampler2D samplerColor = sampler_state
{
    Texture = <texColorBuffer>;
    MinFilter = LINEAR; MagFilter = LINEAR;
    AddressU = CLAMP; AddressV = CLAMP;
};

// 3D LUT 纹理（由 AndroidReShade 的 LutLoader 模块注入）
texture3D texLUT3D <
    source = "lut_current.3d";   // 运行时由 C++ 注入
>
{
    Width  = LUT_SIZE;
    Height = LUT_SIZE;
    Depth  = LUT_SIZE;
    Format = RGBA8;
};

sampler3D samplerLUT = sampler_state
{
    Texture    = <texLUT3D>;
    MinFilter  = LINEAR;
    MagFilter  = LINEAR;
    MipFilter  = NONE;
    AddressU   = CLAMP;
    AddressV   = CLAMP;
    AddressW   = CLAMP;
};

// ============================================================
// Helpers
// ============================================================
float3 ApplyLUT3D(sampler3D lut, float3 color, float lutSize)
{
    // 将 [0,1] 颜色空间映射到 LUT 纹理采样坐标
    // 每格占 1/LUT_SIZE，加 0.5/LUT_SIZE 避免边界问题
    float scale  = (lutSize - 1.0) / lutSize;
    float offset = 0.5 / lutSize;
    float3 uvw   = color * scale + offset;
    return tex3D(lut, uvw).rgb;
}

// ============================================================
// VS
// ============================================================
void VS_LUTMapping(
    in  uint   id  : SV_VertexID,
    out float4 pos : SV_Position,
    out float2 uv  : TEXCOORD
) {
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos  = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// ============================================================
// PS
// ============================================================
float4 PS_LUTMapping(
    in float4 pos : SV_Position,
    in float2 uv  : TEXCOORD
) : SV_Target
{
    float4 color    = tex2D(samplerColor, uv);
    float3 lutColor = ApplyLUT3D(samplerLUT, color.rgb, float(LUT_SIZE));
    float3 result   = lerp(color.rgb, lutColor, LUTStrength);
    return float4(result, color.a);
}

// ============================================================
// Technique
// ============================================================
technique LUTMapping <
    ui_label = "LUT 颜色映射 (LUT Mapping)";
>
{
    pass
    {
        VertexShader = VS_LUTMapping;
        PixelShader  = PS_LUTMapping;
    }
}
