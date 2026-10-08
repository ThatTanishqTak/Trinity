// Meshes drawn one submesh at a time. Each draw's record, in the upload ring, names its mesh buffer, where each vertex stream starts in it, and its material, and the vertices are read from the buffer by the indices the index buffer holds
#include "Bindless.slang"
#include "Lighting.slang"
#include "Material.slang"

static const uint c_Absent = 0xFFFFFFFF;
static const uint c_DrawStride = 80;
static const uint c_MaxDirectionalLights = 4;
static const uint c_ShadeAllLightsFlag = 1;
static const uint c_LightHeatmapFlag = 2;

// Laid out as Renderer3D.cpp writes them. The frame is FrameData there, and each draw a DrawData
struct PushData
{
    DescriptorHandle<ByteAddressBuffer> Frame;
    DescriptorHandle<ByteAddressBuffer> Draws;
    uint FrameOffset;
    uint DrawOffset;
    uint DrawIndex;
    uint Padding;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 WorldPosition : POSITION0;
    float3 Normal : NORMAL0;
    float4 Tangent : TANGENT0;
    float4 TexCoords : TEXCOORD0;
    float4 Color : COLOR0;
    // The clip-space position again, whose division the pixel shader does to find its cluster's tile
    float4 Clip : TEXCOORD1;
};

// The rows of a 3x4 world matrix, then the mesh buffer's bindless index, the material's record, and where the positions, normals and tangents, both UV sets and the colours start, or c_Absent
struct DrawData
{
    float4 Rows[3];
    uint MeshBuffer;
    uint Material;
    uint Positions;
    uint NormalTangents;
    uint TexCoords0;
    uint TexCoords1;
    uint Colors;
};

DrawData LoadDraw()
{
    ByteAddressBuffer l_Draws = g_Push.Draws;
    const uint l_Address = g_Push.DrawOffset + g_Push.DrawIndex * c_DrawStride;
    const uint4 l_Words = l_Draws.Load<uint4>(l_Address + 48);
    const uint4 l_Streams = l_Draws.Load<uint4>(l_Address + 64);

    DrawData l_Draw;
    l_Draw.Rows[0] = l_Draws.Load<float4>(l_Address);
    l_Draw.Rows[1] = l_Draws.Load<float4>(l_Address + 16);
    l_Draw.Rows[2] = l_Draws.Load<float4>(l_Address + 32);
    l_Draw.MeshBuffer = l_Words.x;
    l_Draw.Material = l_Words.y;
    l_Draw.Positions = l_Words.z;
    l_Draw.NormalTangents = l_Words.w;
    l_Draw.TexCoords0 = l_Streams.x;
    l_Draw.TexCoords1 = l_Streams.y;
    l_Draw.Colors = l_Streams.z;

    return l_Draw;
}

// The parts of the frame's constants the pixel shader needs: the eye, with w set for an orthographic view, the way it looks, the row a point's view depth comes from, the ambient light, the directional lights, the light counts and flags, how depth maps to slices, the bindless indices of the material table, the samplers, the lights and the clusters, and the environment: its specular cubemap, irradiance cubemap, lookup table and specular mip count, then its intensity and the cosine and sine of its turn
struct FrameConstants
{
    float4 Eye;
    float3 Forward;
    float4 ViewDepth;
    float3 Ambient;
    uint DirectionalCount;
    uint LightCount;
    uint Flags;
    bool Perspective;
    float2 Slicing;
    uint2 MaterialTable;
    uint2 LinearSampler;
    uint2 NearestSampler;
    uint2 Lights;
    uint2 ClusterCounts;
    uint2 ClusterLights;
    uint LightsOffset;
    uint2 ClampSampler;
    uint4 Environment;
    float4 EnvironmentParameters;
};

