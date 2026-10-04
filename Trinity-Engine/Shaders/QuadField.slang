// Quads written into the upload ring every frame, each vertex a float2 position and an RGBA8 colour read through a bindless index
#include "Bindless.slang"

struct PushData
{
    DescriptorHandle<ByteAddressBuffer> Vertices;
    uint VertexOffset;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

static const uint c_VertexStride = 12;

struct VertexOutput
{
    float4 Position : SV_Position;
    float4 Color : COLOR0;
};

[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    ByteAddressBuffer l_Vertices = g_Push.Vertices;
    const uint l_Address = g_Push.VertexOffset + vertexIndex * c_VertexStride;
    const uint l_Color = l_Vertices.Load(l_Address + 8);

    VertexOutput l_Output;
    l_Output.Position = float4(l_Vertices.Load<float2>(l_Address), 0.0, 1.0);
    l_Output.Color = float4(float(l_Color & 0xFF), float((l_Color >> 8) & 0xFF), float((l_Color >> 16) & 0xFF), float(l_Color >> 24)) / 255.0;

    return l_Output;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    return input.Color;
}