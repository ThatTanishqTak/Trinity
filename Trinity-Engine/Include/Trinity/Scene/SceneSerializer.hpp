#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Scene/ComponentType.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace YAML
{
    class Node;
}

namespace Trinity
{
    class Scene;

    class TRINITY_API ComponentWriter
    {
    public:
        ComponentWriter(std::string& output, std::uint32_t indent);

        void Write(std::string_view key, bool value);
        void Write(std::string_view key, std::int32_t value);
        void Write(std::string_view key, std::uint32_t value);
        void Write(std::string_view key, float value);
        void Write(std::string_view key, const char* value);
        void Write(std::string_view key, std::string_view value);
        void Write(std::string_view key, UUID value);
        void Write(std::string_view key, const glm::vec2& value);
        void Write(std::string_view key, const glm::vec3& value);
        void Write(std::string_view key, const glm::vec4& value);
        void Write(std::string_view key, const glm::quat& value);

        void WriteValue(std::string_view value);

    private:
        void WriteKey(std::string_view key);

        std::string* m_Output = nullptr;
        std::uint32_t m_Indent = 0;
    };

    class TRINITY_API ComponentReader
    {
    public:
        ComponentReader(const YAML::Node& node, std::string_view componentName);

        bool Read(std::string_view key, bool& value) const;
        bool Read(std::string_view key, std::int32_t& value) const;
        bool Read(std::string_view key, std::uint32_t& value) const;
        bool Read(std::string_view key, float& value) const;
        bool Read(std::string_view key, std::string& value) const;
        bool Read(std::string_view key, UUID& value) const;
        bool Read(std::string_view key, glm::vec2& value) const;
        bool Read(std::string_view key, glm::vec3& value) const;
        bool Read(std::string_view key, glm::vec4& value) const;
        bool Read(std::string_view key, glm::quat& value) const;

        bool ReadValue(std::string& value) const;

    private:
        const YAML::Node* m_Node = nullptr;
        std::string_view m_ComponentName;
    };

    struct ComponentSerializer
    {
        std::string_view Name;
        void (*Save)(const void* component, ComponentWriter& writer) = nullptr;
        void (*Load)(void* component, const ComponentReader& reader) = nullptr;
    };

    template<Component T, auto SaveFunction, auto LoadFunction>
    [[nodiscard]] ComponentSerializer MakeComponentSerializer()
    {
        return { T::c_TypeName, [](const void* component, ComponentWriter& writer) { SaveFunction(*static_cast<const T*>(component), writer); }, [](void* component, const ComponentReader& reader) { LoadFunction(*static_cast<T*>(component), reader); } };
    }

    namespace SceneSerializer
    {
        constexpr std::uint32_t c_FormatVersion = 1;

        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();

        TRINITY_API bool RegisterComponent(const ComponentSerializer& serializer);
        TRINITY_API void UnregisterComponent(std::string_view name);

        [[nodiscard]] TRINITY_API std::string SaveToText(Scene& scene);

        [[nodiscard]] TRINITY_API Expected<void, std::string> LoadFromText(Scene& scene, std::string_view text);
        [[nodiscard]] TRINITY_API Expected<void, std::string> Save(Scene& scene, std::string_view path);
        [[nodiscard]] TRINITY_API Expected<void, std::string> Load(Scene& scene, std::string_view path);
    }
}