FrameConstants LoadFrame()
{
    ByteAddressBuffer l_Frame = g_Push.Frame;
    const uint l_Base = g_Push.FrameOffset;
    const uint4 l_Counts = l_Frame.Load<uint4>(l_Base + 256);
    const uint4 l_Table = l_Frame.Load<uint4>(l_Base + 288);
    const uint4 l_Nearest = l_Frame.Load<uint4>(l_Base + 304);
    const uint4 l_Clusters = l_Frame.Load<uint4>(l_Base + 320);

    FrameConstants l_Constants;
    l_Constants.Eye = l_Frame.Load<float4>(l_Base + 64);
    l_Constants.Forward = l_Frame.Load<float4>(l_Base + 80).xyz;
    l_Constants.ViewDepth = l_Frame.Load<float4>(l_Base + 96);
    l_Constants.Ambient = l_Frame.Load<float4>(l_Base + 112).xyz;
    l_Constants.DirectionalCount = min(l_Counts.x, c_MaxDirectionalLights);
    l_Constants.LightCount = l_Counts.y;
    l_Constants.Flags = l_Counts.z;
    l_Constants.Perspective = l_Counts.w != 0;
    l_Constants.Slicing = l_Frame.Load<float4>(l_Base + 272).xy;
    l_Constants.MaterialTable = l_Table.xy;
    l_Constants.LinearSampler = l_Table.zw;
    l_Constants.NearestSampler = l_Nearest.xy;
    l_Constants.Lights = l_Nearest.zw;
    l_Constants.ClusterCounts = l_Clusters.xy;
    l_Constants.ClusterLights = l_Clusters.zw;
    const uint4 l_Offsets = l_Frame.Load<uint4>(l_Base + 336);
    l_Constants.LightsOffset = l_Offsets.x;
    l_Constants.ClampSampler = l_Offsets.yz;
    l_Constants.Environment = l_Frame.Load<uint4>(l_Base + 352);
    l_Constants.EnvironmentParameters = l_Frame.Load<float4>(l_Base + 368);

    return l_Constants;
}

// The tile from where the pixel lies on screen, and the slice from its view depth: its logarithm for a perspective view, as the slices are spaced
uint GetClusterIndex(FrameConstants frame, float4 clip, float viewDepth)
{
    const float2 l_Tiles = float2(float(c_ClusterTilesX), float(c_ClusterTilesY));
    const float2 l_Tile = clamp(floor((clip.xy / clip.w * 0.5 + 0.5) * l_Tiles), 0.0, l_Tiles - 1.0);
    const float l_Depth = frame.Perspective ? log2(max(viewDepth, 1e-6)) : viewDepth;
    const float l_Slice = clamp(floor(l_Depth * frame.Slicing.x + frame.Slicing.y), 0.0, float(c_ClusterSlices - 1));

    return uint(l_Tile.x) + c_ClusterTilesX * (uint(l_Tile.y) + c_ClusterTilesY * uint(l_Slice));
}

// A direction turned about +Y by the environment's rotation, as glTF Sample Viewer turns every direction it looks up
float3 RotateToEnvironment(FrameConstants frame, float3 direction)
{
    const float l_Cosine = frame.EnvironmentParameters.y;
    const float l_Sine = frame.EnvironmentParameters.z;

    return float3(l_Cosine * direction.x + l_Sine * direction.z, direction.y, l_Cosine * direction.z - l_Sine * direction.x);
}

// A unit direction unfolded from the octahedron, as MeshAsset.cpp's DecodeOctahedral does
float3 DecodeOctahedral(int2 encoded)
{
    const float2 l_Folded = max(float2(encoded) / 32767.0, -1.0);
    float3 l_Direction = float3(l_Folded.x, l_Folded.y, 1.0 - abs(l_Folded.x) - abs(l_Folded.y));
    const float l_Fold = max(-l_Direction.z, 0.0);
    l_Direction.x += l_Direction.x >= 0.0 ? -l_Fold : l_Fold;
    l_Direction.y += l_Direction.y >= 0.0 ? -l_Fold : l_Fold;

    return normalize(l_Direction);
}

// The low then the high 16 bits, each sign-extended
int2 UnpackInt16(uint packed)
{
    return int2(int(packed << 16) >> 16, int(packed) >> 16);
}

