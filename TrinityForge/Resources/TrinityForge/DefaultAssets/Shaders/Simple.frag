#version 450
#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragPos;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 5) uniform sampler2D normalMap;
layout(binding = 6) uniform sampler2D roughnessMap;
layout(binding = 7) uniform sampler2D metallicMap;

layout (constant_id = 0) const int MAX_LIGHTS = 4;
struct Light
{
    vec4 Position;
    vec4 Color;
    int Type;
    float Intensity;
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
    float Metallic;
    float Specular;
} material;

layout(binding = 4) uniform sampler2DShadow shadowMap;

layout(location = 0) out vec4 outColor;

vec3 CalculateLight(Light light, vec3 normal, vec3 viewDir, float roughness, float metallic, vec3 albedoLinear)
{
    vec3 lightDir = light.Type == 0 ? normalize(-light.Position.xyz) : normalize(light.Position.xyz - fragPos);
    // Convert light color from sRGB to linear (assuming it's authored in sRGB)
    vec3 radiance = pow(light.Color.xyz, vec3(2.2)) * light.Intensity;

    vec3 F0 = mix(vec3(0.04), albedoLinear, metallic);
    vec3 F = F0;
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = kD * albedoLinear / 3.14159265;

    float spec = pow(max(dot(normal, normalize(lightDir + viewDir)), 0.0), 16.0 * (1.0 - roughness));
    vec3 specular = spec * F;

    return (diffuse + specular) * radiance * NdotL;
}

void main()
{
    vec3 N = normalize(texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0);
    vec3 V = normalize(-fragPos);
    // Assuming texSampler is set up as sRGB format in Vulkan (VK_FORMAT_R8G8B8A8_SRGB), so sampling yields linear values
    vec3 texColor = texture(texSampler, fragTexCoord).rgb;
    float roughnessTex = texture(roughnessMap, fragTexCoord).r;
    float metallicTex = texture(metallicMap, fragTexCoord).r;
    float roughness = material.Roughness * roughnessTex;
    float metallic = material.Metallic * metallicTex;
    // Convert material Albedo from sRGB to linear (assuming it's authored in sRGB)
    vec3 albedoLinear = pow(material.Albedo, vec3(2.2));
    vec3 baseColor = texColor * albedoLinear;

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < lightBuffer.LightCount; ++i)
    {
        Lo += CalculateLight(lightBuffer.Lights[i], N, V, roughness, metallic, albedoLinear);
    }

    vec3 ambient = vec3(0.03) * baseColor;
    vec3 color = ambient + Lo;

    // Tonemapping (assumes HDR input in linear space)
    color = color / (color + vec3(1.0));

    // Removed pow(color, vec3(1.0 / 2.2)) - output linear color, assuming sRGB swapchain format (VK_FORMAT_B8G8R8A8_SRGB) which handles linear-to-sRGB automatically

    float shadow = texture(shadowMap, vec3(fragTexCoord, 1.0));
    outColor = vec4(color * shadow, material.Specular);
}