/**
 * Vignette.fx
 * 暗角 / 渐晕效果
 * 兼容 ReShade 4.x / AndroidReShade
 */

uniform float VignetteRadius <
    ui_type  = "slider";
    ui_label = "暗角半径 Radius";
    ui_min   = 0.0; ui_max = 1.5; ui_step = 0.01;
> = 0.85;

uniform float VignetteSoftness <
    ui_type  = "slider";
    ui_label = "边缘柔和度 Softness";
    ui_min   = 0.01; ui_max = 1.0; ui_step = 0.01;
> = 0.45;

uniform float VignetteStrength <
    ui_type  = "slider";
    ui_label = "暗角强度 Strength";
    ui_min   = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.6;

uniform float3 VignetteColor <
    ui_type  = "color";
    ui_label = "暗角颜色 Color";
> = float3(0.0, 0.0, 0.0);

texture2D texColorBuffer : COLOR;
sampler2D samplerColor = sampler_state
{
    Texture = <texColorBuffer>;
    MinFilter = LINEAR; MagFilter = LINEAR;
    AddressU = CLAMP; AddressV = CLAMP;
};

void VS_Vignette(
    in  uint   id  : SV_VertexID,
    out float4 pos : SV_Position,
    out float2 uv  : TEXCOORD
) {
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos  = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_Vignette(
    in float4 pos : SV_Position,
    in float2 uv  : TEXCOORD
) : SV_Target
{
    float4 color = tex2D(samplerColor, uv);

    // 以中心为圆心，计算椭圆距离
    float2 center = uv - 0.5;
    // 调整宽高比
    center.x *= BUFFER_WIDTH / float(BUFFER_HEIGHT);
    float dist = length(center);

    float vignette = smoothstep(VignetteRadius, VignetteRadius - VignetteSoftness, dist);
    float3 result  = lerp(VignetteColor, color.rgb, lerp(1.0, vignette, VignetteStrength));

    return float4(result, color.a);
}

technique Vignette <
    ui_label = "暗角效果 (Vignette)";
>
{
    pass
    {
        VertexShader = VS_Vignette;
        PixelShader  = PS_Vignette;
    }
}
