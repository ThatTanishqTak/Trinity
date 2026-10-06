#include "Trinity/Scene/SceneSerializer.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <span>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        constexpr std::string_view c_EntityKey = "Entity";
        constexpr std::string_view c_ParentKey = "Parent";

        using MigrationFunction = void (*)(YAML::Node& root);

        // Entry N - 1 turns a format N file into format N + 1
        constexpr std::array<MigrationFunction, SceneSerializer::c_FormatVersion - 1> c_Migrations{};

        struct State
        {
            std::vector<ComponentSerializer, TaggedAllocator<ComponentSerializer, MemoryTag::Engine>> Serializers;
        };

        State* s_State = nullptr;

        std::string FormatFloat(float value)
        {
            std::array<char, 32> l_Buffer{};
            const std::to_chars_result l_Result = std::to_chars(l_Buffer.data(), l_Buffer.data() + l_Buffer.size(), value);

            return std::string(l_Buffer.data(), l_Result.ptr);
        }

        template<typename T>
        bool ParseNumber(std::string_view text, T& value)
        {
            T l_Value{};
            const std::from_chars_result l_Result = std::from_chars(text.data(), text.data() + text.size(), l_Value);
            if (l_Result.ec != std::errc() || l_Result.ptr != text.data() + text.size())
            {
                return false;
            }

            value = l_Value;

            return true;
        }

        std::string FormatFloats(std::span<const float> values)
        {
            std::string l_Text = "[";
            for (std::size_t it_Index = 0; it_Index < values.size(); ++it_Index)
            {
                l_Text += it_Index == 0 ? "" : ", ";
                l_Text += FormatFloat(values[it_Index]);
            }

            return l_Text + "]";
        }

        // Plain only when no YAML reader could take the text for a number, a boolean, a null or syntax. Everything else is double-quoted
        std::string FormatString(std::string_view text)
        {
            constexpr std::array<std::string_view, 9> c_Keywords{ "true", "false", "yes", "no", "on", "off", "null", "y", "n" };

            const auto a_IsLetter = [](char character) { return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_'; };
            const auto a_IsPlain = [&a_IsLetter](char character) { return a_IsLetter(character) || (character >= '0' && character <= '9') || character == ' ' || character == '.' || character == '-' || character == '/' || character == '(' || character == ')'; };

            std::string l_Lower(text);
            std::ranges::transform(l_Lower, l_Lower.begin(), [](char character) { return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character; });

            if (!text.empty() && a_IsLetter(text.front()) && text.back() != ' ' && std::ranges::all_of(text, a_IsPlain) && std::ranges::find(c_Keywords, l_Lower) == c_Keywords.end())
            {
                return std::string(text);
            }

            std::string l_Quoted = "\"";
            for (const char it_Character : text)
            {
                const unsigned char l_Byte = static_cast<unsigned char>(it_Character);
                if (it_Character == '"' || it_Character == '\\')
                {
                    l_Quoted += '\\';
                    l_Quoted += it_Character;
                }
                else if (it_Character == '\n')
                {
                    l_Quoted += "\\n";
                }
                else if (it_Character == '\t')
                {
                    l_Quoted += "\\t";
                }
                else if (l_Byte < 0x20 || l_Byte == 0x7F)
                {
                    l_Quoted += std::format("\\x{:02x}", l_Byte);
                }
                else
                {
                    l_Quoted += it_Character;
                }
            }

            return l_Quoted + "\"";
        }

        // Every line of the text, each indented, on lines of their own after the key
        void AppendIndented(std::string& output, std::string_view text, std::uint32_t indent)
        {
            std::size_t l_Start = 0;
            while (l_Start <= text.size())
            {
                const std::size_t l_End = std::min(text.find('\n', l_Start), text.size());
                output += '\n';
                output.append(indent, ' ');
                output.append(text.substr(l_Start, l_End - l_Start));
                l_Start = l_End + 1;
            }
        }

        // A preserved component as the scene file had it: a block map or sequence on indented lines, anything else after the key
        void AppendUnknown(std::string& output, std::string_view yaml, std::uint32_t indent)
        {
            const YAML::Node l_Node = YAML::Load(std::string(yaml));
            if (l_Node.IsNull())
            {
                return;
            }

            if ((l_Node.IsMap() || l_Node.IsSequence()) && l_Node.size() != 0 && l_Node.Style() != YAML::EmitterStyle::Flow)
            {
                AppendIndented(output, yaml, indent);

                return;
            }

            output += ' ';
            output += yaml;
        }

        bool ParseFloatSequence(const YAML::Node& node, std::span<float> values)
        {
            if (!node.IsSequence() || node.size() != values.size())
            {
                return false;
            }

            std::array<float, 4> l_Parsed{};
            for (std::size_t it_Index = 0; it_Index < values.size(); ++it_Index)
            {
                if (!node[it_Index].IsScalar() || !ParseNumber(node[it_Index].Scalar(), l_Parsed[it_Index]))
                {
                    return false;
                }
            }

            std::copy_n(l_Parsed.begin(), values.size(), values.begin());

            return true;
        }

        std::string ToHex(UUID uuid)
        {
            const std::array<char, 16> l_Hex = uuid.ToHex();

            return std::string(l_Hex.data(), l_Hex.size());
        }

        entt::id_type GetPoolId(std::string_view name)
        {
            return entt::hashed_string::value(name.data(), name.size());
        }

        const ComponentSerializer* FindSerializer(std::string_view name)
        {
            const auto a_Found = std::ranges::find(s_State->Serializers, name, &ComponentSerializer::Name);

            return a_Found != s_State->Serializers.end() ? &*a_Found : nullptr;
        }

        void SaveTag(const TagComponent& component, ComponentWriter& writer)
        {
            writer.WriteValue(component.Tag);
        }

        void LoadTag(TagComponent& component, const ComponentReader& reader)
        {
            std::string l_Tag;
            if (reader.ReadValue(l_Tag))
            {
                component.Tag = l_Tag;
            }
        }

        void SaveTransform(const TransformComponent& component, ComponentWriter& writer)
        {
            writer.Write("Position", component.Position);
            writer.Write("Rotation", component.Rotation);
            writer.Write("Scale", component.Scale);
        }

        void LoadTransform(TransformComponent& component, const ComponentReader& reader)
        {
            reader.Read("Position", component.Position);
            reader.Read("Rotation", component.Rotation);
            reader.Read("Scale", component.Scale);
        }

        void SaveCamera(const CameraComponent& component, ComponentWriter& writer)
        {
            writer.Write("OrthographicSize", component.OrthographicSize);
            writer.Write("Near", component.Near);
            writer.Write("Far", component.Far);
            writer.Write("Primary", component.Primary);
        }

        void LoadCamera(CameraComponent& component, const ComponentReader& reader)
        {
            reader.Read("OrthographicSize", component.OrthographicSize);
            reader.Read("Near", component.Near);
            reader.Read("Far", component.Far);
            reader.Read("Primary", component.Primary);
        }

        void SaveSpriteRenderer(const SpriteRendererComponent& component, ComponentWriter& writer)
        {
            writer.Write("Texture", component.Texture);
            writer.Write("Tint", component.Tint);
            writer.Write("FlipX", component.FlipX);
            writer.Write("FlipY", component.FlipY);
            writer.Write("UVRect", component.UVRect);
            writer.Write("SortingLayer", component.SortingLayer);
            writer.Write("OrderInLayer", component.OrderInLayer);
        }

        void LoadSpriteRenderer(SpriteRendererComponent& component, const ComponentReader& reader)
        {
            reader.Read("Texture", component.Texture);
            reader.Read("Tint", component.Tint);
            reader.Read("FlipX", component.FlipX);
            reader.Read("FlipY", component.FlipY);
            reader.Read("UVRect", component.UVRect);
            reader.Read("SortingLayer", component.SortingLayer);
            reader.Read("OrderInLayer", component.OrderInLayer);
        }

        struct FileEntity
        {
            UUID ID;
            UUID Parent;
            YAML::Node Node;
        };

        Expected<std::vector<FileEntity>, std::string> ReadEntities(const YAML::Node& root)
        {
            const YAML::Node l_Entities = root["Entities"];
            if (!l_Entities)
            {
                return std::vector<FileEntity>();
            }

            if (!l_Entities.IsSequence())
            {
                return Unexpected{ std::string("Entities is not a sequence") };
            }

            std::vector<FileEntity> l_Result;
            l_Result.reserve(l_Entities.size());
            std::unordered_map<UUID, UUID> l_Parents;
            for (std::size_t it_Index = 0; it_Index < l_Entities.size(); ++it_Index)
            {
                const YAML::Node l_Entry = l_Entities[it_Index];
                const YAML::Node l_ID = l_Entry.IsMap() ? l_Entry[std::string(c_EntityKey)] : YAML::Node();
                const std::optional<UUID> l_UUID = l_ID && l_ID.IsScalar() ? UUID::Parse(l_ID.Scalar()) : std::nullopt;
                if (!l_UUID || !l_UUID->IsValid())
                {
                    return Unexpected{ std::format("entity {} has no valid Entity UUID", it_Index) };
                }

                UUID l_Parent;
                if (const YAML::Node l_ParentNode = l_Entry[std::string(c_ParentKey)])
                {
                    const std::optional<UUID> l_ParentUUID = l_ParentNode.IsScalar() ? UUID::Parse(l_ParentNode.Scalar()) : std::nullopt;
                    if (!l_ParentUUID || !l_ParentUUID->IsValid())
                    {
                        return Unexpected{ std::format("entity {} has a malformed Parent", *l_UUID) };
                    }

                    l_Parent = *l_ParentUUID;
                }

                if (!l_Parents.emplace(*l_UUID, l_Parent).second)
                {
                    return Unexpected{ std::format("entity {} appears more than once", *l_UUID) };
                }

                l_Result.push_back({ *l_UUID, l_Parent, l_Entry });
            }

            // Every parent exists, and following parents from any entity reaches a root within as many steps as there are entities
            for (const FileEntity& it_Entity : l_Result)
            {
                UUID l_Current = it_Entity.Parent;
                for (std::size_t it_Step = 0; l_Current.IsValid(); ++it_Step)
                {
                    const auto a_Found = l_Parents.find(l_Current);
                    if (a_Found == l_Parents.end())
                    {
                        return Unexpected{ std::format("entity {} names parent {}, which is not in the file", it_Entity.ID, l_Current) };
                    }

                    if (it_Step > l_Result.size())
                    {
                        return Unexpected{ std::format("entity {} is its own ancestor", it_Entity.ID) };
                    }

                    l_Current = a_Found->second;
                }
            }

            return l_Result;
        }

        // Format and migrations first, so entity text kept by the editor reads exactly like a scene file
        Expected<YAML::Node, std::string> ParseDocument(std::string_view text)
        {
            YAML::Node l_Root;
            try
            {
                l_Root = YAML::Load(std::string(text));
            }
            catch (const YAML::Exception& exception)
            {
                return Unexpected{ std::format("not valid YAML: {}", exception.what()) };
            }

            const YAML::Node l_Format = l_Root.IsMap() ? l_Root["Format"] : YAML::Node();
            std::uint32_t l_Version = 0;
            if (!l_Format || !l_Format.IsScalar() || !ParseNumber(l_Format.Scalar(), l_Version) || l_Version == 0)
            {
                return Unexpected{ std::string("no valid Format version") };
            }

            if (l_Version > SceneSerializer::c_FormatVersion)
            {
                return Unexpected{ std::format("written in format {} by a newer Trinity, and this build reads format {} and older", l_Version, SceneSerializer::c_FormatVersion) };
            }

            for (std::uint32_t it_Version = l_Version; it_Version < SceneSerializer::c_FormatVersion; ++it_Version)
            {
                c_Migrations[it_Version - 1](l_Root);
            }

            return l_Root;
        }

        void AppendEntity(std::string& text, Entity entity, bool writeParent)
        {
            constexpr std::uint32_t c_FieldIndent = 6;

            SceneRegistry& l_Registry = entity.GetScene()->GetRegistry();

            text += std::format("\n  - {}: {}", c_EntityKey, ToHex(entity.GetUUID()));
            if (const Entity l_Parent = entity.GetParent(); writeParent && l_Parent)
            {
                text += std::format("\n    {}: {}", c_ParentKey, ToHex(l_Parent.GetUUID()));
            }

            for (const ComponentSerializer& it_Serializer : s_State->Serializers)
            {
                const SceneRegistry::common_type* l_Storage = l_Registry.storage(GetPoolId(it_Serializer.Name));
                if (l_Storage == nullptr || !l_Storage->contains(entity.GetHandle()))
                {
                    continue;
                }

                text += std::format("\n    {}:", FormatString(it_Serializer.Name));
                ComponentWriter l_Writer(text, c_FieldIndent);
                it_Serializer.Save(l_Storage->value(entity.GetHandle()), l_Writer);
            }

            if (entity.Has<UnknownComponentsComponent>())
            {
                for (const UnknownComponentsComponent::Entry& it_Unknown : entity.Get<UnknownComponentsComponent>().Entries)
                {
                    text += std::format("\n    {}:", FormatString(it_Unknown.Name));
                    AppendUnknown(text, it_Unknown.Yaml, c_FieldIndent);
                }
            }
        }

        void LoadComponent(Entity entity, const ComponentSerializer& serializer, SceneRegistry::common_type& storage, const YAML::Node& node)
        {
            if (!storage.contains(entity.GetHandle()))
            {
                storage.push(entity.GetHandle());
            }

            const ComponentReader l_Reader(node, serializer.Name);
            serializer.Load(storage.value(entity.GetHandle()), l_Reader);
        }

        // Every entity first, with its own UUID, then parents in file order, which is hierarchy order, so siblings keep theirs. Components no loaded code knows are kept as YAML and counted by name
        void CreateEntities(Scene& scene, const std::vector<FileEntity>& entities, std::unordered_map<std::string, std::size_t>& unknown)
        {
            SceneRegistry& l_Registry = scene.GetRegistry();

            for (const FileEntity& it_Entity : entities)
            {
                static_cast<void>(scene.CreateEntityWithUUID(it_Entity.ID));
            }

            for (const FileEntity& it_Entity : entities)
            {
                if (it_Entity.Parent.IsValid())
                {
                    scene.SetParent(scene.FindEntityByUUID(it_Entity.ID), scene.FindEntityByUUID(it_Entity.Parent), false);
                }
            }

            for (const FileEntity& it_Entity : entities)
            {
                Entity l_Entity = scene.FindEntityByUUID(it_Entity.ID);
                for (const auto& it_Pair : it_Entity.Node)
                {
                    const std::string& l_Name = it_Pair.first.Scalar();
                    if (l_Name == c_EntityKey || l_Name == c_ParentKey)
                    {
                        continue;
                    }

                    const ComponentSerializer* l_Serializer = FindSerializer(l_Name);
                    SceneRegistry::common_type* l_Storage = l_Serializer != nullptr ? l_Registry.storage(GetPoolId(l_Name)) : nullptr;
                    if (l_Storage == nullptr)
                    {
                        if (!l_Entity.Has<UnknownComponentsComponent>())
                        {
                            l_Entity.Add<UnknownComponentsComponent>();
                        }

                        l_Entity.Get<UnknownComponentsComponent>().Entries.push_back({ TaggedString<MemoryTag::Scene>(l_Name), TaggedString<MemoryTag::Scene>(YAML::Dump(it_Pair.second)) });
                        ++unknown[l_Name];

                        continue;
                    }

                    LoadComponent(l_Entity, *l_Serializer, *l_Storage, it_Pair.second);
                }
            }
        }

        // Only a component with a registered serializer, which code knows how to save and load
        SceneRegistry::common_type* FindStorage(Entity entity, std::string_view name)
        {
            return FindSerializer(name) != nullptr ? entity.GetScene()->GetRegistry().storage(GetPoolId(name)) : nullptr;
        }
    }

    ComponentWriter::ComponentWriter(std::string& output, std::uint32_t indent) : m_Output(&output), m_Indent(indent)
    {

    }

    void ComponentWriter::WriteKey(std::string_view key)
    {
        *m_Output += '\n';
        m_Output->append(m_Indent, ' ');
        *m_Output += FormatString(key);
        *m_Output += ": ";
    }

    void ComponentWriter::Write(std::string_view key, bool value)
    {
        WriteKey(key);
        *m_Output += value ? "true" : "false";
    }

    void ComponentWriter::Write(std::string_view key, std::int32_t value)
    {
        WriteKey(key);
        *m_Output += std::to_string(value);
    }

    void ComponentWriter::Write(std::string_view key, std::uint32_t value)
    {
        WriteKey(key);
        *m_Output += std::to_string(value);
    }

    void ComponentWriter::Write(std::string_view key, float value)
    {
        WriteKey(key);
        *m_Output += FormatFloat(value);
    }

    void ComponentWriter::Write(std::string_view key, const char* value)
    {
        Write(key, std::string_view(value));
    }

    void ComponentWriter::Write(std::string_view key, std::string_view value)
    {
        WriteKey(key);
        *m_Output += FormatString(value);
    }

    void ComponentWriter::Write(std::string_view key, UUID value)
    {
        WriteKey(key);
        *m_Output += ToHex(value);
    }

    void ComponentWriter::Write(std::string_view key, const glm::vec2& value)
    {
        WriteKey(key);
        *m_Output += FormatFloats(std::array{ value.x, value.y });
    }

    void ComponentWriter::Write(std::string_view key, const glm::vec3& value)
    {
        WriteKey(key);
        *m_Output += FormatFloats(std::array{ value.x, value.y, value.z });
    }

    void ComponentWriter::Write(std::string_view key, const glm::vec4& value)
    {
        WriteKey(key);
        *m_Output += FormatFloats(std::array{ value.x, value.y, value.z, value.w });
    }

    void ComponentWriter::Write(std::string_view key, const glm::quat& value)
    {
        WriteKey(key);
        *m_Output += FormatFloats(std::array{ value.x, value.y, value.z, value.w });
    }

    void ComponentWriter::WriteValue(std::string_view value)
    {
        *m_Output += ' ';
        *m_Output += FormatString(value);
    }

    ComponentReader::ComponentReader(const YAML::Node& node, std::string_view componentName) : m_Node(&node), m_ComponentName(componentName)
    {

    }

    namespace
    {
        std::optional<YAML::Node> FindValue(const YAML::Node& component, std::string_view key)
        {
            if (!component.IsMap())
            {
                return std::nullopt;
            }

            const YAML::Node l_Value = component[std::string(key)];
            if (!l_Value)
            {
                return std::nullopt;
            }

            return l_Value;
        }

        bool Malformed(std::string_view componentName, std::string_view key)
        {
            TR_CORE_WARN("Scene: {}.{} is malformed and keeps its default", componentName, key);

            return false;
        }

        template<typename T>
        bool ReadNumber(const YAML::Node& component, std::string_view componentName, std::string_view key, T& value)
        {
            const std::optional<YAML::Node> l_Value = FindValue(component, key);
            if (!l_Value)
            {
                return false;
            }

            return (l_Value->IsScalar() && ParseNumber(l_Value->Scalar(), value)) || Malformed(componentName, key);
        }

        template<std::size_t N>
        bool ReadFloats(const YAML::Node& component, std::string_view componentName, std::string_view key, std::span<float, N> values)
        {
            const std::optional<YAML::Node> l_Value = FindValue(component, key);
            if (!l_Value)
            {
                return false;
            }

            return ParseFloatSequence(*l_Value, values) || Malformed(componentName, key);
        }
    }

    bool ComponentReader::Read(std::string_view key, bool& value) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        if (l_Value->IsScalar() && (l_Value->Scalar() == "true" || l_Value->Scalar() == "false"))
        {
            value = l_Value->Scalar() == "true";

            return true;
        }

        return Malformed(m_ComponentName, key);
    }

    bool ComponentReader::Read(std::string_view key, std::int32_t& value) const
    {
        return ReadNumber(*m_Node, m_ComponentName, key, value);
    }

    bool ComponentReader::Read(std::string_view key, std::uint32_t& value) const
    {
        return ReadNumber(*m_Node, m_ComponentName, key, value);
    }

    bool ComponentReader::Read(std::string_view key, float& value) const
    {
        return ReadNumber(*m_Node, m_ComponentName, key, value);
    }

    bool ComponentReader::Read(std::string_view key, std::string& value) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        if (!l_Value->IsScalar())
        {
            return Malformed(m_ComponentName, key);
        }

        value = l_Value->Scalar();

        return true;
    }

    bool ComponentReader::Read(std::string_view key, UUID& value) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        const std::optional<UUID> l_UUID = l_Value->IsScalar() ? UUID::Parse(l_Value->Scalar()) : std::nullopt;
        if (!l_UUID)
        {
            return Malformed(m_ComponentName, key);
        }

        value = *l_UUID;

        return true;
    }

    bool ComponentReader::Read(std::string_view key, glm::vec2& value) const
    {
        return ReadFloats(*m_Node, m_ComponentName, key, std::span<float, 2>(&value.x, 2));
    }

    bool ComponentReader::Read(std::string_view key, glm::vec3& value) const
    {
        return ReadFloats(*m_Node, m_ComponentName, key, std::span<float, 3>(&value.x, 3));
    }

    bool ComponentReader::Read(std::string_view key, glm::vec4& value) const
    {
        return ReadFloats(*m_Node, m_ComponentName, key, std::span<float, 4>(&value.x, 4));
    }

    bool ComponentReader::Read(std::string_view key, glm::quat& value) const
    {
        std::array<float, 4> l_Values{ value.x, value.y, value.z, value.w };
        if (!ReadFloats(*m_Node, m_ComponentName, key, std::span<float, 4>(l_Values)))
        {
            return false;
        }

        value = glm::quat(l_Values[3], l_Values[0], l_Values[1], l_Values[2]);

        return true;
    }

    bool ComponentReader::ReadValue(std::string& value) const
    {
        if (!m_Node->IsScalar())
        {
            TR_CORE_WARN("Scene: {} is not a single value and keeps its default", m_ComponentName);

            return false;
        }

        value = m_Node->Scalar();

        return true;
    }

    namespace SceneSerializer
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "The scene serializer is already initialized.");

            s_State = Memory::New<State>(MemoryTag::Engine);

            RegisterComponent(MakeComponentSerializer<TagComponent, SaveTag, LoadTag>());
            RegisterComponent(MakeComponentSerializer<TransformComponent, SaveTransform, LoadTransform>());
            RegisterComponent(MakeComponentSerializer<CameraComponent, SaveCamera, LoadCamera>());
            RegisterComponent(MakeComponentSerializer<SpriteRendererComponent, SaveSpriteRenderer, LoadSpriteRenderer>());
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            Memory::Delete(s_State);
            s_State = nullptr;
        }

        bool RegisterComponent(const ComponentSerializer& serializer)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            if (serializer.Name.empty() || serializer.Name == c_EntityKey || serializer.Name == c_ParentKey || serializer.Save == nullptr || serializer.Load == nullptr)
            {
                TR_CORE_ERROR("Scene: a component serializer needs a name other than Entity and Parent, and both functions");

                return false;
            }

            if (FindSerializer(serializer.Name) != nullptr)
            {
                TR_CORE_ERROR("Scene: a serializer for {} is already registered", serializer.Name);

                return false;
            }

            s_State->Serializers.push_back(serializer);

            return true;
        }

        void UnregisterComponent(std::string_view name)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            std::erase_if(s_State->Serializers, [name](const ComponentSerializer& serializer) { return serializer.Name == name; });
        }

        // In the order they were registered, the engine's first
        std::vector<std::string_view> GetComponentNames()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            std::vector<std::string_view> l_Names;
            l_Names.reserve(s_State->Serializers.size());
            for (const ComponentSerializer& it_Serializer : s_State->Serializers)
            {
                l_Names.push_back(it_Serializer.Name);
            }

            return l_Names;
        }

        std::string SaveToText(Scene& scene)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            std::string l_Text = std::format("Format: {}\nEntities:", c_FormatVersion);
            if (!scene.GetFirstRoot())
            {
                l_Text += " []";
            }

            for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
            {
                AppendEntity(l_Text, it_Entity, true);
            }

            return l_Text + "\n";
        }

        Expected<void, std::string> LoadFromText(Scene& scene, std::string_view text)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            const Expected<YAML::Node, std::string> l_Root = ParseDocument(text);
            if (!l_Root)
            {
                return Unexpected{ l_Root.GetError() };
            }

            Expected<std::vector<FileEntity>, std::string> l_Entities = ReadEntities(*l_Root);
            if (!l_Entities)
            {
                return Unexpected{ l_Entities.GetError() };
            }

            // Nothing can fail from here, so the scene is only replaced once the file is known to be whole
            scene.Clear();

            std::unordered_map<std::string, std::size_t> l_Unknown;
            CreateEntities(scene, *l_Entities, l_Unknown);
            for (const auto& [it_Name, it_Count] : l_Unknown)
            {
                TR_CORE_INFO("Scene: kept {} {} component(s) as YAML, since no loaded code knows that component", it_Count, it_Name);
            }

            return {};
        }

        // The root has no Parent in the text, so it can be put back anywhere
        std::string SaveEntityToText(Scene& scene, Entity root)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");
            TR_CORE_ASSERT(root.GetScene() == &scene && root.IsValid(), "SaveEntityToText was given an entity of another scene, or one already destroyed");

            std::string l_Text = std::format("Format: {}\nEntities:", c_FormatVersion);
            for (Entity it_Entity = root; it_Entity; it_Entity = scene.GetNextInSubtree(it_Entity, root))
            {
                AppendEntity(l_Text, it_Entity, it_Entity != root);
            }

            return l_Text + "\n";
        }

        // The entities keep the UUIDs in the text, so none may already be in the scene. The root goes before a sibling when one is given, which must be a child of parent, and otherwise last under parent or among the roots
        Expected<Entity, std::string> LoadEntityFromText(Scene& scene, std::string_view text, Entity parent, Entity before)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            const Expected<YAML::Node, std::string> l_Root = ParseDocument(text);
            if (!l_Root)
            {
                return Unexpected{ l_Root.GetError() };
            }

            Expected<std::vector<FileEntity>, std::string> l_Entities = ReadEntities(*l_Root);
            if (!l_Entities)
            {
                return Unexpected{ l_Entities.GetError() };
            }

            const auto a_IsRoot = [](const FileEntity& entity) { return !entity.Parent.IsValid(); };
            if (l_Entities->empty() || !a_IsRoot(l_Entities->front()) || std::any_of(l_Entities->begin() + 1, l_Entities->end(), a_IsRoot))
            {
                return Unexpected{ std::string("does not hold one entity and its subtree, with the entity first") };
            }

            for (const FileEntity& it_Entity : *l_Entities)
            {
                if (scene.FindEntityByUUID(it_Entity.ID))
                {
                    return Unexpected{ std::format("entity {} is already in the scene", it_Entity.ID) };
                }
            }

            std::unordered_map<std::string, std::size_t> l_Unknown;
            CreateEntities(scene, *l_Entities, l_Unknown);

            const Entity l_Entity = scene.FindEntityByUUID(l_Entities->front().ID);
            if (before)
            {
                scene.MoveBefore(l_Entity, before, false);
            }
            else if (parent)
            {
                scene.SetParent(l_Entity, parent, false);
            }

            return l_Entity;
        }

        bool HasComponent(Entity entity, std::string_view name)
        {
            const SceneRegistry::common_type* l_Storage = entity.GetScene()->GetRegistry().storage(GetPoolId(name));

            return l_Storage != nullptr && l_Storage->contains(entity.GetHandle());
        }

        // Default constructed. Only a component with a serializer, which the entity is without
        bool AddComponent(Entity entity, std::string_view name)
        {
            SceneRegistry::common_type* l_Storage = FindStorage(entity, name);
            if (l_Storage == nullptr || l_Storage->contains(entity.GetHandle()))
            {
                return false;
            }

            l_Storage->push(entity.GetHandle());

            return true;
        }

        // Transform stays, since every entity has one
        bool RemoveComponent(Entity entity, std::string_view name)
        {
            SceneRegistry::common_type* l_Storage = name != TransformComponent::c_TypeName ? FindStorage(entity, name) : nullptr;
            if (l_Storage == nullptr || !l_Storage->contains(entity.GetHandle()))
            {
                return false;
            }

            l_Storage->erase(entity.GetHandle());

            return true;
        }

        // The component's fields as a scene file holds them, under a Value key, or nothing when the entity is without it
        std::string SaveComponentToText(Entity entity, std::string_view name)
        {
            const SceneRegistry::common_type* l_Storage = FindStorage(entity, name);
            if (l_Storage == nullptr || !l_Storage->contains(entity.GetHandle()))
            {
                return {};
            }

            std::string l_Text = "Value:";
            ComponentWriter l_Writer(l_Text, 2);
            FindSerializer(name)->Save(l_Storage->value(entity.GetHandle()), l_Writer);

            return l_Text + "\n";
        }

        // Adds the component first when the entity is without it
        Expected<void, std::string> LoadComponentFromText(Entity entity, std::string_view name, std::string_view text)
        {
            SceneRegistry::common_type* l_Storage = FindStorage(entity, name);
            if (l_Storage == nullptr)
            {
                return Unexpected{ std::format("no loaded code knows {}", name) };
            }

            YAML::Node l_Root;
            try
            {
                l_Root = YAML::Load(std::string(text));
            }
            catch (const YAML::Exception& exception)
            {
                return Unexpected{ std::format("not valid YAML: {}", exception.what()) };
            }

            const YAML::Node l_Value = l_Root.IsMap() ? l_Root["Value"] : YAML::Node();
            if (!l_Value)
            {
                return Unexpected{ std::string("has no Value") };
            }

            LoadComponent(entity, *FindSerializer(name), *l_Storage, l_Value);

            return {};
        }

        Expected<void, std::string> Save(Scene& scene, std::string_view path)
        {
            const Expected<void, FileError> l_Written = FileSystem::WriteText(path, SaveToText(scene));
            if (!l_Written)
            {
                return Unexpected{ std::format("{} could not be written: {}", path, ToString(l_Written.GetError())) };
            }

            return {};
        }

        Expected<void, std::string> Load(Scene& scene, std::string_view path)
        {
            const Expected<std::string, FileError> l_Text = FileSystem::ReadText(path);
            if (!l_Text)
            {
                return Unexpected{ std::format("{} could not be read: {}", path, ToString(l_Text.GetError())) };
            }

            Expected<void, std::string> l_Loaded = LoadFromText(scene, *l_Text);
            if (!l_Loaded)
            {
                return Unexpected{ std::format("{} {}", path, l_Loaded.GetError()) };
            }

            return {};
        }
    }
}