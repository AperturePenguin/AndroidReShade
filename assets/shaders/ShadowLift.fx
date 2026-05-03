/**
 * ShadowLift.fx
 * 暗部增强 / 阴影提升 shader
 * 兼容 ReShade 4.x / AndroidReShade
 *
 * 功能：
 *   - 暗部非线性提亮（保持高光）
 *   - 中间调色彩偏移
 *   - 暗部饱和度增强（电影感）
 */

// ============================================================
// Uniforms
// ============================================================
uniform float ShadowLiftAmount <
    ui_type  = "slider";
    ui_label = "暗部提升量 Shadow Lift";
    ui_min   = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.2;

uniform float ShadowGamma <
    ui_type  = "slider";
    ui_label = "暗部 Gamma";
    ui_min   = 0.3; ui_max = 2.0; ui_step = 0.01;
> = 0.7;

uniform float MidtoneSaturation <
    ui_type  = "slider";
    ui_label = "中间调饱和度 Midtone Saturation";
    ui_min   = 0.0; ui_max = 2.0; ui_step = 0.01;
> = 1.2;

uniform float3 ShadowTint <
    ui_type  = "color";
    ui_label = "暗部色调 Shadow Tint (RGB)";
> = float3(0.05, 0.07, 0.12);   // 默认冷蓝调，电影感

uniform float ShadowTintStrength <
    ui_type  = "slider";
    ui_label = "暗部色调强度";
    ui_min   = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.25;

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
// VS
// ============================================================
void VS_ShadowLift(
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
float4 PS_ShadowLift(
    in float4 pos : SV_Position,
    in float2 uv  : TEXCOORD
) : SV_Target
{
    float4 color = tex2D(samplerColor, uv);
    float3 c     = color.rgb;

    // --- 1. 计算亮度蒙版（0=最暗, 1=最亮）---
    float luma    = dot(c, float3(0.2126, 0.7152, 0.0722));
    float darkMsk = 1.0 - smoothstep(0.0, 0.5, luma);   // 越暗越强
    float midMsk  = 1.0 - abs(luma - 0.5) * 2.0;        // 中间调蒙版

    // --- 2. 暗部提升（非线性曲线）---
    float3 lifted = pow(c + ShadowLiftAmount * (1.0 - c), 1.0 / ShadowGamma);
    c = lerp(c, lifted, darkMsk);

    // --- 3. 暗部色调（冷暖偏移）---
    c = lerp(c, c * (1.0 - ShadowTintStrength) + ShadowTint * ShadowTintStrength, darkMsk);

    // --- 4. 中间调饱和度增强 ---
    float lumaNew = dot(c, float3(0.2126, 0.7152, 0.0722));
    float3 gray   = float3(lumaNew, lumaNew, lumaNew);
    c = lerp(gray, c, lerp(1.0, MidtoneSaturation, midMsk));

    return float4(saturate(c), color.a);
}

// ============================================================
// Technique
// ============================================================
technique ShadowLift <
    ui_label = "暗部增强 (Shadow Lift)";
>
{
    pass
    {
        VertexShader = VS_ShadowLift;
        PixelShader  = PS_ShadowLift;
    }
}
