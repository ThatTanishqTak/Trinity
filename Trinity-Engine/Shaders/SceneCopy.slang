// Copies the scene target to the output one texel per pixel, from one triangle that covers the whole output
#include "Bindless.slang"

struct PushData
{
    DescriptorHandle<Texture2D> Scene;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

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

    return l_Scene.Load(int3(int2(position.xy), 0));
}