// Both passes run this, so the pre-pass and the opaque pass compute the same depth and the equal test holds. Normals go through the cofactor of the world matrix, which is right for any scale, and keeps facing outwards under a mirroring one
[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    const DrawData l_Draw = LoadDraw();
    ByteAddressBuffer l_Mesh = DescriptorHandle<ByteAddressBuffer>(uint2(l_Draw.MeshBuffer, 0));
    ByteAddressBuffer l_Frame = g_Push.Frame;

    const float4 l_Local = float4(l_Mesh.Load<float3>(l_Draw.Positions + vertexIndex * 12), 1.0);
    const float3 l_World = float3(dot(l_Draw.Rows[0], l_Local), dot(l_Draw.Rows[1], l_Local), dot(l_Draw.Rows[2], l_Local));

    VertexOutput l_Output;
    l_Output.Position = l_Frame.Load<float4>(g_Push.FrameOffset) * l_World.x + l_Frame.Load<float4>(g_Push.FrameOffset + 16) * l_World.y + l_Frame.Load<float4>(g_Push.FrameOffset + 32) * l_World.z + l_Frame.Load<float4>(g_Push.FrameOffset + 48);
    l_Output.Clip = l_Output.Position;
    l_Output.WorldPosition = l_World;

    const float3 l_Column0 = float3(l_Draw.Rows[0].x, l_Draw.Rows[1].x, l_Draw.Rows[2].x);
    const float3 l_Column1 = float3(l_Draw.Rows[0].y, l_Draw.Rows[1].y, l_Draw.Rows[2].y);
    const float3 l_Column2 = float3(l_Draw.Rows[0].z, l_Draw.Rows[1].z, l_Draw.Rows[2].z);
    const float l_Sign = dot(l_Column0, cross(l_Column1, l_Column2)) < 0.0 ? -1.0 : 1.0;

    float3 l_Normal = float3(0.0, 0.0, 1.0);
    float4 l_Tangent = float4(1.0, 0.0, 0.0, 1.0);
    if (l_Draw.NormalTangents != c_Absent)
    {
        const uint2 l_Packed = l_Mesh.Load<uint2>(l_Draw.NormalTangents + vertexIndex * 8);
        const int2 l_EncodedTangent = UnpackInt16(l_Packed.y);
        l_Normal = DecodeOctahedral(UnpackInt16(l_Packed.x));
        l_Tangent = float4(DecodeOctahedral(int2(l_EncodedTangent.x, l_EncodedTangent.y & ~1)), (l_EncodedTangent.y & 1) != 0 ? -1.0 : 1.0);
    }

    l_Output.Normal = (cross(l_Column1, l_Column2) * l_Normal.x + cross(l_Column2, l_Column0) * l_Normal.y + cross(l_Column0, l_Column1) * l_Normal.z) * l_Sign;
    l_Output.Tangent = float4(l_Column0 * l_Tangent.x + l_Column1 * l_Tangent.y + l_Column2 * l_Tangent.z, l_Tangent.w * l_Sign);

    const float2 l_TexCoord0 = l_Draw.TexCoords0 != c_Absent ? l_Mesh.Load<float2>(l_Draw.TexCoords0 + vertexIndex * 8) : float2(0.0, 0.0);
    const float2 l_TexCoord1 = l_Draw.TexCoords1 != c_Absent ? l_Mesh.Load<float2>(l_Draw.TexCoords1 + vertexIndex * 8) : float2(0.0, 0.0);
    l_Output.TexCoords = float4(l_TexCoord0, l_TexCoord1);

    float4 l_Color = float4(1.0, 1.0, 1.0, 1.0);
    if (l_Draw.Colors != c_Absent)
    {
        const uint2 l_Packed = l_Mesh.Load<uint2>(l_Draw.Colors + vertexIndex * 8);
        l_Color = float4(float(l_Packed.x & 0xFFFF), float(l_Packed.x >> 16), float(l_Packed.y & 0xFFFF), float(l_Packed.y >> 16)) / 65535.0;
    }

    l_Output.Color = l_Color;

    return l_Output;
}

// The pre-pass writes depth alone
[shader("fragment")]
void DepthPixelMain()
{

}

// The record's texture, sampled with its filter, through the UV set its slot names. A whole draw names one material, so no index here varies across a wave
float4 SampleMaterial(Material material, uint slot, float4 texCoords, FrameConstants frame)
{
    const uint l_Texture = material.Textures[slot];
    Texture2D l_Image = DescriptorHandle<Texture2D>(uint2(l_Texture & ~c_MaterialNearestBit, 0));
    SamplerState l_Sampler = DescriptorHandle<SamplerState>((l_Texture & c_MaterialNearestBit) != 0 ? frame.NearestSampler : frame.LinearSampler);

    return l_Image.Sample(l_Sampler, material.GetTexCoord(slot) == 1 ? texCoords.zw : texCoords.xy);
}

