/**
 * Sharpen.fx
 * 自适应锐化 shader（Unsharp Mask + CAS 简化版）
 * 兼容 ReShade 4.x / AndroidReShade
 */

// ============================================================
// Uniforms
// ============================================================
uniform float SharpenStrength <
    ui_type  = "slider";
    ui_label = "锐化强度 Sharpen Strength";
    ui_min   = 0.0; ui_max = 5.0; ui_step = 0.1;
> = 1.5;

uniform float SharpenRadius <
    ui_type  = "slider";
    ui_label = "锐化半径 Radius (pixels)";
    ui_min   = 0.5; ui_max = 3.0; ui_step = 0.1;
> = 1.0;

uniform float SharpenClamp <
    ui_type  = "slider";
    ui_label = "高频限制 Clamp";
    ui_min   = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.035;

// ============================================================
// Textures
// ============================================================
texture2D texColorBuffer : COLOR;
texture2D texDepthBuffer : DEPTH;

sampler2D samplerColor = sampler_state
{
    Texture = <texColorBuffer>;
    MinFilter = LINEAR; MagFilter = LINEAR;
    AddressU = CLAMP; AddressV = CLAMP;
};

// ============================================================
// Helpers
// ============================================================
#define PIXEL_SIZE float2(1.0 / BUFFER_WIDTH, 1.0 / BUFFER_HEIGHT)

float GetLuma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// ============================================================
// VS
// ============================================================
void VS_Sharpen(
    in  uint   id       : SV_VertexID,
    out float4 pos      : SV_Position,
    out float2 uv       : TEXCOORD
) {
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos  = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// ============================================================
// PS - Unsharp Mask
// ============================================================
float4 PS_Sharpen(
    in float4 pos : SV_Position,
    in float2 uv  : TEXCOORD
) : SV_Target
{
    float2 off = PIXEL_SIZE * SharpenRadius;

    // 5-tap cross 模糊（近似高斯）
    float4 center = tex2D(samplerColor, uv);
    float4 blur   = center * 4.0
                  + tex2D(samplerColor, uv + float2( off.x,  0.0))
                  + tex2D(samplerColor, uv + float2(-off.x,  0.0))
                  + tex2D(samplerColor, uv + float2( 0.0,  off.y))
                  + tex2D(samplerColor, uv + float2( 0.0, -off.y));
    blur /= 8.0;

    // Unsharp Mask = original + strength * (original - blur)
    float4 diff   = center - blur;
    float  lumaD  = GetLuma(diff.rgb);
    float  clampF = clamp(lumaD, -SharpenClamp, SharpenClamp) / max(lumaD, 1e-5);

    float4 result = center + diff * (SharpenStrength * clampF);
    return float4(saturate(result.rgb), center.a);
}

// ============================================================
// Technique
// ============================================================
technique Sharpen <
    ui_label = "自适应锐化 (Adaptive Sharpen)";
>
{
    pass
    {
        VertexShader = VS_Sharpen;
        PixelShader  = PS_Sharpen;
    }
}
