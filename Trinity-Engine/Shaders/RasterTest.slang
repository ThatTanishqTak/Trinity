// Sandbox's raster state test: one triangle in clip space and one colour, all from the push constants
struct PushData
{
    float4 Positions[3];
    float4 Color;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

[shader("vertex")]
float4 VertexMain(uint vertexIndex: SV_VertexID) : SV_Position
{
    return g_Push.Positions[vertexIndex];
}

[shader("fragment")]
float4 PixelMain(float4 position: SV_Position) : SV_Target
{
    return g_Push.Color;
}