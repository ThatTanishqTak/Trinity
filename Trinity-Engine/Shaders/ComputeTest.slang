// Sandbox's compute test: a buffer fill, a copy in a later dispatch, and a storage texture fill
#include "Bindless.slang"

struct PushData
{
    DescriptorHandle<RWByteAddressBuffer> Buffer;
    DescriptorHandle<RWTexture2D<float4>> Texture;
    uint Count;
    uint Size;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

// The same hash the Sandbox computes on the CPU to check the readback
uint BufferWord(uint index)
{
    return index * 2654435761u + 0x9E3779B9u;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void FillBuffer(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= g_Push.Count)
    {
        return;
    }

    RWByteAddressBuffer l_Buffer = g_Push.Buffer;
    l_Buffer.Store(id.x * 4, BufferWord(id.x));
}

// Reads words another workgroup wrote in FillBuffer, so they match only if the barrier worked
[shader("compute")]
[numthreads(64, 1, 1)]
void ReverseBuffer(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= g_Push.Count)
    {
        return;
    }

    RWByteAddressBuffer l_Buffer = g_Push.Buffer;
    const uint l_Word = l_Buffer.Load((g_Push.Count - 1 - id.x) * 4);
    l_Buffer.Store((g_Push.Count + id.x) * 4, ((l_Word << 7) | (l_Word >> 25)) ^ id.x);
}

// Whole 8-bit values, so an RGBA8 UNORM texture stores them exactly
[shader("compute")]
[numthreads(8, 8, 1)]
void FillTexture(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= g_Push.Size || id.y >= g_Push.Size)
    {
        return;
    }

    RWTexture2D<float4> l_Texture = g_Push.Texture;
    l_Texture[id.xy] = float4(float(id.x), float(id.y), float((id.x * 3 + id.y * 5) & 0xFF), 255.0) / 255.0;
}