// A screen-space quad that samples a texture, both the texture and the sampler read through bindless indices
#include "Bindless.slang"

struct PushData
{
    DescriptorHandle<Texture2D> Texture;
    DescriptorHandle<SamplerState> Sampler;
    float4 Bounds;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

// Four vertices drawn as a triangle strip: bottom left, bottom right, top left, top right. Bounds holds the left, bottom, right and top edges in clip space
[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    const float2 l_Corner = float2(float(vertexIndex & 1), float(vertexIndex >> 1));

    VertexOutput l_Output;
    l_Output.Position = float4(lerp(g_Push.Bounds.xy, g_Push.Bounds.zw, l_Corner), 0.0, 1.0);
    l_Output.TexCoord = float2(l_Corner.x, 1.0 - l_Corner.y);

    return l_Output;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    Texture2D l_Texture = g_Push.Texture;
    SamplerState l_Sampler = g_Push.Sampler;

    return l_Texture.Sample(l_Sampler, input.TexCoord);
}