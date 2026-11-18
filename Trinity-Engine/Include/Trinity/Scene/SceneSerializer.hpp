#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Scene/ComponentType.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace YAML
{
    class Node;
}

namespace Trinity
{
    class BinaryNode;
    class BinaryTreeBuilder;
    class Entity;
    struct ModelData;

    class TRINITY_API ComponentWriter
    {
    public:
        ComponentWriter(std::string& output, std::uint32_t indent);
        // Into a binary scene's tree, as the same keys and values the text would have
        explicit ComponentWriter(BinaryTreeBuilder& builder);

        void Write(std::string_view key, bool value);
        void Write(std::string_view key, std::int32_t value);
        void Write(std::string_view key, std::uint32_t value);
        void Write(std::string_view key, std::int64_t value);
        void Write(std::string_view key, std::uint64_t value);
        void Write(std::string_view key, float value);
        void Write(std::string_view key, double value);
        void Write(std::string_view key, const char* value);
        void Write(std::string_view key, std::string_view value);
        void Write(std::string_view key, UUID value);
        void Write(std::string_view key, const glm::vec2& value);
        void Write(std::string_view key, const glm::vec3& value);
        void Write(std::string_view key, const glm::vec4& value);
        void Write(std::string_view key, const glm::quat& value);
        void Write(std::string_view key, std::span<const UUID> values);
        void Write(std::string_view key, std::span<const glm::vec2> values);
        // An item a line, so a prefab instance can change one item by index. Not inside an inline map
        void WriteList(std::string_view key, std::span<const UUID> values);
        // A field with its type, on one line, such as Speed: {Type: Float, Value: 5}
        void Write(std::string_view key, const ScriptFieldValue& value);

        void WriteValue(std::string_view value);

        // Written as [] when no item follows. Each BeginItem starts an item, until End
        void BeginList(std::string_view key);
        void BeginItem();
        // Written as {} when no key follows, until End
        void BeginMap(std::string_view key);
        // A map on one line, such as {Type: Float, Value: 5}, of the keys that follow until End
        void BeginInlineMap(std::string_view key);
        void End();

    private:
        enum class BlockKind : std::uint8_t
        {
            List,
            Map,
            InlineMap
        };

        struct Block
        {
            BlockKind Kind = BlockKind::Map;
            std::uint32_t Indent = 0;
            bool Empty = true;
        };

        static constexpr std::size_t c_MaxDepth = 8;

        void WriteKey(std::string_view key);
        void WriteKeyName(std::string_view key);
        void Push(BlockKind kind);
        // A value written as it is, such as a number, or as text YAML quotes where it must
        void WritePlain(std::string_view key, std::string_view text);
        void WriteString(std::string_view key, std::string_view text);
        void WriteFloats(std::string_view key, std::span<const float> values);

        std::string* m_Output = nullptr;
        BinaryTreeBuilder* m_Binary = nullptr;
        std::uint32_t m_Indent = 0;
        std::array<Block, c_MaxDepth> m_Blocks{};
        std::uint32_t m_Depth = 0;
        bool m_ItemPending = false;
    };

    class TRINITY_API ComponentReader
    {
    public:
        ComponentReader(const YAML::Node& node, std::string_view componentName);
        // From a binary scene's tree, read as the YAML of the same text is
        ComponentReader(const BinaryNode& node, std::string_view componentName);

        bool Read(std::string_view key, bool& value) const;
        bool Read(std::string_view key, std::int32_t& value) const;
        bool Read(std::string_view key, std::uint32_t& value) const;
        bool Read(std::string_view key, std::int64_t& value) const;
        bool Read(std::string_view key, std::uint64_t& value) const;
        bool Read(std::string_view key, float& value) const;
        bool Read(std::string_view key, double& value) const;
        bool Read(std::string_view key, std::string& value) const;
        bool Read(std::string_view key, UUID& value) const;
        bool Read(std::string_view key, glm::vec2& value) const;
        bool Read(std::string_view key, glm::vec3& value) const;
        bool Read(std::string_view key, glm::vec4& value) const;
        bool Read(std::string_view key, glm::quat& value) const;
        bool Read(std::string_view key, std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scene>>& values) const;
        bool Read(std::string_view key, std::vector<glm::vec2, TaggedAllocator<glm::vec2, MemoryTag::Scene>>& values) const;

        // False, leaving the value, when the key is missing or the type is not one this build has
        bool Read(std::string_view key, ScriptFieldValue& value) const;

