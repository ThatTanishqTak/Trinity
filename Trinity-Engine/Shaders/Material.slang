// A record of the renderer's material table, as MaterialLoader::Pack writes it. Colours are linear, and each texture is a bindless index with the nearest filter in the top bit, or c_NoTexture for an empty slot
static const uint c_MaterialStride = 80;
static const uint c_NoTexture = 0xFFFFFFFF;
static const uint c_MaterialNearestBit = 0x80000000;

static const uint c_AlphaModeOpaque = 0;
static const uint c_AlphaModeMask = 1;
static const uint c_AlphaModeBlend = 2;

static const uint c_BaseColorSlot = 0;
static const uint c_MetallicRoughnessSlot = 1;
static const uint c_NormalSlot = 2;
static const uint c_OcclusionSlot = 3;
static const uint c_EmissiveSlot = 4;
static const uint c_MaterialTextureSlots = 5;

struct Material
{
    float4 BaseColorFactor;
    float3 EmissiveFactor;
    float EmissiveStrength;
    float MetallicFactor;
    float RoughnessFactor;
    float NormalScale;
    float OcclusionStrength;
    float AlphaCutoff;
    // The alpha mode in the two lowest bits, then double-sided, and from bit 8 one bit for each texture slot sampled with the second UV set
    uint Flags;
    uint Textures[c_MaterialTextureSlots];

    uint GetAlphaMode()
    {
        return Flags & 0x3;
    }

    bool IsDoubleSided()
    {
        return (Flags & 0x4) != 0;
    }

    uint GetTexCoord(uint slot)
    {
        return (Flags >> (8 + slot)) & 1;
    }

    bool HasTexture(uint slot)
    {
        return Textures[slot] != c_NoTexture;
    }
};

Material LoadMaterial(ByteAddressBuffer table, uint index)
{
    const uint l_Address = index * c_MaterialStride;
    const float4 l_Emissive = table.Load<float4>(l_Address + 16);
    const float4 l_Factors = table.Load<float4>(l_Address + 32);
    const uint4 l_Words = table.Load<uint4>(l_Address + 48);
    const uint4 l_Textures = table.Load<uint4>(l_Address + 64);

    Material l_Material;
    l_Material.BaseColorFactor = table.Load<float4>(l_Address);
    l_Material.EmissiveFactor = l_Emissive.xyz;
    l_Material.EmissiveStrength = l_Emissive.w;
    l_Material.MetallicFactor = l_Factors.x;
    l_Material.RoughnessFactor = l_Factors.y;
    l_Material.NormalScale = l_Factors.z;
    l_Material.OcclusionStrength = l_Factors.w;
    l_Material.AlphaCutoff = asfloat(l_Words.x);
    l_Material.Flags = l_Words.y;
    l_Material.Textures[0] = l_Words.z;
    l_Material.Textures[1] = l_Words.w;
    l_Material.Textures[2] = l_Textures.x;
    l_Material.Textures[3] = l_Textures.y;
    l_Material.Textures[4] = l_Textures.z;

    return l_Material;
}