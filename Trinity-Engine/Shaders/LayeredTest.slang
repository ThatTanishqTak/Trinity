// Sandbox's layered texture test: reads every mip of every face of a cube and every layer of an array through their shader views, one word each
#include "Bindless.slang"

struct PushData
{
    DescriptorHandle<TextureCube> Cube;
    DescriptorHandle<Texture2DArray> Array;
    DescriptorHandle<SamplerState> Sampler;
    DescriptorHandle<RWByteAddressBuffer> Output;
    uint MipLevels;
    uint ArrayLayers;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

static const uint c_CubeFaces = 6;

uint PackColor(float4 color)
{
    const uint4 l_Bytes = uint4(round(saturate(color) * 255.0));

    return l_Bytes.r | (l_Bytes.g << 8) | (l_Bytes.b << 16) | (l_Bytes.a << 24);
}

// The middle of each face, in the order +X, -X, +Y, -Y, +Z, -Z
float3 GetFaceDirection(uint face)
{
    const float l_Sign = (face & 1) == 0 ? 1.0 : -1.0;
    const uint l_Axis = face / 2;

    return float3(l_Axis == 0 ? l_Sign : 0.0, l_Axis == 1 ? l_Sign : 0.0, l_Axis == 2 ? l_Sign : 0.0);
}

// X is the mip, Y the face or layer, and Z picks the cube or the array. The cube's words come first, then the array's, each numbered as GetSubresourceIndex numbers subresources
[shader("compute")]
[numthreads(4, 8, 1)]
void ReadLayers(uint3 id: SV_DispatchThreadID)
{
    const bool l_Cube = id.z == 0;
    if (id.x >= g_Push.MipLevels || id.y >= (l_Cube ? c_CubeFaces : g_Push.ArrayLayers))
    {
        return;
    }

    float4 l_Color;
    if (l_Cube)
    {
        TextureCube l_Texture = g_Push.Cube;
        SamplerState l_Sampler = g_Push.Sampler;
        l_Color = l_Texture.SampleLevel(l_Sampler, GetFaceDirection(id.y), float(id.x));
    }
    else
    {
        Texture2DArray l_Texture = g_Push.Array;
        l_Color = l_Texture.Load(int4(0, 0, int(id.y), int(id.x)));
    }

    const uint l_Word = (l_Cube ? 0 : c_CubeFaces * g_Push.MipLevels) + id.x + id.y * g_Push.MipLevels;
    RWByteAddressBuffer l_Output = g_Push.Output;
    l_Output.Store(l_Word * 4, PackColor(l_Color));
}