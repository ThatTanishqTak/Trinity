// Sorts a view's point and spot lights into clusters, a cluster to a thread: each tests its box in view space against every light's sphere, in light order, keeps the first c_MaxClusterLights that reach it and counts them all
#include "Bindless.slang"
#include "Lighting.slang"

// Laid out as ClusterPushData in Renderer3D.cpp. The light spheres are in the upload ring, a float4 each
struct PushData
{
    DescriptorHandle<ByteAddressBuffer> Bounds;
    DescriptorHandle<RWByteAddressBuffer> Counts;
    DescriptorHandle<RWByteAddressBuffer> Lights;
    uint BoundsOffset;
    uint LightCount;
    float Near;
    float Far;
    float ScaleX;
    float ScaleY;
    float OffsetX;
    float OffsetY;
    uint Perspective;
    uint Padding;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

[shader("compute")]
[numthreads(64, 1, 1)]
void AssignLights(uint3 id: SV_DispatchThreadID)
{
    const uint l_Cluster = id.x;
    if (l_Cluster >= c_ClusterCount)
    {
        return;
    }

    ClusterGrid l_Grid;
    l_Grid.Near = g_Push.Near;
    l_Grid.Far = g_Push.Far;
    l_Grid.Scale = float2(g_Push.ScaleX, g_Push.ScaleY);
    l_Grid.Offset = float2(g_Push.OffsetX, g_Push.OffsetY);
    l_Grid.Perspective = g_Push.Perspective != 0;

    float3 l_Minimum;
    float3 l_Maximum;
    GetClusterBounds(l_Grid, l_Cluster % c_ClusterTilesX, (l_Cluster / c_ClusterTilesX) % c_ClusterTilesY, l_Cluster / (c_ClusterTilesX * c_ClusterTilesY), l_Minimum, l_Maximum);

    ByteAddressBuffer l_Bounds = g_Push.Bounds;
    RWByteAddressBuffer l_Lights = g_Push.Lights;
    uint l_Count = 0;
    for (uint it_Light = 0; it_Light < g_Push.LightCount; ++it_Light)
    {
        const float4 l_Sphere = l_Bounds.Load<float4>(g_Push.BoundsOffset + it_Light * 16);
        if (GetSquaredDistance(l_Sphere.xyz, l_Minimum, l_Maximum) > l_Sphere.w * l_Sphere.w)
        {
            continue;
        }

        if (l_Count < c_MaxClusterLights)
        {
            l_Lights.Store((l_Cluster * c_MaxClusterLights + l_Count) * 4, it_Light);
        }

        ++l_Count;
    }

    RWByteAddressBuffer l_Counts = g_Push.Counts;
    l_Counts.Store(l_Cluster * 4, l_Count);
}