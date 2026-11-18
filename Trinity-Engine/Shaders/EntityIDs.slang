// The sprites' ID first, as they are drawn over the meshes, then the meshes'. 0 is no entity
#include "Bindless.slang"

static const uint c_NoEntity = 0;

// Mesh samples are 0 when there are no mesh IDs
uint LoadEntity(uint sprites, uint meshes, uint meshSamples, int2 pixel)
{
    Texture2D<uint> l_Sprites = DescriptorHandle<Texture2D<uint>>(uint2(sprites, 0));
    const uint l_Sprite = l_Sprites.Load(int3(pixel, 0));
    if (l_Sprite != c_NoEntity || meshSamples == 0)
    {
        return l_Sprite;
    }

    if (meshSamples == 1)
    {
        Texture2D<uint> l_Meshes = DescriptorHandle<Texture2D<uint>>(uint2(meshes, 0));

        return l_Meshes.Load(int3(pixel, 0));
    }

    Texture2DMS<uint> l_Meshes = DescriptorHandle<Texture2DMS<uint>>(uint2(meshes, 0));

    return l_Meshes.Load(pixel, 0);
}