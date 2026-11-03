// The scene's depth in one pixel, written for Renderer to copy back to the CPU. Depth is reversed, so of a multisampled pixel's samples the largest is the nearest surface, which is the one kept
#include "Bindless.slang"

// Laid out as Renderer.cpp writes it. One sample is a depth texture that is not multisampled
struct PushData
{
    uint Depth;
    uint DepthSamples;
    int2 Pixel;
    DescriptorHandle<RWByteAddressBuffer> Output;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

[shader("compute")]
[numthreads(1, 1, 1)]
void PickDepth()
{
    float l_Depth = 0.0;
    if (g_Push.DepthSamples <= 1)
    {
        Texture2D<float> l_Texture = DescriptorHandle<Texture2D<float>>(uint2(g_Push.Depth, 0));
        l_Depth = l_Texture.Load(int3(g_Push.Pixel, 0));
    }
    else
    {
        Texture2DMS<float> l_Texture = DescriptorHandle<Texture2DMS<float>>(uint2(g_Push.Depth, 0));
        for (uint it_Sample = 0; it_Sample < g_Push.DepthSamples; ++it_Sample)
        {
            l_Depth = max(l_Depth, l_Texture.Load(g_Push.Pixel, it_Sample));
        }
    }

    RWByteAddressBuffer l_Output = g_Push.Output;
    l_Output.Store(0, asuint(l_Depth));
}