// Debug lines in world space, drawn by Renderer.cpp over a view's tonemapped image from the vertices DebugDraw gathered: two a line, each a world position and an sRGB colour with red in the low byte, as the display target holds colours. A depth-tested line is drawn only where it is no further from the eye than the scene's surface, read from the view's depth, so meshes hide it. A line on a surface keeps showing, since it may lie up to a thousandth of the surface's distance behind it
#include "Bindless.slang"

// Laid out as Renderer.cpp writes it. The view-projection is four columns, as glm stores it. Depth samples are 0 when the lines are not tested against the scene's depth
struct PushData
{
    float4 ViewProjection[4];
    DescriptorHandle<ByteAddressBuffer> Vertices;
    uint VertexOffset;
    uint Depth;
    uint DepthSamples;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

static const uint c_VertexStride = 16;
// Reversed depth is the eye's near plane over its distance, so a line nearer than the surface's depth times this is within a thousandth of the surface's distance
static const float c_DepthTolerance = 0.999;

struct VertexOutput
{
    float4 Position : SV_Position;
    float4 Color : COLOR0;
};

float4 UnpackColor(uint color)
{
    return float4(float(color & 0xFF), float((color >> 8) & 0xFF), float((color >> 16) & 0xFF), float(color >> 24)) / 255.0;
}

[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    ByteAddressBuffer l_Vertices = g_Push.Vertices;
    const uint l_Address = g_Push.VertexOffset + vertexIndex * c_VertexStride;
    const float3 l_World = asfloat(l_Vertices.Load3(l_Address));

    VertexOutput l_Output;
    l_Output.Position = g_Push.ViewProjection[0] * l_World.x + g_Push.ViewProjection[1] * l_World.y + g_Push.ViewProjection[2] * l_World.z + g_Push.ViewProjection[3];
    l_Output.Color = UnpackColor(l_Vertices.Load(l_Address + 12));

    return l_Output;
}

// From the first sample when the scene's depth is multisampled
float LoadSceneDepth(int2 pixel)
{
    if (g_Push.DepthSamples == 1)
    {
        Texture2D<float> l_Depth = DescriptorHandle<Texture2D<float>>(uint2(g_Push.Depth, 0));

        return l_Depth.Load(int3(pixel, 0));
    }

    Texture2DMS<float> l_Depth = DescriptorHandle<Texture2DMS<float>>(uint2(g_Push.Depth, 0));

    return l_Depth.Load(pixel, 0);
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    if (g_Push.DepthSamples != 0 && input.Position.z < LoadSceneDepth(int2(input.Position.xy)) * c_DepthTolerance)
    {
        discard;
    }

    return input.Color;
}