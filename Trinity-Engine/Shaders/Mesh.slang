// Meshes drawn one submesh at a time. Each draw's record, in the upload ring, names its mesh buffer, where each vertex stream starts in it, and its material, and the vertices are read from the buffer by the indices the index buffer holds
#include "Bindless.slang"
#include "Material.slang"

static const float c_Pi = 3.14159265;
static const uint c_Absent = 0xFFFFFFFF;
static const uint c_DrawStride = 80;

// Laid out as Renderer3D.cpp writes them. A frame: the view-projection's columns, the eye and whether the view is orthographic, the way it looks, the direction towards the sun, its radiance, the ambient light, then the material table and the two samplers
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
float4 SampleMaterial(Material material, uint slot, float4 texCoords, ByteAddressBuffer frame)
{
    const uint l_Texture = material.Textures[slot];
    const uint2 l_Samplers = frame.Load<uint4>(g_Push.FrameOffset + 144).zw;
    const uint2 l_Nearest = frame.Load<uint2>(g_Push.FrameOffset + 160);
    Texture2D l_Image = DescriptorHandle<Texture2D>(uint2(l_Texture & ~c_MaterialNearestBit, 0));
    SamplerState l_Sampler = DescriptorHandle<SamplerState>((l_Texture & c_MaterialNearestBit) != 0 ? l_Nearest : l_Samplers);

    return l_Image.Sample(l_Sampler, material.GetTexCoord(slot) == 1 ? texCoords.zw : texCoords.xy);
}

// Trowbridge-Reitz, with alpha the square of perceptual roughness
float DistributionGGX(float normalDotHalf, float alpha)
{
    const float l_Alpha2 = alpha * alpha;
    const float l_Denominator = (normalDotHalf * l_Alpha2 - normalDotHalf) * normalDotHalf + 1.0;

    return l_Alpha2 / (c_Pi * l_Denominator * l_Denominator);
}

// Smith's height-correlated masking and shadowing, with the BRDF's 4 N.L N.V folded in
float VisibilitySmithGGXCorrelated(float normalDotView, float normalDotLight, float alpha)
{
    const float l_Alpha2 = alpha * alpha;
    const float l_View = normalDotLight * sqrt((normalDotView - normalDotView * l_Alpha2) * normalDotView + l_Alpha2);
    const float l_Light = normalDotView * sqrt((normalDotLight - normalDotLight * l_Alpha2) * normalDotLight + l_Alpha2);

    return 0.5 / max(l_View + l_Light, 1e-7);
}

float3 FresnelSchlick(float3 reflectance, float viewDotHalf)
{
    const float l_Factor = pow(1.0 - viewDotHalf, 5.0);

    return reflectance + (1.0 - reflectance) * l_Factor;
}

// glTF's metallic-roughness BRDF: Lambert diffuse taking what Fresnel leaves, and GGX specular, lit by the sun. Ambient light stands in for image-based lighting until it arrives, and only it is occluded
[shader("fragment")]
float4 PixelMain(VertexOutput input, bool isFrontFace: SV_IsFrontFace) : SV_Target
{
    ByteAddressBuffer l_Frame = g_Push.Frame;
    const DrawData l_Draw = LoadDraw();
    const uint2 l_Table = l_Frame.Load<uint4>(g_Push.FrameOffset + 144).xy;
    const Material l_Material = LoadMaterial(DescriptorHandle<ByteAddressBuffer>(l_Table), l_Draw.Material);

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

    const float4 l_Eye = l_Frame.Load<float4>(g_Push.FrameOffset + 64);
    const float3 l_Forward = l_Frame.Load<float4>(g_Push.FrameOffset + 80).xyz;
    const float3 l_ToSun = normalize(l_Frame.Load<float4>(g_Push.FrameOffset + 96).xyz);
    const float3 l_SunRadiance = l_Frame.Load<float4>(g_Push.FrameOffset + 112).xyz;
    const float3 l_Ambient = l_Frame.Load<float4>(g_Push.FrameOffset + 128).xyz;

    const float3 l_View = l_Eye.w != 0.0 ? -normalize(l_Forward) : normalize(l_Eye.xyz - input.WorldPosition);
    const float3 l_Half = normalize(l_View + l_ToSun);
    const float l_NormalDotView = max(dot(l_Normal, l_View), 1e-4);
    const float l_NormalDotLight = saturate(dot(l_Normal, l_ToSun));
    const float l_NormalDotHalf = saturate(dot(l_Normal, l_Half));
    const float l_ViewDotHalf = saturate(dot(l_View, l_Half));

    const float l_Alpha = max(l_Roughness * l_Roughness, 0.002);
    const float3 l_Diffuse = l_BaseColor.rgb * (1.0 - l_Metallic);
    const float3 l_Reflectance = lerp(float3(0.04, 0.04, 0.04), l_BaseColor.rgb, l_Metallic);
    const float3 l_Fresnel = FresnelSchlick(l_Reflectance, l_ViewDotHalf);
    const float3 l_Specular = l_Fresnel * (DistributionGGX(l_NormalDotHalf, l_Alpha) * VisibilitySmithGGXCorrelated(l_NormalDotView, l_NormalDotLight, l_Alpha));
    const float3 l_DiffuseTerm = (1.0 - l_Fresnel) * l_Diffuse / c_Pi;

    const float3 l_Color = (l_DiffuseTerm + l_Specular) * l_SunRadiance * l_NormalDotLight + l_Ambient * (l_Diffuse + l_Reflectance) * l_Occlusion + l_Emissive;

    return float4(l_Color, l_BaseColor.a);
}