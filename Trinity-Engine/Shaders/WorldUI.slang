// A unit quad showing the document's texture unlit, tested against opaque depth, writing none
#include "Bindless.slang"
#include "Color.slang"

// Laid out as Renderer3D.cpp writes it
struct PushData
{
    float4 Transform[4];
    DescriptorHandle<Texture2D> Texture;
    DescriptorHandle<SamplerState> Sampler;
    uint Entity;
    uint Padding;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

// A triangle strip. The document's top row is at V = 0, so the quad's top takes it
[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    const float2 l_Corner = float2(float(vertexIndex & 1), float(vertexIndex >> 1));
    const float2 l_Local = l_Corner - 0.5;

    VertexOutput l_Output;
    l_Output.Position = g_Push.Transform[0] * l_Local.x + g_Push.Transform[1] * l_Local.y + g_Push.Transform[3];
    l_Output.TexCoord = float2(l_Corner.x, 1.0 - l_Corner.y);

    return l_Output;
}

float4 SampleDocument(float2 texCoord)
{
    Texture2D l_Texture = g_Push.Texture;
    SamplerState l_Sampler = g_Push.Sampler;

    return DocumentToLinear(l_Texture.Sample(l_Sampler, texCoord));
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    return SampleDocument(input.TexCoord);
}

struct PickOutput
{
    float4 Color : SV_Target0;
    uint Entity : SV_Target1;
};

// The entity ID is written only where the document drew, so clear parts pick what is behind
[shader("fragment")]
PickOutput PickPixelMain(VertexOutput input)
{
    PickOutput l_Output;
    l_Output.Color = SampleDocument(input.TexCoord);
    if (l_Output.Color.a <= 0.0)
    {
        discard;
    }

    l_Output.Entity = g_Push.Entity;

    return l_Output;
}