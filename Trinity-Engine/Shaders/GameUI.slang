// RmlUi's vertices read from the upload ring, its textures and sampler through bindless indices, drawn in the display space of the view, with colours multiplied by their alpha
#include "Bindless.slang"
#include "Color.slang"

// The first, second and fourth columns of the transform from the view's pixels to clip space, which is all a vertex at depth 0 needs
struct PushData
{
    float4 Column0;
    float4 Column1;
    float4 Column3;
    DescriptorHandle<ByteAddressBuffer> Vertices;
    DescriptorHandle<Texture2D> Texture;
    DescriptorHandle<SamplerState> Sampler;
    float2 Translation;
    uint VertexOffset;
    uint Flags;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

// Rml::Vertex: a float2 position, an RGBA8 colour already multiplied by its alpha with red in the low byte, and a float2 texture coordinate
static const uint c_VertexStride = 20;

static const uint c_Textured = 1;
// The view's display target holds sRGB-encoded values, so what an sRGB texture decodes to is encoded again
static const uint c_EncodeSrgb = 2;
// A project's texture has straight alpha, where RmlUi's own are multiplied by it already
static const uint c_Premultiply = 4;

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
    const float2 l_Position = l_Vertices.Load<float2>(l_Address) + g_Push.Translation;
    const uint l_Color = l_Vertices.Load(l_Address + 8);

    VertexOutput l_Output;
    l_Output.Position = g_Push.Column0 * l_Position.x + g_Push.Column1 * l_Position.y + g_Push.Column3;
    l_Output.TexCoord = l_Vertices.Load<float2>(l_Address + 12);
    l_Output.Color = float4(float(l_Color & 0xFF), float((l_Color >> 8) & 0xFF), float((l_Color >> 16) & 0xFF), float(l_Color >> 24)) / 255.0;

    return l_Output;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    if ((g_Push.Flags & c_Textured) == 0)
    {
        return input.Color;
    }

    Texture2D l_Texture = g_Push.Texture;
    SamplerState l_Sampler = g_Push.Sampler;

    float4 l_Texel = l_Texture.Sample(l_Sampler, input.TexCoord);
    if ((g_Push.Flags & c_EncodeSrgb) != 0)
    {
        l_Texel.rgb = LinearToSrgb(l_Texel.rgb);
    }

    if ((g_Push.Flags & c_Premultiply) != 0)
    {
        l_Texel.rgb *= l_Texel.a;
    }

    return input.Color * l_Texel;
}