// The first triangle, its vertices read from a buffer through a bindless index
#include "Bindless.slang"

struct PushData
{
    DescriptorHandle<ByteAddressBuffer> Vertices;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

// Each vertex is a float4 position followed by a float4 colour
static const uint c_VertexStride = 32;

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 Color : COLOR0;
};

[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    ByteAddressBuffer l_Vertices = g_Push.Vertices;

    VertexOutput l_Output;
    l_Output.Position = l_Vertices.Load<float4>(vertexIndex * c_VertexStride);
    l_Output.Color = l_Vertices.Load<float4>(vertexIndex * c_VertexStride + 16).rgb;

    return l_Output;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    return float4(input.Color, 1.0);
}