// glTF's metallic-roughness material lit by the directional lights, then by the point and spot lights of the pixel's cluster, or by every one of them when asked, in light order either way, then by the environment, or with no environment a little ambient light, which alone is occluded. The environment's radiance is prefiltered for roughness m / (levels - 1) at mip m
[shader("fragment")]
float4 PixelMain(VertexOutput input, bool isFrontFace: SV_IsFrontFace) : SV_Target
{
    const FrameConstants l_Frame = LoadFrame();
    const DrawData l_Draw = LoadDraw();
    const Material l_Material = LoadMaterial(DescriptorHandle<ByteAddressBuffer>(l_Frame.MaterialTable), l_Draw.Material);

    float4 l_BaseColor = l_Material.BaseColorFactor * input.Color;
    if (l_Material.HasTexture(c_BaseColorSlot))
    {
        l_BaseColor *= SampleMaterial(l_Material, c_BaseColorSlot, input.TexCoords, l_Frame);
    }

    float l_Metallic = l_Material.MetallicFactor;
    float l_Roughness = l_Material.RoughnessFactor;
    if (l_Material.HasTexture(c_MetallicRoughnessSlot))
    {
        const float4 l_Sample = SampleMaterial(l_Material, c_MetallicRoughnessSlot, input.TexCoords, l_Frame);
        l_Metallic *= l_Sample.b;
        l_Roughness *= l_Sample.g;
    }

    // A back face of a double-sided surface is lit as its own front
    float3 l_Normal = normalize(input.Normal) * (isFrontFace ? 1.0 : -1.0);
    if (l_Material.HasTexture(c_NormalSlot))
    {
        const float2 l_Sample = SampleMaterial(l_Material, c_NormalSlot, input.TexCoords, l_Frame).rg * 2.0 - 1.0;
        const float2 l_Scaled = l_Sample * l_Material.NormalScale;
        const float3 l_TangentNormal = float3(l_Scaled, sqrt(saturate(1.0 - dot(l_Sample, l_Sample))));
        const float3 l_Tangent = normalize(input.Tangent.xyz - l_Normal * dot(l_Normal, input.Tangent.xyz));
        const float3 l_Bitangent = cross(l_Normal, l_Tangent) * input.Tangent.w;
        l_Normal = normalize(l_Tangent * l_TangentNormal.x + l_Bitangent * l_TangentNormal.y + l_Normal * l_TangentNormal.z);
    }

    float l_Occlusion = 1.0;
    if (l_Material.HasTexture(c_OcclusionSlot))
    {
        l_Occlusion = lerp(1.0, SampleMaterial(l_Material, c_OcclusionSlot, input.TexCoords, l_Frame).r, l_Material.OcclusionStrength);
    }

    float3 l_Emissive = l_Material.EmissiveFactor * l_Material.EmissiveStrength;
    if (l_Material.HasTexture(c_EmissiveSlot))
    {
        l_Emissive *= SampleMaterial(l_Material, c_EmissiveSlot, input.TexCoords, l_Frame).rgb;
    }

    Surface l_Surface;
    l_Surface.Position = input.WorldPosition;
    l_Surface.Normal = l_Normal;
    l_Surface.View = l_Frame.Eye.w != 0.0 ? -normalize(l_Frame.Forward) : normalize(l_Frame.Eye.xyz - input.WorldPosition);
    l_Surface.NormalDotView = max(dot(l_Normal, l_Surface.View), 1e-4);
    l_Surface.BaseColor = l_BaseColor.rgb;
    l_Surface.Metallic = saturate(l_Metallic);
    l_Surface.PerceptualRoughness = saturate(l_Roughness);
    l_Surface.Alpha = max(l_Surface.PerceptualRoughness * l_Surface.PerceptualRoughness, 0.002);

    ByteAddressBuffer l_FrameBuffer = g_Push.Frame;
    float3 l_Color = float3(0.0, 0.0, 0.0);
    for (uint it_Light = 0; it_Light < l_Frame.DirectionalCount; ++it_Light)
    {
        const uint l_Address = g_Push.FrameOffset + 128 + it_Light * 32;
        l_Color += ShadeLight(l_Surface, normalize(l_FrameBuffer.Load<float4>(l_Address).xyz), l_FrameBuffer.Load<float4>(l_Address + 16).xyz);
    }

    uint l_ClusterCount = 0;
    if (l_Frame.LightCount != 0)
    {
        ByteAddressBuffer l_Lights = DescriptorHandle<ByteAddressBuffer>(l_Frame.Lights);
        if ((l_Frame.Flags & c_ShadeAllLightsFlag) != 0)
        {
            for (uint it_Light = 0; it_Light < l_Frame.LightCount; ++it_Light)
            {
                l_Color += ShadePunctualLight(l_Surface, LoadPunctualLight(l_Lights, l_Frame.LightsOffset + it_Light * c_PunctualLightStride));
            }
        }
        else
        {
            ByteAddressBuffer l_Counts = DescriptorHandle<ByteAddressBuffer>(l_Frame.ClusterCounts);
            ByteAddressBuffer l_ClusterLights = DescriptorHandle<ByteAddressBuffer>(l_Frame.ClusterLights);
            const uint l_Cluster = GetClusterIndex(l_Frame, input.Clip, dot(l_Frame.ViewDepth, float4(input.WorldPosition, 1.0)));
            l_ClusterCount = l_Counts.Load(l_Cluster * 4);
            const uint l_Shaded = min(l_ClusterCount, c_MaxClusterLights);
            for (uint it_Entry = 0; it_Entry < l_Shaded; ++it_Entry)
            {
                const uint l_Light = l_ClusterLights.Load((l_Cluster * c_MaxClusterLights + it_Entry) * 4);
                l_Color += ShadePunctualLight(l_Surface, LoadPunctualLight(l_Lights, l_Frame.LightsOffset + l_Light * c_PunctualLightStride));
            }
        }
    }

    if (l_Frame.Environment.w != 0)
    {
        TextureCube l_Specular = DescriptorHandle<TextureCube>(uint2(l_Frame.Environment.x, 0));
        TextureCube l_Irradiance = DescriptorHandle<TextureCube>(uint2(l_Frame.Environment.y, 0));
        Texture2D l_Lookup = DescriptorHandle<Texture2D>(uint2(l_Frame.Environment.z, 0));
        SamplerState l_Linear = DescriptorHandle<SamplerState>(l_Frame.LinearSampler);
        SamplerState l_Clamp = DescriptorHandle<SamplerState>(l_Frame.ClampSampler);
        const float3 l_Reflection = normalize(reflect(-l_Surface.View, l_Surface.Normal));
        const float l_Level = l_Surface.PerceptualRoughness * float(l_Frame.Environment.w - 1);
        const float3 l_Prefiltered = l_Specular.SampleLevel(l_Linear, RotateToEnvironment(l_Frame, l_Reflection), l_Level).rgb * l_Frame.EnvironmentParameters.x;
        const float3 l_Incoming = l_Irradiance.SampleLevel(l_Linear, RotateToEnvironment(l_Frame, l_Surface.Normal), 0.0).rgb * l_Frame.EnvironmentParameters.x;
        const float2 l_ScaleBias = l_Lookup.SampleLevel(l_Clamp, float2(saturate(dot(l_Surface.Normal, l_Surface.View)), l_Surface.PerceptualRoughness), 0.0).rg;
        l_Color += ShadeEnvironment(l_Surface, l_Incoming, l_Prefiltered, l_ScaleBias) * l_Occlusion;
    }
    else
    {
        l_Color += l_Frame.Ambient * (l_Surface.BaseColor * (1.0 - l_Surface.Metallic) + lerp(float3(0.04, 0.04, 0.04), l_Surface.BaseColor, l_Surface.Metallic)) * l_Occlusion;
    }

    l_Color += l_Emissive;
    if ((l_Frame.Flags & c_LightHeatmapFlag) != 0)
    {
        l_Color = lerp(l_Color * 0.25, GetHeatColor(l_ClusterCount), 0.75);
    }

    return float4(l_Color, l_BaseColor.a);
}