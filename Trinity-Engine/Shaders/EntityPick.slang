// The entity in one pixel, written for Renderer to copy back to the CPU
#include "EntityIDs.slang"

// Laid out as Renderer.cpp writes it
struct PushData
{
    uint Sprites;
    uint Meshes;
    uint MeshSamples;
    uint Padding;
    int2 Pixel;
    DescriptorHandle<RWByteAddressBuffer> Output;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

[shader("compute")]
[numthreads(1, 1, 1)]
void PickEntity()
{
    RWByteAddressBuffer l_Output = g_Push.Output;
    l_Output.Store(0, LoadEntity(g_Push.Sprites, g_Push.Meshes, g_Push.MeshSamples, g_Push.Pixel));
}