        bool ReadValue(std::string& value) const;
        // A malformed value reads as its type's zero
        bool ReadValue(ScriptFieldValue& value) const;

        // False when the key is missing or not a sequence
        bool ReadList(std::string_view key, const std::function<void(const ComponentReader& item)>& read) const;
        // In file order. False when the key is missing or not a map
        bool ReadMap(std::string_view key, const std::function<void(std::string_view name, const ComponentReader& value)>& read) const;

    private:
        // The function given whichever node the reader reads, which the same helpers read
        template<typename F>
        bool OnNode(F&& function) const;

        const YAML::Node* m_Node = nullptr;
        const BinaryNode* m_Binary = nullptr;
        std::string_view m_ComponentName;
    };

    // The name and every function must stay valid until the component is unregistered
    struct ComponentSerializer
    {
        std::string_view Name;
        void (*Save)(const void* component, ComponentWriter& writer) = nullptr;
        void (*Load)(void* component, const ComponentReader& reader) = nullptr;
        void (*CreateStorage)(SceneRegistry& registry) = nullptr;
    };


    template<Component T, auto SaveFunction, auto LoadFunction>
    [[nodiscard]] ComponentSerializer MakeComponentSerializer()
    {
        return { T::c_TypeName, [](const void* component, ComponentWriter& writer) { SaveFunction(*static_cast<const T*>(component), writer); }, [](void* component, const ComponentReader& reader) { LoadFunction(*static_cast<T*>(component), reader); }, [](SceneRegistry& registry) { static_cast<void>(registry.storage<T>()); } };
    }

    namespace SceneSerializer
    {
        // Format 2 added prefab instances, so a format 1 scene reads as it is and is written as format 2
        constexpr std::uint32_t c_FormatVersion = 2;
        // YAML's tree without its text, written and read by the same component serializers
        constexpr std::uint32_t c_BinaryFormatVersion = 1;

        // Null for a missing or unreadable prefab. The template must outlive the text read against it
        using PrefabResolver = std::function<Scene* (UUID prefab)>;

        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();

        // With none set, a prefab is found among the assets the asset manager has ready
        TRINITY_API void SetPrefabResolver(PrefabResolver resolver);
        // Null for a missing prefab, or one asked for while its own template is found. Main thread only
        [[nodiscard]] TRINITY_API Scene* FindPrefabTemplate(UUID prefab);

        // Gives every live scene the pool, and loads each of the component that was kept as YAML
        TRINITY_API bool RegisterComponent(const ComponentSerializer& serializer);
        // Keeps the component as YAML in every live scene and drops its pool. Call before its code unloads
        TRINITY_API void UnregisterComponent(std::string_view name);
        [[nodiscard]] TRINITY_API std::vector<std::string_view> GetComponentNames();

        [[nodiscard]] TRINITY_API std::string SaveToText(Scene& scene);

        [[nodiscard]] TRINITY_API Expected<void, std::string> LoadFromText(Scene& scene, std::string_view text);
        // In hierarchy order. An instance's root says it is one unless flattened, as a cooked scene is
        [[nodiscard]] TRINITY_API std::vector<std::byte> SaveToBinary(Scene& scene, bool flatten = false);
        // A component no loaded code knows is kept as YAML. Refused when not whole or from a newer Trinity
        [[nodiscard]] TRINITY_API Expected<void, std::string> LoadFromBinary(Scene& scene, std::span<const std::byte> data);
        // Whether the data is a binary scene or prefab rather than a file's text, by its first bytes
        [[nodiscard]] TRINITY_API bool IsBinary(std::span<const std::byte> data);
        [[nodiscard]] TRINITY_API std::string SaveEntityToText(Scene& scene, Entity root);
        [[nodiscard]] TRINITY_API Expected<Entity, std::string> LoadEntityFromText(Scene& scene, std::string_view text, Entity parent, Entity before);

        [[nodiscard]] TRINITY_API bool HasComponent(Entity entity, std::string_view name);
        TRINITY_API bool AddComponent(Entity entity, std::string_view name);
        TRINITY_API bool RemoveComponent(Entity entity, std::string_view name);
        [[nodiscard]] TRINITY_API std::string SaveComponentToText(Entity entity, std::string_view name);
        [[nodiscard]] TRINITY_API Expected<void, std::string> LoadComponentFromText(Entity entity, std::string_view name, std::string_view text);

