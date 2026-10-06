// Sprites drawn as instanced quads. Each instance is read from the upload ring and names its own texture, so sprites with different textures share one draw
#include "Bindless.slang"

// The view-projection as four columns, as glm stores it, so no matrix layout convention is involved
struct PushData
{
    float4 ViewProjection[4];
    DescriptorHandle<ByteAddressBuffer> Instances;
    DescriptorHandle<SamplerState> LinearSampler;
    DescriptorHandle<SamplerState> NearestSampler;
    uint InstanceOffset;
    uint Padding;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

// The rows of a 3x4 world matrix, then the UV rectangle as 16-bit UNORMs, minimum U and V before maximum U and V, the tint as RGBA8 with red in the low byte, and the texture's bindless index with its filter in the top bit
static const uint c_InstanceStride = 64;
static const uint c_NearestBit = 0x80000000;

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
    nointerpolation uint Texture : TEXCOORD1;
};

float4 UnpackColor(uint color)
{
    return float4(float(color & 0xFF), float((color >> 8) & 0xFF), float((color >> 16) & 0xFF), float(color >> 24)) / 255.0;
}

// Four vertices as a triangle strip over a unit quad centred on the origin: bottom left, bottom right, top left, top right
[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID, uint instanceIndex: SV_InstanceID)
{
    ByteAddressBuffer l_Instances = g_Push.Instances;
    const uint l_Address = g_Push.InstanceOffset + instanceIndex * c_InstanceStride;

    const float4 l_Row0 = l_Instances.Load<float4>(l_Address);
    const float4 l_Row1 = l_Instances.Load<float4>(l_Address + 16);
    const float4 l_Row2 = l_Instances.Load<float4>(l_Address + 32);
    const uint2 l_UV = l_Instances.Load<uint2>(l_Address + 48);
    const uint l_Color = l_Instances.Load(l_Address + 56);
    const uint l_Texture = l_Instances.Load(l_Address + 60);

    const float2 l_Corner = float2(float(vertexIndex & 1), float(vertexIndex >> 1));
    const float4 l_Local = float4(l_Corner - 0.5, 0.0, 1.0);
    const float4 l_World = float4(dot(l_Row0, l_Local), dot(l_Row1, l_Local), dot(l_Row2, l_Local), 1.0);

    // V runs down the image, so the top of the quad takes the rectangle's minimum V
    const float4 l_Rect = float4(float(l_UV.x & 0xFFFF), float(l_UV.x >> 16), float(l_UV.y & 0xFFFF), float(l_UV.y >> 16)) / 65535.0;

    VertexOutput l_Output;
    l_Output.Position = g_Push.ViewProjection[0] * l_World.x + g_Push.ViewProjection[1] * l_World.y + g_Push.ViewProjection[2] * l_World.z + g_Push.ViewProjection[3];
    l_Output.TexCoord = float2(lerp(l_Rect.x, l_Rect.z, l_Corner.x), lerp(l_Rect.w, l_Rect.y, l_Corner.y));
    l_Output.Color = UnpackColor(l_Color);
    l_Output.Texture = l_Texture;

    return l_Output;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    // Neighbouring sprites in one draw can name different textures and filters, so both indices are marked non-uniform
    const DescriptorHandle<Texture2D> l_TextureHandle = DescriptorHandle<Texture2D>(uint2(input.Texture & ~c_NearestBit, 0));
    const DescriptorHandle<SamplerState> l_SamplerHandle = (input.Texture & c_NearestBit) != 0 ? g_Push.NearestSampler : g_Push.LinearSampler;
    Texture2D l_Texture = nonuniform(l_TextureHandle);
    SamplerState l_Sampler = nonuniform(l_SamplerHandle);

    return input.Color * l_Texture.Sample(l_Sampler, input.TexCoord);
}