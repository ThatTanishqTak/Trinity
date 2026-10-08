// A line around the selected entities, drawn over the tonemapped image from the ID targets: on pixels outside them that have one within two pixels, stronger the more of their neighbours do, so it reads the same at any zoom and through anything in front
#include "EntityIDs.slang"

// Laid out as Renderer.cpp writes it. The selected entities are sorted, so each pixel finds its own by a binary search. The colour is sRGB-encoded, as the display target is
struct PushData
{
    uint Sprites;
    uint Meshes;
    uint MeshSamples;
    uint SelectedCount;
    DescriptorHandle<ByteAddressBuffer> Selected;
    uint SelectedOffset;
    uint Padding;
    int2 Size;
    int2 Padding2;
    float4 Color;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

static const int2 c_Neighbours[12] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1), int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1), int2(-2, 0), int2(2, 0), int2(0, -2), int2(0, 2) };

struct VertexOutput
{
    float4 Position : SV_Position;
};

// One triangle over the whole target
[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    VertexOutput l_Output;
    l_Output.Position = float4(float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2)) * 2.0 - 1.0, 0.0, 1.0);

    return l_Output;
}

bool IsSelected(int2 pixel)
{
    const uint l_Entity = LoadEntity(g_Push.Sprites, g_Push.Meshes, g_Push.MeshSamples, clamp(pixel, int2(0, 0), g_Push.Size - 1));
    if (l_Entity == c_NoEntity)
    {
        return false;
    }

    ByteAddressBuffer l_Selected = g_Push.Selected;
    uint l_Low = 0;
    uint l_High = g_Push.SelectedCount;
    while (l_Low < l_High)
    {
        const uint l_Middle = (l_Low + l_High) / 2;
        const uint l_Value = l_Selected.Load(g_Push.SelectedOffset + l_Middle * 4);
        if (l_Value == l_Entity)
        {
            return true;
        }

        if (l_Value < l_Entity)
        {
            l_Low = l_Middle + 1;
        }
        else
        {
            l_High = l_Middle;
        }
    }

    return false;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    const int2 l_Pixel = int2(input.Position.xy);
    if (IsSelected(l_Pixel))
    {
        discard;
    }

    uint l_Hits = 0;
    for (uint it_Neighbour = 0; it_Neighbour < 12; ++it_Neighbour)
    {
        l_Hits += IsSelected(l_Pixel + c_Neighbours[it_Neighbour]) ? 1 : 0;
    }

    if (l_Hits == 0)
    {
        discard;
    }

    return float4(g_Push.Color.rgb, g_Push.Color.a * saturate(float(l_Hits) / 3.0));
}