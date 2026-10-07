// The Scene view's world grid, drawn into the scene target under the sprites. Lines are antialiased per pixel, so they stay one pixel wide at any zoom and target size
struct PushData
{
    float2 Center;
    float2 HalfExtent;
    float Spacing;
    float MinorFade;
    float2 Padding;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 World : TEXCOORD0;
};

// One triangle over the whole target, with clip space Y up as the world's is
[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    const float2 l_Clip = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2)) * 2.0 - 1.0;

    VertexOutput l_Output;
    l_Output.Position = float4(l_Clip, 0.0, 1.0);
    l_Output.World = g_Push.Center + l_Clip * g_Push.HalfExtent;

    return l_Output;
}

// 1 on a line every spacing units, falling to 0 a pixel away
float GridLine(float2 world, float spacing)
{
    const float2 l_Coordinate = world / spacing;
    const float2 l_Distance = abs(frac(l_Coordinate - 0.5) - 0.5) / fwidth(l_Coordinate);

    return 1.0 - min(min(l_Distance.x, l_Distance.y), 1.0);
}

// Minor lines fade in as their cells grow, major lines every ten, and the axes in red for X and green for Y. The colours are linear, as the scene target is, and match sRGB (0.9, 0.3, 0.3) and (0.3, 0.85, 0.3)
[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    const float l_Minor = GridLine(input.World, g_Push.Spacing) * g_Push.MinorFade;
    const float l_Major = GridLine(input.World, g_Push.Spacing * 10.0);
    const float2 l_Axis = 1.0 - min(abs(input.World) / (fwidth(input.World) * 1.5), 1.0);

    float4 l_Color = float4(1.0, 1.0, 1.0, max(l_Minor * 0.1, l_Major * 0.25));
    l_Color = lerp(l_Color, float4(0.787, 0.073, 0.073, 0.8), l_Axis.y);
    l_Color = lerp(l_Color, float4(0.073, 0.692, 0.073, 0.8), l_Axis.x);

    return l_Color;
}