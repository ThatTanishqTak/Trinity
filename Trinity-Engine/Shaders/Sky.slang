// The sky a scene's Environment shows behind everything a perspective view sees, drawn by Renderer3D at the end of the opaque pass as one triangle over the whole target at the far depth, so the equal depth test passes only where no surface was drawn. Each pixel shows the environment's radiance at its sharpest along the eye ray through it, turned and scaled as lighting looks it up, writing no depth
#include "Bindless.slang"

// The inverse of the projection times the view's turn alone, as four columns, as glm stores them, which takes a point of the near plane to the way the eye looks through it, then the specular cubemap and its sampler, the environment's intensity, and the cosine and sine of its turn
struct PushData
{
    float4 InverseViewProjection[4];
    DescriptorHandle<TextureCube> Environment;
    DescriptorHandle<SamplerState> Sampler;
    float Intensity;
    float Cosine;
    float Sine;
    float Padding;
};

[[vk::push_constant]] ConstantBuffer<PushData> g_Push;

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 Clip : TEXCOORD0;
};

// Three vertices whose triangle covers the target, at depth 0, which is the far end of reversed depth
[shader("vertex")]
VertexOutput VertexMain(uint vertexIndex: SV_VertexID)
{
    const float2 l_Corner = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    const float2 l_Clip = l_Corner * 2.0 - 1.0;

    VertexOutput l_Output;
    l_Output.Position = float4(l_Clip, 0.0, 1.0);
    l_Output.Clip = l_Clip;

    return l_Output;
}

// The near plane is at depth 1 in reversed depth, so its point through the pixel, with the view's position left out, lies along the eye ray. The environment is turned about +Y as Mesh.slang's RotateToEnvironment turns what it looks up
float3 SampleSky(float2 clip)
{
    const float4 l_Near = g_Push.InverseViewProjection[0] * clip.x + g_Push.InverseViewProjection[1] * clip.y + g_Push.InverseViewProjection[2] + g_Push.InverseViewProjection[3];
    const float3 l_Direction = normalize(l_Near.xyz / l_Near.w);
    const float3 l_Turned = float3(g_Push.Cosine * l_Direction.x + g_Push.Sine * l_Direction.z, l_Direction.y, g_Push.Cosine * l_Direction.z - g_Push.Sine * l_Direction.x);
    TextureCube l_Environment = g_Push.Environment;
    SamplerState l_Sampler = g_Push.Sampler;

    return l_Environment.SampleLevel(l_Sampler, l_Turned, 0.0).rgb * g_Push.Intensity;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target
{
    return float4(SampleSky(input.Clip), 1.0);
}

struct PickOutput
{
    float4 Color : SV_Target0;
    uint Entity : SV_Target1;
};

// The colour, and no entity into the ID target beside it, as it was cleared, so a click on the sky picks nothing
[shader("fragment")]
PickOutput PickPixelMain(VertexOutput input)
{
    PickOutput l_Output;
    l_Output.Color = float4(SampleSky(input.Clip), 1.0);
    l_Output.Entity = 0;

    return l_Output;
}