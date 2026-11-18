// One triangle at the far depth, so the equal depth test passes only where no surface was drawn
#include "Bindless.slang"

// Laid out as Renderer3D.cpp writes it. The matrix takes a near-plane point to the eye's direction
struct PushData
{
    float4 InverseViewProjection[4];
    DescriptorHandle<TextureCube> Environment;
    DescriptorHandle<SamplerState> Sampler;
    float Intensity;
    float Cosine;
    float Sine;
    float Padding;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 Clip : TEXCOORD0;
};

// Depth 0 is the far end of reversed depth
[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    const float2 l_Corner = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    const float2 l_Clip = l_Corner * 2.0 - 1.0;

    VertexOutput l_Output;
    l_Output.Position = float4(l_Clip, 0.0, 1.0);
    l_Output.Clip = l_Clip;

    return l_Output;
}

// The near plane is at depth 1, so its point through the pixel lies along the eye ray
float3 SampleSky(float2 clip)
{
    const float4 l_Near = g_Push.InverseViewProjection[0] * clip.x + g_Push.InverseViewProjection[1] * clip.y + g_Push.InverseViewProjection[2] + g_Push.InverseViewProjection[3];
    const float3 l_Direction = normalize(l_Near.xyz / l_Near.w);
    const float3 l_Turned = float3(g_Push.Cosine * l_Direction.x + g_Push.Sine * l_Direction.z, l_Direction.y, g_Push.Cosine * l_Direction.z - g_Push.Sine * l_Direction.x);
    TextureCube l_Environment = g_Push.Environment;
    SamplerState l_Sampler = g_Push.Sampler;

    return l_Environment.SampleLevel(l_Sampler, l_Turned, 0.0).rgb * g_Push.Intensity;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    return float4(SampleSky(input.Clip), 1.0);
}

struct PickOutput
{
    float4 Color : SV_Target0;
    uint Entity : SV_Target1;
};

// No entity goes into the ID target, so a click on the sky picks nothing
[shader("fragment")]
PickOutput PickPixelMain(VertexOutput input)
{
    PickOutput l_Output;
    l_Output.Color = float4(SampleSky(input.Clip), 1.0);
    l_Output.Entity = 0;

    return l_Output;
}