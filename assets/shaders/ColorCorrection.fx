/**
 * ColorCorrection.fx
 * 色彩校正 shader - 兼容 ReShade 4.x / AndroidReShade
 *
 * 功能：亮度 / 对比度 / 饱和度 / Gamma 四合一
 */

// ============================================================
// Uniforms（由 AndroidReShade 控制面板自动绑定）
// ============================================================
uniform float Brightness <
    ui_type = "slider";
    ui_label = "亮度 Brightness";
    ui_min = -1.0; ui_max = 1.0; ui_step = 0.01;
> = 0.0;

uniform float Contrast <
    ui_type = "slider";
    ui_label = "对比度 Contrast";
    ui_min = 0.0; ui_max = 3.0; ui_step = 0.01;
> = 1.0;

uniform float Saturation <
    ui_type = "slider";
    ui_label = "饱和度 Saturation";
    ui_min = 0.0; ui_max = 3.0; ui_step = 0.01;
> = 1.0;

uniform float Gamma <
    ui_type = "slider";
    ui_label = "Gamma 校正";
    ui_min = 0.1; ui_max = 3.0; ui_step = 0.01;
> = 1.0;

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

// ============================================================
// Vertex Shader（全屏三角形，ReShade 标准）
// ============================================================
void VS_PostProcess(
    in  uint   id       : SV_VertexID,
    out float4 position : SV_Position,
    out float2 texcoord : TEXCOORD
) {
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position   = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// ============================================================
// Pixel Shader
// ============================================================
float4 PS_ColorCorrection(
    in float4 position : SV_Position,
    in float2 texcoord : TEXCOORD
) : SV_Target
{
    float4 color = tex2D(samplerColor, texcoord);

    // --- 1. 亮度偏移 ---
    color.rgb += Brightness;

    // --- 2. 对比度（以 0.5 为中心缩放）---
    color.rgb = (color.rgb - 0.5) * Contrast + 0.5;

    // --- 3. 饱和度（Luma 混合）---
    float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    color.rgb  = lerp(float3(luma, luma, luma), color.rgb, Saturation);

    // --- 4. Gamma 校正 ---
    color.rgb = pow(saturate(color.rgb), 1.0 / Gamma);

    return float4(saturate(color.rgb), color.a);
}

// ============================================================
// Technique
// ============================================================
technique ColorCorrection <
    ui_label = "色彩校正 (Color Correction)";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader  = PS_ColorCorrection;
    }
}
