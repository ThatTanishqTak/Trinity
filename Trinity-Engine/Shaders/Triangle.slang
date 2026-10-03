// The first triangle. Positions and colours come from the vertex index until the vertex buffer is read through a bindless index

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 Color : COLOR0;
};

// Counter-clockwise with Y up
static const float2 c_Positions[3] = { float2(0.0, 0.5), float2(-0.5, -0.5), float2(0.5, -0.5) };
static const float3 c_Colors[3] = { float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0) };

[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex : SV_VertexID)
{
    VertexOutput l_Output;
    l_Output.Position = float4(c_Positions[vertexIndex], 0.0, 1.0);
    l_Output.Color = c_Colors[vertexIndex];

    return l_Output;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    return float4(input.Color, 1.0);
}