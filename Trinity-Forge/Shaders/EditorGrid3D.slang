// The Viewport's ground grid in 3D: the plane Y = 0, drawn over the scene before its resolve and tested against the scene's depth, so meshes hide it. Each pixel's eye ray meets the plane, and the depth written is the plane's there. Lines are antialiased per pixel and fade out where their cells shrink to a few pixels, so they never shimmer into a grey haze
struct PushData
{
    // The inverse of the view projection by column, which takes a pixel back to the world, and the view projection's third and fourth rows, which give the plane's depth
    float4 InverseColumns[4];
    float4 DepthRow;
    float4 WRow;
    float3 Eye;
    float Spacing;
    float MinorFade;
    float FadeDistance;
    float2 Padding;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 Clip : TEXCOORD0;
};

struct PixelOutput
{
    float4 Color : SV_Target;
    float Depth : SV_Depth;
};

// One triangle over the whole target
[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    const float2 l_Clip = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2)) * 2.0 - 1.0;

    VertexOutput l_Output;
    l_Output.Position = float4(l_Clip, 0.0, 1.0);
    l_Output.Clip = l_Clip;

    return l_Output;
}

float3 Unproject(float2 clip, float depth)
{
    const float4 l_World = g_Push.InverseColumns[0] * clip.x + g_Push.InverseColumns[1] * clip.y + g_Push.InverseColumns[2] * depth + g_Push.InverseColumns[3];

    return l_World.xyz / l_World.w;
}

// 1 on a line every spacing units, falling to 0 a pixel away, and fading out as the cells fall from ten pixels across to two
float GridLine(float2 world, float spacing)
{
    const float2 l_Coordinate = world / spacing;
    const float2 l_Width = fwidth(l_Coordinate);
    const float2 l_Distance = abs(frac(l_Coordinate - 0.5) - 0.5) / l_Width;
    const float l_Fade = saturate((0.5 - max(l_Width.x, l_Width.y)) / 0.4);

    return (1.0 - min(min(l_Distance.x, l_Distance.y), 1.0)) * l_Fade;
}

// Three sizes of cell, each ten times the last. The smallest fades in as the eye comes down towards the plane, so the sizes hand over smoothly from one power of ten to the next. The X axis is red and the Z axis blue, in linear colours that match sRGB (0.9, 0.3, 0.3) and (0.3, 0.45, 0.95)
[shader("fragment")]
PixelOutput PixelMain(VertexOutput input)
{
    // Reversed depth, so 1 is the near plane, and 0.5 is twice as far, where the projection has no far plane. A ray that never meets the plane goes on to the end with no colour, since derivatives need every pixel of a quad
    const float3 l_Near = Unproject(input.Clip, 1.0);
    const float3 l_Direction = Unproject(input.Clip, 0.5) - l_Near;
    const bool l_Meets = abs(l_Direction.y) > 1e-8 && l_Near.y * l_Direction.y < 0.0;
    const float3 l_World = l_Meets ? l_Near - l_Direction * (l_Near.y / l_Direction.y) : l_Near;
    const float2 l_Plane = l_World.xz;

    const float l_Smallest = GridLine(l_Plane, g_Push.Spacing) * 0.5 * g_Push.MinorFade;
    const float l_Middle = GridLine(l_Plane, g_Push.Spacing * 10.0) * (0.5 + 0.5 * g_Push.MinorFade);
    const float l_Largest = GridLine(l_Plane, g_Push.Spacing * 100.0);
    const float2 l_AxisWidth = fwidth(l_Plane) * 1.5;
    const float2 l_Axis = 1.0 - min(abs(l_Plane) / max(l_AxisWidth, 1e-8), 1.0);
    const float l_Fade = 1.0 - smoothstep(g_Push.FadeDistance * 0.5, g_Push.FadeDistance, length(l_Plane - g_Push.Eye.xz));

    float4 l_Color = float4(1.0, 1.0, 1.0, max(max(l_Smallest, l_Middle), l_Largest) * 0.3);
    l_Color = lerp(l_Color, float4(0.787, 0.073, 0.073, 0.9), l_Axis.y);
    l_Color = lerp(l_Color, float4(0.073, 0.171, 0.888, 0.9), l_Axis.x);
    l_Color.a *= l_Meets ? l_Fade : 0.0;
    if (l_Color.a <= 0.0)
    {
        discard;
    }

    const float4 l_Point = float4(l_World, 1.0);

    PixelOutput l_Output;
    l_Output.Color = l_Color;
    l_Output.Depth = saturate(dot(g_Push.DepthRow, l_Point) / dot(g_Push.WRow, l_Point));

    return l_Output;
}