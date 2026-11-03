#include "Trinity/UI/UIWorldSettings.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Trinity
{
    namespace
    {
        constexpr std::array<std::string_view, 2> c_RenderModeNames{ "Screen", "World" };
        constexpr std::array<std::string_view, 2> c_SortingNames{ "Depth", "Sprites" };
        constexpr std::array<std::string_view, 2> c_SizeModeNames{ "Fixed", "Dynamic" };
        // Below this squared length a camera's axis is taken to be missing, as in a view-projection that flattens the scene
        constexpr float c_MinAxisLength = 1e-12f;

        template<typename Enum, std::size_t Count>
        std::string_view GetName(const std::array<std::string_view, Count>& names, Enum value)
        {
            const std::size_t l_Index = static_cast<std::size_t>(value);

            return l_Index < names.size() ? names[l_Index] : "Invalid";
        }

        template<typename Enum, std::size_t Count>
        std::optional<Enum> FindName(const std::array<std::string_view, Count>& names, std::string_view name)
        {
            const auto a_Found = std::ranges::find(names, name);

            return a_Found != names.end() ? std::optional(static_cast<Enum>(a_Found - names.begin())) : std::nullopt;
        }
    }

    // The camera's right and up are the first two rows of its view-projection, as a projection without skew scales the view's rows and adds nothing across them. Up is made square to right, so a camera with a sheared transform still gives a flat turn. A view-projection with no right or up leaves the entity's rotation
    glm::mat4 GetWorldUIMatrix(const UIWorldSettings& settings, glm::uvec2 pixels, const glm::mat4& world, const glm::mat4& viewProjection)
    {
        const float l_PixelsPerUnit = std::isfinite(settings.PixelsPerUnit) ? std::clamp(settings.PixelsPerUnit, UIWorldSettings::c_MinPixelsPerUnit, UIWorldSettings::c_MaxPixelsPerUnit) : UIWorldSettings().PixelsPerUnit;
        const glm::vec2 l_Size = glm::vec2(pixels) / l_PixelsPerUnit;

        glm::mat4 l_Quad(1.0f);
        l_Quad[0][0] = l_Size.x;
        l_Quad[1][1] = l_Size.y;
        l_Quad[3] = glm::vec4((0.5f - settings.Pivot.x) * l_Size.x, (0.5f - settings.Pivot.y) * l_Size.y, 0.0f, 1.0f);
        if (!settings.FaceCamera)
        {
            return world * l_Quad;
        }

        const glm::vec3 l_Right(viewProjection[0][0], viewProjection[1][0], viewProjection[2][0]);
        const glm::vec3 l_Up(viewProjection[0][1], viewProjection[1][1], viewProjection[2][1]);
        if (glm::dot(l_Right, l_Right) < c_MinAxisLength)
        {
            return world * l_Quad;
        }

        const glm::vec3 l_X = glm::normalize(l_Right);
        const glm::vec3 l_Square = l_Up - glm::dot(l_Up, l_X) * l_X;
        if (glm::dot(l_Square, l_Square) < c_MinAxisLength)
        {
            return world * l_Quad;
        }

        const glm::vec3 l_Y = glm::normalize(l_Square);
        const glm::vec3 l_Z = glm::cross(l_X, l_Y);

        glm::mat4 l_Facing(1.0f);
        l_Facing[0] = glm::vec4(l_X * glm::length(glm::vec3(world[0])), 0.0f);
        l_Facing[1] = glm::vec4(l_Y * glm::length(glm::vec3(world[1])), 0.0f);
        l_Facing[2] = glm::vec4(l_Z * glm::length(glm::vec3(world[2])), 0.0f);
        l_Facing[3] = world[3];

        return l_Facing * l_Quad;
    }

    std::string_view ToString(UIRenderMode mode)
    {
        return GetName(c_RenderModeNames, mode);
    }

    std::optional<UIRenderMode> FindUIRenderMode(std::string_view name)
    {
        return FindName<UIRenderMode>(c_RenderModeNames, name);
    }

    std::string_view ToString(UIWorldSorting sorting)
    {
        return GetName(c_SortingNames, sorting);
    }

    std::optional<UIWorldSorting> FindUIWorldSorting(std::string_view name)
    {
        return FindName<UIWorldSorting>(c_SortingNames, name);
    }

    std::string_view ToString(UIWorldSizeMode mode)
    {
        return GetName(c_SizeModeNames, mode);
    }

    std::optional<UIWorldSizeMode> FindUIWorldSizeMode(std::string_view name)
    {
        return FindName<UIWorldSizeMode>(c_SizeModeNames, name);
    }
}