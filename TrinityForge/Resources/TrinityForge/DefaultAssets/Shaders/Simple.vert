#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragPos;

layout(binding = 0) uniform UniformBufferObject
{
    mat4 Model;
    mat4 View;
    mat4 Proj;
} ubo;

void main()
{
    vec4 worldPos = ubo.Model * vec4(inPosition, 1.0);
    fragPos = worldPos.xyz;
    gl_Position = ubo.Proj * ubo.View * worldPos;
    // Convert vertex color from sRGB to linear if it's in sRGB space (assuming it is, per common practice and blog guidance)
    fragColor = pow(inColor, vec3(2.2));
    fragTexCoord = inTexCoord;
}