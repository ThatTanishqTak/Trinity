#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(binding = 1) uniform sampler2D texSampler;

layout(binding = 2) uniform Light
{
    vec3 Position;
    vec3 Color;
} light;

layout(binding = 3) uniform Material
{
    vec3 Albedo;
    float Roughness;
} material;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 norm = vec3(0.0, 0.0, 1.0);
    vec3 lightDir = normalize(light.Position);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * light.Color * material.Albedo;
    vec3 ambient = 0.1 * material.Albedo;
    vec3 texColor = texture(texSampler, fragTexCoord).rgb;
    vec3 color = (ambient + diffuse) * texColor;
    outColor = vec4(color, 1.0);
}