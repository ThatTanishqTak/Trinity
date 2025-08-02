#include "Trinity/trpch.h"

#include "Trinity/ECS/Components.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Trinity
{
    glm::mat4 Transform::GetTransform() const
    {
        glm::mat4 l_Rotation = glm::mat4(1.0f);
        l_Rotation = glm::rotate(l_Rotation, glm::radians(Rotation.x), { 1.0f, 0.0f, 0.0f });
        l_Rotation = glm::rotate(l_Rotation, glm::radians(Rotation.y), { 0.0f, 1.0f, 0.0f });
        l_Rotation = glm::rotate(l_Rotation, glm::radians(Rotation.z), { 0.0f, 0.0f, 1.0f });

        return glm::translate(glm::mat4(1.0f), Translation) * l_Rotation * glm::scale(glm::mat4(1.0f), Scale);
    }
}