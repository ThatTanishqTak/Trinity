#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(binding = 1) uniform sampler2D texSampler;

const int MAX_LIGHTS = 4;
struct Light
{
    vec4 Position;
    vec4 Color;
};
layout(binding = 2) uniform LightBuffer
{
    Light Lights[MAX_LIGHTS];
    int LightCount;
} lightBuffer;

layout(binding = 3) uniform Material
{
    vec3 Albedo;
    float Roughness;
} material;

layout(binding = 4) uniform sampler2DShadow shadowMap;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 norm = vec3(0.0, 0.0, 1.0);
    vec3 texColor = texture(texSampler, fragTexCoord).rgb;
    vec3 color = vec3(0.1) * material.Albedo * texColor;
    for (int i = 0; i < lightBuffer.LightCount; ++i)
    {
        vec3 lightDir = normalize(lightBuffer.Lights[i].Position.xyz);
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * lightBuffer.Lights[i].Color.xyz * material.Albedo;
        color += diffuse * texColor;
    }
    float shadow = texture(shadowMap, vec3(fragTexCoord, 1.0));
    outColor = vec4(color * shadow, 1.0);
}