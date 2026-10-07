// Turns the linear scene into the display's image, one scene texel per pixel from one triangle over the whole target: the exposure, the tone curve, a clamp and the sRGB encoding, as ApplyToneMapping does on the CPU
#include "Bindless.slang"
#include "Color.slang"

struct PushData
{
    DescriptorHandle<Texture2D> Scene;
    float Exposure;
    uint Curve;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

// Tonemapper::PBRNeutral
static const uint c_CurvePBRNeutral = 1;

// Khronos PBR Neutral, as ApplyPBRNeutral has it
float3 PBRNeutral(float3 color)
{
    const float l_StartCompression = 0.8 - 0.04;
    const float l_Desaturation = 0.15;

    const float l_Min = min(color.r, min(color.g, color.b));
    const float l_Offset = l_Min < 0.08 ? l_Min - 6.25 * l_Min * l_Min : 0.04;
    color -= l_Offset;

    const float l_Peak = max(color.r, max(color.g, color.b));
    if (l_Peak < l_StartCompression)
    {
        return color;
    }

    const float l_Range = 1.0 - l_StartCompression;
    const float l_NewPeak = 1.0 - l_Range * l_Range / (l_Peak + l_Range - l_StartCompression);
    color *= l_NewPeak / l_Peak;

    const float l_Blend = 1.0 - 1.0 / (l_Desaturation * (l_Peak - l_NewPeak) + 1.0);

    return lerp(color, float3(l_NewPeak, l_NewPeak, l_NewPeak), l_Blend);
}

[shader("vertex")]
float4 VertexMain(uint vertexIndex: SV_VertexID) : SV_Position
{
    const float2 l_Corner = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));

    return float4(l_Corner * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 PixelMain(float4 position: SV_Position) : SV_Target
{
    Texture2D l_Scene = g_Push.Scene;
    float3 l_Color = max(l_Scene.Load(int3(int2(position.xy), 0)).rgb, float3(0.0, 0.0, 0.0)) * g_Push.Exposure;
    if (g_Push.Curve == c_CurvePBRNeutral)
    {
        l_Color = PBRNeutral(l_Color);
    }

    return float4(LinearToSrgb(saturate(l_Color)), 1.0);
}