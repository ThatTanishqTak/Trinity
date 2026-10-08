// The entity in a pixel of the ID targets Renderer draws when a scene asks for them: the sprites', which are drawn over the meshes, then the meshes', from their first sample when they are multisampled. 0 is no entity, as Trinity::ToPickID has it
#include "Bindless.slang"

static const uint c_NoEntity = 0;

// The meshes' samples are 0 when there are no mesh IDs, and either target's index is a bindless shader resource index
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