        // The subtree under local IDs told back from its UUIDs, so its entities keep them as an instance
        [[nodiscard]] TRINITY_API Expected<std::string, std::string> SavePrefabToText(Scene& scene, Entity root, UUID instance);
        // As prefab mode saves the prefab it edits. Refused for a template of more than one root
        [[nodiscard]] TRINITY_API Expected<std::string, std::string> SavePrefabTemplateToText(Scene& prefab);
        // Local IDs come from each node's name path, so a reimport keeps the ID of a node whose path stays
        [[nodiscard]] TRINITY_API std::string WriteModelPrefab(const ModelData& model);
        // Loads the prefab's entities as a template. An instance of a prefab that holds it is refused
        [[nodiscard]] TRINITY_API Expected<void, std::string> LoadPrefabFromText(Scene& prefab, std::string_view text, UUID asset = {});
        // As SaveToBinary saved it unflattened, so it waits on no prefab. Refused unless one root
        [[nodiscard]] TRINITY_API Expected<void, std::string> LoadPrefabFromBinary(Scene& prefab, std::span<const std::byte> data);
        // Refused for a prefab the resolver does not find, or when the instance's UUIDs are in the scene
        [[nodiscard]] TRINITY_API Expected<Entity, std::string> InstantiatePrefab(Scene& scene, UUID prefab, UUID instance, Entity parent, Entity before);
        // Each once, so they can be loaded before the text is. None for text that is not valid YAML
        [[nodiscard]] TRINITY_API std::vector<UUID> GetPrefabReferences(std::string_view text);
        // The same of binary data. None of a flattened scene, or of data that is not whole
        [[nodiscard]] TRINITY_API std::vector<UUID> GetBinaryPrefabReferences(std::span<const std::byte> data);

        // One change an instance makes to its prefab: a value, a component or an entity added or removed
        struct PrefabOverride
        {
            enum class Kind : std::uint8_t
            {
                Value,
                AddedComponent,
                RemovedComponent,
                RemovedEntity,
                AddedEntity
            };

            Kind Type = Kind::Value;
            UUID Entity;
            std::string Component;
            std::string Path;

            [[nodiscard]] bool operator==(const PrefabOverride&) const = default;
        };

        // A prefab's text, as ApplyPrefabOverrides gives it for each prefab it changes
        struct PrefabText
        {
            UUID Prefab;
            std::string Text;
        };

        // With the parent and the sibling after it, so it can be made again where it was
        struct PrefabInstanceText
        {
            UUID Root;
            UUID Parent;
            UUID Before;
            std::string Text;
        };

        // In the prefab's order, as a save would keep them. None for an entity that is no instance's root
        [[nodiscard]] TRINITY_API std::vector<PrefabOverride> GetPrefabOverrides(Entity root);
        // Undoes each override on the instance. Refused, changing nothing, for one it does not have
        [[nodiscard]] TRINITY_API Expected<void, std::string> RevertPrefabOverrides(Entity root, std::span<const PrefabOverride> overrides);
        // The prefab's text with the overrides made its own, for the caller to write. The instance stays
        [[nodiscard]] TRINITY_API Expected<std::string, std::string> ApplyPrefabOverrides(Entity root, std::span<const PrefabOverride> overrides);
        // Outermost first, as Unity's Apply to Prefab offers them. The index is the level to apply at
        [[nodiscard]] TRINITY_API std::vector<UUID> GetOverrideLevels(Entity root, const PrefabOverride & override);
        // Each changed prefab's text, innermost first, for the caller to write in that order
        [[nodiscard]] TRINITY_API Expected<std::vector<PrefabText>, std::string> ApplyPrefabOverrides(Entity root, std::span<const PrefabOverride> overrides, std::size_t level);
        // Saves each instance of the prefab, and of prefabs that hold it, so it can be made again
        [[nodiscard]] TRINITY_API std::vector<PrefabInstanceText> SavePrefabInstances(Scene& scene, UUID prefab);
        // Made again where it was, keeping overrides and UUIDs. Returns the first that could not be made
        [[nodiscard]] TRINITY_API Expected<void, std::string> RestorePrefabInstances(Scene& scene, std::span<const PrefabInstanceText> instances);

        [[nodiscard]] TRINITY_API Expected<void, std::string> Save(Scene& scene, std::string_view path);
        [[nodiscard]] TRINITY_API Expected<void, std::string> Load(Scene& scene, std::string_view path);
    }
}