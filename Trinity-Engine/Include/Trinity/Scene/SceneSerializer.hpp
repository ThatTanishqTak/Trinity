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
    class Entity;

    class TRINITY_API ComponentWriter
    {
    public:
        ComponentWriter(std::string& output, std::uint32_t indent);

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
        // A field with its type, on one line, such as Speed: {Type: Float, Value: 5}
        void Write(std::string_view key, const ScriptFieldValue& value);

        void WriteValue(std::string_view value);

        // A key whose value is a block sequence, written as [] when no item follows. Each BeginItem starts an item whose keys follow, until End closes the sequence
        void BeginList(std::string_view key);
        void BeginItem();
        // A key whose value is a block map of the keys that follow, written as {} when none does, until End
        void BeginMap(std::string_view key);
        // A key whose value is a map on one line, such as {Type: Float, Value: 5}, of the keys that follow until End
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

        std::string* m_Output = nullptr;
        std::uint32_t m_Indent = 0;
        std::array<Block, c_MaxDepth> m_Blocks{};
        std::uint32_t m_Depth = 0;
        bool m_ItemPending = false;
    };

    class TRINITY_API ComponentReader
    {
    public:
        ComponentReader(const YAML::Node& node, std::string_view componentName);

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

        // A field written with its type, read as the type the text gives, which replaces the value's own. False, leaving the value, when the key is missing or the type is not one this build has
        bool Read(std::string_view key, ScriptFieldValue& value) const;

        bool ReadValue(std::string& value) const;
        // This node as a field written with its type, as Read reads the value of a key. A malformed value reads as its type's zero
        bool ReadValue(ScriptFieldValue& value) const;

        // Each item of the sequence under the key, in order, read as a component is. False when the key is missing or not a sequence
        bool ReadList(std::string_view key, const std::function<void(const ComponentReader& item)>& read) const;
        // Each key of the map under the key, in file order, with a reader of its value. False when the key is missing or not a map
        bool ReadMap(std::string_view key, const std::function<void(std::string_view name, const ComponentReader& value)>& read) const;

    private:
        const YAML::Node* m_Node = nullptr;
        std::string_view m_ComponentName;
    };

    // How a component saves and loads, and how a scene makes its pool, which for a module's component is the module's code. The name, and every function, must stay valid until the component is unregistered
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

        // The prefab's template, its entities under their local IDs, by the prefab's UUID, or null for a prefab that is not there or cannot be read, whose instances are kept as their text. The template must stay until the text that reads it is read, and while a scene holds instances of it that are saved
        using PrefabResolver = std::function<Scene* (UUID prefab)>;

        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();

        // Until one is set, and once an empty one is, a prefab is found among the assets the asset manager has ready
        TRINITY_API void SetPrefabResolver(PrefabResolver resolver);

        // Gives every scene there is the component's pool, and loads again each of the component kept as YAML while no loaded code knew it
        TRINITY_API bool RegisterComponent(const ComponentSerializer& serializer);
        // Keeps each of the component in every scene there is as YAML, as a scene file read without its code would, and drops its pool, which a module's code destroys, so unregister before that code unloads
        TRINITY_API void UnregisterComponent(std::string_view name);
        [[nodiscard]] TRINITY_API std::vector<std::string_view> GetComponentNames();

        [[nodiscard]] TRINITY_API std::string SaveToText(Scene& scene);

        [[nodiscard]] TRINITY_API Expected<void, std::string> LoadFromText(Scene& scene, std::string_view text);
        [[nodiscard]] TRINITY_API std::string SaveEntityToText(Scene& scene, Entity root);
        [[nodiscard]] TRINITY_API Expected<Entity, std::string> LoadEntityFromText(Scene& scene, std::string_view text, Entity parent, Entity before);

        [[nodiscard]] TRINITY_API bool HasComponent(Entity entity, std::string_view name);
        TRINITY_API bool AddComponent(Entity entity, std::string_view name);
        TRINITY_API bool RemoveComponent(Entity entity, std::string_view name);
        [[nodiscard]] TRINITY_API std::string SaveComponentToText(Entity entity, std::string_view name);
        [[nodiscard]] TRINITY_API Expected<void, std::string> LoadComponentFromText(Entity entity, std::string_view name, std::string_view text);

        // A .trprefab's text: the entity and its subtree under the local IDs the instance's UUID tells back from theirs, so once the entity is that instance's root its entities keep their UUIDs. An entity named outside the subtree is cleared, since a prefab names none, and the instance keeps it as what it changes. Refused for a subtree that holds a prefab instance, which a prefab cannot hold yet
        [[nodiscard]] TRINITY_API Expected<std::string, std::string> SavePrefabToText(Scene& scene, Entity root, UUID instance);
        // The prefab's entities under their local IDs, in place of the scene's own, as a template that instances are made from
        [[nodiscard]] TRINITY_API Expected<void, std::string> LoadPrefabFromText(Scene& prefab, std::string_view text);
        // A new instance of the prefab, unchanged, its root put as LoadEntityFromText puts one. Refused for a prefab the resolver does not find, or when an entity of the instance's UUIDs is already in the scene
        [[nodiscard]] TRINITY_API Expected<Entity, std::string> InstantiatePrefab(Scene& scene, UUID prefab, UUID instance, Entity parent, Entity before);
        // The prefabs the text's instances are of, each once, so they can be loaded before the text is. None for text that is not valid YAML
        [[nodiscard]] TRINITY_API std::vector<UUID> GetPrefabReferences(std::string_view text);

        // One thing an instance changes of its prefab: a component's value at a key path, or the whole component for no path, as the scene file keeps them, a component it adds or removes, one of its prefab's entities it removes, named by the UUID it would have, or an entity it adds under one of them, named by its own UUID
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

        // An instance as its root's text, with the parent and the sibling after it, so it can be made again where it is
        struct PrefabInstanceText
        {
            UUID Root;
            UUID Parent;
            UUID Before;
            std::string Text;
        };

        // What the instance changes of its prefab, as a save would keep it, entity by entity in its prefab's order, each entity's values and components before the entities added under it, and a removed entity once for its whole subtree. None for an entity that is no instance's root, or one whose prefab the resolver does not find
        [[nodiscard]] TRINITY_API std::vector<PrefabOverride> GetPrefabOverrides(Entity root);
        // Each of the overrides undone on the instance as it is, so what it changed is as its prefab has it: a removed entity made again in its prefab's place among its siblings, and an added one destroyed. Refused, changing nothing, for an override the instance does not have or one given twice, or a root as GetPrefabOverrides refuses
        [[nodiscard]] TRINITY_API Expected<void, std::string> RevertPrefabOverrides(Entity root, std::span<const PrefabOverride> overrides);
        // The prefab's text with the overrides made its own, for the caller to write as its file: values, components and added entities as the instance has them, and what it removes gone. An added entity keeps its UUID in this instance, its local ID told back from it. What a value names outside the instance is cleared, as SavePrefabToText clears it. The instance is left as it is. Refused as RevertPrefabOverrides is, and for an added entity that is or holds an instance of a prefab
        [[nodiscard]] TRINITY_API Expected<std::string, std::string> ApplyPrefabOverrides(Entity root, std::span<const PrefabOverride> overrides);
        // Each instance of the prefab in the scene, saved against the template the resolver finds now, outermost first, and one inside another of the same prefab saved within that one
        [[nodiscard]] TRINITY_API std::vector<PrefabInstanceText> SavePrefabInstances(Scene& scene, UUID prefab);
        // Each instance made again from its text where it was, with the template the resolver finds now, so it keeps what it changes of its prefab, the entities added under it and every UUID. The first that could not be made, with the rest made still
        [[nodiscard]] TRINITY_API Expected<void, std::string> RestorePrefabInstances(Scene& scene, std::span<const PrefabInstanceText> instances);

        [[nodiscard]] TRINITY_API Expected<void, std::string> Save(Scene& scene, std::string_view path);
        [[nodiscard]] TRINITY_API Expected<void, std::string> Load(Scene& scene, std::string_view path);
    }
}