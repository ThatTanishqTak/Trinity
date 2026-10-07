// Dear ImGui's vertices read from the upload ring, its textures and sampler through bindless indices
#include "Bindless.slang"
#include "Color.slang"

struct PushData
{
    DescriptorHandle<ByteAddressBuffer> Vertices;
    DescriptorHandle<Texture2D> Texture;
    DescriptorHandle<SamplerState> Sampler;
    uint VertexOffset;
    uint Flags;
    float2 Scale;
    float2 Translate;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

// ImDrawVert: a float2 position, a float2 texture coordinate and an RGBA8 colour with red in the low byte
static const uint c_VertexStride = 20;

// ImGui draws in gamma space, so what an sRGB texture decodes to is encoded again
static const uint c_EncodeSrgb = 1;

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
};

[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    ByteAddressBuffer l_Vertices = g_Push.Vertices;
    const uint l_Address = g_Push.VertexOffset + vertexIndex * c_VertexStride;
    const uint l_Color = l_Vertices.Load(l_Address + 16);

    VertexOutput l_Output;
    l_Output.Position = float4(l_Vertices.Load<float2>(l_Address) * g_Push.Scale + g_Push.Translate, 0.0, 1.0);
    l_Output.TexCoord = l_Vertices.Load<float2>(l_Address + 8);
    l_Output.Color = float4(float(l_Color & 0xFF), float((l_Color >> 8) & 0xFF), float((l_Color >> 16) & 0xFF), float(l_Color >> 24)) / 255.0;

    return l_Output;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    Texture2D l_Texture = g_Push.Texture;
    SamplerState l_Sampler = g_Push.Sampler;

    float4 l_Texel = l_Texture.Sample(l_Sampler, input.TexCoord);
    if ((g_Push.Flags & c_EncodeSrgb) != 0)
    {
        l_Texel.rgb = LinearToSrgb(l_Texel.rgb);
    }

    return input.Color * l_Texel;
}