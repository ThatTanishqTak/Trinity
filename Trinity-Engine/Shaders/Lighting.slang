// What the cluster pass and the opaque pass share: the cluster grid, as ClusterGrid in Renderer3D.hpp has it, point and spot lights as PunctualLight lays them out, and the BRDF every light is shaded with
static const float c_Pi = 3.14159265;
static const uint c_ClusterTilesX = 16;
static const uint c_ClusterTilesY = 9;
static const uint c_ClusterSlices = 24;
static const uint c_ClusterCount = c_ClusterTilesX * c_ClusterTilesY * c_ClusterSlices;
static const uint c_MaxClusterLights = 128;
static const uint c_PunctualLightStride = 48;

struct ClusterGrid
{
    float Near;
    float Far;
    float2 Scale;
    float2 Offset;
    bool Perspective;
};

// Where a slice begins, with both ends exact so the grid meets the CPU's at Near and Far
float GetSliceDepth(ClusterGrid grid, uint slice)
{
    if (slice == 0)
    {
        return grid.Near;
    }

    if (slice >= c_ClusterSlices)
    {
        return grid.Far;
    }

    const float l_Fraction = float(slice) / float(c_ClusterSlices);

    return grid.Perspective ? grid.Near * pow(grid.Far / grid.Near, l_Fraction) : grid.Near + (grid.Far - grid.Near) * l_Fraction;
}

// A cluster's box in view space, as ClusterGrid::GetBounds works it out
void GetClusterBounds(ClusterGrid grid, uint x, uint y, uint slice, out float3 minimum, out float3 maximum)
{
    const float l_Near = GetSliceDepth(grid, slice);
    const float l_Far = GetSliceDepth(grid, slice + 1);
    const float2 l_Low = float2(float(x) * 2.0 / float(c_ClusterTilesX) - 1.0, float(y) * 2.0 / float(c_ClusterTilesY) - 1.0);
    const float2 l_High = float2(float(x + 1) * 2.0 / float(c_ClusterTilesX) - 1.0, float(y + 1) * 2.0 / float(c_ClusterTilesY) - 1.0);
    float2 l_Minimum;
    float2 l_Maximum;
    if (grid.Perspective)
    {
        const float2 l_LowSlope = l_Low / grid.Scale;
        const float2 l_HighSlope = l_High / grid.Scale;
        l_Minimum = min(min(l_LowSlope * l_Near, l_LowSlope * l_Far), min(l_HighSlope * l_Near, l_HighSlope * l_Far));
        l_Maximum = max(max(l_LowSlope * l_Near, l_LowSlope * l_Far), max(l_HighSlope * l_Near, l_HighSlope * l_Far));
    }
    else
    {
        const float2 l_LowSide = (l_Low - grid.Offset) / grid.Scale;
        const float2 l_HighSide = (l_High - grid.Offset) / grid.Scale;
        l_Minimum = min(l_LowSide, l_HighSide);
        l_Maximum = max(l_LowSide, l_HighSide);
    }

    minimum = float3(l_Minimum, -l_Far);
    maximum = float3(l_Maximum, -l_Near);
}

float GetSquaredDistance(float3 position, float3 minimum, float3 maximum)
{
    const float3 l_Outside = max(max(minimum - position, position - maximum), 0.0);

    return dot(l_Outside, l_Outside);
}

struct PunctualLight
{
    float3 Position;
    float Range;
    float3 Direction;
    float ConeScale;
    float3 Radiance;
    float ConeOffset;
};

PunctualLight LoadPunctualLight(ByteAddressBuffer buffer, uint address)
{
    const float4 l_Place = buffer.Load<float4>(address);
    const float4 l_Aim = buffer.Load<float4>(address + 16);
    const float4 l_Light = buffer.Load<float4>(address + 32);

    PunctualLight l_Punctual;
    l_Punctual.Position = l_Place.xyz;
    l_Punctual.Range = l_Place.w;
    l_Punctual.Direction = l_Aim.xyz;
    l_Punctual.ConeScale = l_Aim.w;
    l_Punctual.Radiance = l_Light.xyz;
    l_Punctual.ConeOffset = l_Light.w;

    return l_Punctual;
}

// A shaded point: its position and normal, the way towards the eye, and its diffuse colour, reflectance at normal incidence and GGX alpha
struct Surface
{
    float3 Position;
    float3 Normal;
    float3 View;
    float NormalDotView;
    float3 Diffuse;
    float3 Reflectance;
    float Alpha;
};

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

// glTF's metallic-roughness BRDF for light arriving from one direction: Lambert diffuse taking what Fresnel leaves, and GGX specular. Light from behind the surface adds exactly nothing
float3 ShadeLight(Surface surface, float3 toLight, float3 radiance)
{
    const float l_NormalDotLight = saturate(dot(surface.Normal, toLight));
    if (l_NormalDotLight <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 l_Sum = surface.View + toLight;
    const float3 l_Half = l_Sum * rsqrt(max(dot(l_Sum, l_Sum), 1e-12));
    const float l_NormalDotHalf = saturate(dot(surface.Normal, l_Half));
    const float l_ViewDotHalf = saturate(dot(surface.View, l_Half));
    const float3 l_Fresnel = FresnelSchlick(surface.Reflectance, l_ViewDotHalf);
    const float3 l_Specular = l_Fresnel * (DistributionGGX(l_NormalDotHalf, surface.Alpha) * VisibilitySmithGGXCorrelated(surface.NormalDotView, l_NormalDotLight, surface.Alpha));
    const float3 l_Diffuse = (1.0 - l_Fresnel) * surface.Diffuse / c_Pi;

    return (l_Diffuse + l_Specular) * radiance * l_NormalDotLight;
}

// KHR_lights_punctual's falloff: the inverse square, windowed to reach 0 at the range, and for a spot light a smooth edge between its cone angles. Beyond its range a light adds exactly nothing, so shading by every light adds what shading by a cluster's lights does
float3 ShadePunctualLight(Surface surface, PunctualLight light)
{
    const float3 l_ToLight = light.Position - surface.Position;
    const float l_DistanceSquared = dot(l_ToLight, l_ToLight);
    const float l_RangeSquared = light.Range * light.Range;
    if (l_DistanceSquared >= l_RangeSquared)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float l_Ratio = l_DistanceSquared / l_RangeSquared;
    const float l_Window = saturate(1.0 - l_Ratio * l_Ratio);
    const float3 l_Direction = l_ToLight * rsqrt(max(l_DistanceSquared, 1e-8));
    const float l_Cone = saturate(dot(light.Direction, -l_Direction) * light.ConeScale + light.ConeOffset);
    const float l_Attenuation = l_Window * l_Window * l_Cone * l_Cone / max(l_DistanceSquared, 1e-4);

    return ShadeLight(surface, l_Direction, light.Radiance * l_Attenuation);
}

// From blue for an empty cluster through green and yellow to red at the cap, and magenta past it
float3 GetHeatColor(uint count)
{
    if (count > c_MaxClusterLights)
    {
        return float3(1.0, 0.0, 1.0);
    }

    const float l_Heat = float(count) / float(c_MaxClusterLights) * 4.0;

    return saturate(float3(min(l_Heat - 1.5, 4.5 - l_Heat), min(l_Heat - 0.5, 3.5 - l_Heat), min(l_Heat + 0.5, 2.5 - l_Heat)));
}