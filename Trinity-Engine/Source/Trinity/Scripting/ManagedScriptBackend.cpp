#include "Trinity/Scripting/ManagedScripts.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scripting/DotNetHost.hpp"
#include "Trinity/Scripting/Interop.hpp"
#include "Trinity/Scripting/ScriptBackend.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        // Trinity.Interop.FieldType numbers the types as ScriptFieldType does, which these hold to
        static_assert(static_cast<int>(ScriptFieldType::Bool) == 0 && static_cast<int>(ScriptFieldType::Float) == 5 && static_cast<int>(ScriptFieldType::Vector2) == 7 && static_cast<int>(ScriptFieldType::String) == 12 && static_cast<int>(ScriptFieldType::Asset) == 15, "Trinity.Interop.FieldType must number the field types as ScriptFieldType does");

        Interop::FieldValue ToFieldValue(const ScriptFieldValue& value)
        {
            Interop::FieldValue l_Value{};
            l_Value.Type = static_cast<Interop::Int32>(value.GetType());
            switch (value.GetType())
            {
                case ScriptFieldType::Bool: l_Value.Integer = value.Get<bool>() ? 1 : 0; break;
                case ScriptFieldType::Int32: l_Value.Integer = value.Get<std::int32_t>(); break;
                case ScriptFieldType::Int64: l_Value.Integer = value.Get<std::int64_t>(); break;
                case ScriptFieldType::UInt32: l_Value.Integer = value.Get<std::uint32_t>(); break;
                case ScriptFieldType::UInt64: l_Value.Integer = static_cast<std::int64_t>(value.Get<std::uint64_t>()); break;
                case ScriptFieldType::Float: l_Value.Real = value.Get<float>(); break;
                case ScriptFieldType::Double: l_Value.Real = value.Get<double>(); break;
                case ScriptFieldType::Vector2:
                {
                    const glm::vec2& l_Vector = value.Get<glm::vec2>();
                    l_Value.Floats = { l_Vector.x, l_Vector.y, 0.0f, 0.0f };
                    break;
                }
                case ScriptFieldType::Vector3:
                {
                    const glm::vec3& l_Vector = value.Get<glm::vec3>();
                    l_Value.Floats = { l_Vector.x, l_Vector.y, l_Vector.z, 0.0f };
                    break;
                }
                case ScriptFieldType::Vector4:
                case ScriptFieldType::Color:
                {
                    const glm::vec4& l_Vector = value.Get<glm::vec4>();
                    l_Value.Floats = { l_Vector.x, l_Vector.y, l_Vector.z, l_Vector.w };
                    break;
                }
                case ScriptFieldType::Quaternion:
                {
                    const glm::quat& l_Rotation = value.Get<glm::quat>();
                    l_Value.Floats = { l_Rotation.x, l_Rotation.y, l_Rotation.z, l_Rotation.w };
                    break;
                }
                case ScriptFieldType::String: l_Value.Text = Interop::ToUtf8(std::string_view(value.Get<ScriptString>())); break;
                case ScriptFieldType::Enum: l_Value.Integer = value.Get<std::int64_t>(); break;
                case ScriptFieldType::Entity:
                case ScriptFieldType::Asset: l_Value.Integer = static_cast<std::int64_t>(value.Get<UUID>().GetValue()); break;
            }

            return l_Value;
        }

        // Nothing for a type that is not one
        std::optional<ScriptFieldValue> FromFieldValue(const Interop::FieldValue& value)
        {
            if (value.Type < 0 || value.Type > static_cast<Interop::Int32>(ScriptFieldType::Asset))
            {
                return std::nullopt;
            }

            const std::array<float, 4>& l_Floats = value.Floats;
            switch (static_cast<ScriptFieldType>(value.Type))
            {
                case ScriptFieldType::Bool: return ScriptFieldValue::Bool(value.Integer != 0);
                case ScriptFieldType::Int32: return ScriptFieldValue::Int32(static_cast<std::int32_t>(value.Integer));
                case ScriptFieldType::Int64: return ScriptFieldValue::Int64(value.Integer);
                case ScriptFieldType::UInt32: return ScriptFieldValue::UInt32(static_cast<std::uint32_t>(value.Integer));
                case ScriptFieldType::UInt64: return ScriptFieldValue::UInt64(static_cast<std::uint64_t>(value.Integer));
                case ScriptFieldType::Float: return ScriptFieldValue::Float(static_cast<float>(value.Real));
                case ScriptFieldType::Double: return ScriptFieldValue::Double(value.Real);
                case ScriptFieldType::Vector2: return ScriptFieldValue::Vector2({ l_Floats[0], l_Floats[1] });
                case ScriptFieldType::Vector3: return ScriptFieldValue::Vector3({ l_Floats[0], l_Floats[1], l_Floats[2] });
                case ScriptFieldType::Vector4: return ScriptFieldValue::Vector4({ l_Floats[0], l_Floats[1], l_Floats[2], l_Floats[3] });
                case ScriptFieldType::Quaternion: return ScriptFieldValue::Quaternion(glm::quat(l_Floats[3], l_Floats[0], l_Floats[1], l_Floats[2]));
                case ScriptFieldType::Color: return ScriptFieldValue::Color({ l_Floats[0], l_Floats[1], l_Floats[2], l_Floats[3] });
                case ScriptFieldType::String: return ScriptFieldValue::String(Interop::ToStringView(value.Text));
                case ScriptFieldType::Enum: return ScriptFieldValue::Enum(value.Integer);
                case ScriptFieldType::Entity: return ScriptFieldValue::Entity(UUID(static_cast<std::uint64_t>(value.Integer)));
                case ScriptFieldType::Asset: return ScriptFieldValue::Asset(UUID(static_cast<std::uint64_t>(value.Integer)));
            }

            return std::nullopt;
        }

        // An instance is its GC handle, which is never 0 for a live one
        std::intptr_t ToHandle(const void* instance)
        {
            return reinterpret_cast<std::intptr_t>(instance);
        }

        // C# classes, each by the index ScriptCore described it at. ScriptCore catches every exception a script throws and logs it with its stack trace, so a call that threw is only false here
        class ManagedBackend final : public ScriptBackend
        {
        public:
            [[nodiscard]] ScriptLanguage GetLanguage() const override { return ScriptLanguage::CSharp; }

            [[nodiscard]] void* CreateInstance(const ScriptClassDescription& description, Entity entity, [[maybe_unused]] ScriptRuntime& runtime) override
            {
                const std::optional<std::int32_t> l_Index = FindIndex(description.Name);
                if (!l_Index)
                {
                    TR_CORE_ERROR("Scripting: C# class {} is registered, and its assembly has no such class", std::string_view(description.Name));

                    return nullptr;
                }

                return reinterpret_cast<void*>(DotNetHost::CreateInstance(*l_Index, entity.GetUUID()));
            }

            void DestroyInstance(void* instance) override
            {
                DotNetHost::DestroyInstance(ToHandle(instance));
            }

            // ScriptCore described the fields in the order the description has them
            bool ApplyFields(void* instance, const ScriptClassDescription& description, const ScriptEntry& entry, [[maybe_unused]] Scene& scene) override
            {
                for (std::size_t it_Index = 0; it_Index < description.Fields.size(); ++it_Index)
                {
                    const Interop::FieldValue l_Value = ToFieldValue(ScriptClassRegistry::GetFieldValue(entry, description.Fields[it_Index]));
                    if (!DotNetHost::SetField(ToHandle(instance), static_cast<std::int32_t>(it_Index), l_Value))
                    {
                        return false;
                    }
                }

                return true;
            }

            void ReadFields(const void* instance, const ScriptClassDescription& description, ScriptEntry& entry) override
            {
                for (std::size_t it_Index = 0; it_Index < description.Fields.size(); ++it_Index)
                {
                    Interop::FieldValue l_Value{};
                    if (!DotNetHost::GetField(ToHandle(instance), static_cast<std::int32_t>(it_Index), l_Value))
                    {
                        continue;
                    }

                    std::optional<ScriptFieldValue> l_Read = FromFieldValue(l_Value);
                    if (l_Read && l_Read->GetType() == description.Fields[it_Index].GetType())
                    {
                        entry.SetField(description.Fields[it_Index].Name, std::move(*l_Read));
                    }
                }
            }

            bool OnCreate(void* instance) override { return DotNetHost::Invoke(ToHandle(instance), DotNetHost::ManagedCallback::Create, 0.0f); }
            bool OnFixedUpdate(void* instance, Timestep step) override { return DotNetHost::Invoke(ToHandle(instance), DotNetHost::ManagedCallback::FixedUpdate, step.GetSeconds()); }
            bool OnUpdate(void* instance, Timestep timestep) override { return DotNetHost::Invoke(ToHandle(instance), DotNetHost::ManagedCallback::Update, timestep.GetSeconds()); }
            bool OnLateUpdate(void* instance, Timestep timestep) override { return DotNetHost::Invoke(ToHandle(instance), DotNetHost::ManagedCallback::LateUpdate, timestep.GetSeconds()); }
            bool OnDestroy(void* instance) override { return DotNetHost::Invoke(ToHandle(instance), DotNetHost::ManagedCallback::Destroy, 0.0f); }
            bool OnReloaded(void* instance) override { return DotNetHost::Invoke(ToHandle(instance), DotNetHost::ManagedCallback::Reloaded, 0.0f); }

            bool OnPhysicsEvent(void* instance, PhysicsEventType type, const ScriptCollision& collision) override
            {
                const Interop::Collision l_Collision{ collision.Other.GetValue(), collision.Point, collision.Normal, collision.Is2D };

                return DotNetHost::InvokePhysics(ToHandle(instance), type, l_Collision);
            }

            // ScriptCore numbers the classes it describes from 0 as each assembly loads
            void BeginLoad()
            {
                m_Classes = {};
                m_NextIndex = 0;
                m_Loading = true;
            }

            void EndLoad()
            {
                m_Loading = false;
                m_Building.reset();
            }

            void BeginClass(std::string_view name, std::uint32_t callbacks)
            {
                m_Building.reset();
                const std::int32_t l_Index = m_NextIndex++;
                if (!m_Loading)
                {
                    return;
                }

                Building l_Building;
                l_Building.Index = l_Index;
                l_Building.Description.Name = name;
                l_Building.Description.Language = ScriptLanguage::CSharp;
                l_Building.Description.Callbacks = callbacks & ScriptCallbacks::All;
                m_Building = std::move(l_Building);
            }

            void AddField(std::string_view name, const Interop::FieldValue* value, std::string_view assetType)
            {
                if (!m_Building)
                {
                    return;
                }

                std::optional<ScriptFieldValue> l_Default = value != nullptr ? FromFieldValue(*value) : std::nullopt;
                if (!l_Default)
                {
                    TR_CORE_ERROR("Scripting: field {} of C# class {} has no type a field can have", name, std::string_view(m_Building->Description.Name));
                    m_Building->Valid = false;

                    return;
                }

                ScriptFieldDescription l_Field;
                l_Field.Name = name;
                l_Field.Default = std::move(*l_Default);
                l_Field.AssetType = assetType;
                m_Building->Description.Fields.push_back(std::move(l_Field));
            }

            void AddEnumValue(std::string_view name, std::int64_t value)
            {
                if (m_Building && !m_Building->Description.Fields.empty())
                {
                    m_Building->Description.Fields.back().EnumValues.push_back({ TaggedString<MemoryTag::Scripting>(name), value });
                }
            }

            bool EndClass()
            {
                std::optional<Building> l_Building = std::exchange(m_Building, std::nullopt);
                if (!l_Building || !l_Building->Valid || !ScriptClassRegistry::Register(l_Building->Description))
                {
                    return false;
                }

                m_Classes.push_back({ std::move(l_Building->Description.Name), l_Building->Index });

                return true;
            }

            void Clear()
            {
                m_Classes = {};
            }

            [[nodiscard]] std::size_t GetClassCount() const { return m_Classes.size(); }

            [[nodiscard]] bool IsLoaded() const { return m_Loaded; }
            void SetLoaded(bool loaded) { m_Loaded = loaded; }

        private:
            struct ManagedClass
            {
                TaggedString<MemoryTag::Scripting> Name;
                std::int32_t Index = 0;
            };

            struct Building
            {
                ScriptClassDescription Description;
                std::int32_t Index = 0;
                bool Valid = true;
            };

            [[nodiscard]] std::optional<std::int32_t> FindIndex(std::string_view name) const
            {
                const auto a_Found = std::ranges::find_if(m_Classes, [name](const ManagedClass& managed) { return std::string_view(managed.Name) == name; });

                return a_Found != m_Classes.end() ? std::optional(a_Found->Index) : std::nullopt;
            }

            std::vector<ManagedClass, TaggedAllocator<ManagedClass, MemoryTag::Scripting>> m_Classes;
            std::optional<Building> m_Building;
            std::int32_t m_NextIndex = 0;
            bool m_Loading = false;
            bool m_Loaded = false;
        };

        ManagedBackend* s_Managed = nullptr;
    }

    namespace ManagedScriptBackend
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_Managed == nullptr, "The C# script backend is already initialized.");

            s_Managed = Memory::New<ManagedBackend>(MemoryTag::Scripting);
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_Managed != nullptr, "The C# script backend is not initialized.");

            if (s_Managed->IsLoaded())
            {
                TR_CORE_WARN("Scripting: a C# assembly was still loaded at shutdown");
            }

            Memory::Delete(s_Managed);
            s_Managed = nullptr;
        }

        ScriptBackend* Get()
        {
            return s_Managed;
        }
    }

    // Called only by ScriptCore, from within LoadAssembly
    namespace Interop
    {
        Void Scripting_BeginClass(Utf8 name, UInt32 callbacks)
        {
            if (s_Managed != nullptr)
            {
                s_Managed->BeginClass(ToStringView(name), callbacks);
            }
        }

        Void Scripting_AddField(Utf8 name, const FieldValue* value, Utf8 assetType)
        {
            if (s_Managed != nullptr)
            {
                s_Managed->AddField(ToStringView(name), value, ToStringView(assetType));
            }
        }

        Void Scripting_AddEnumValue(Utf8 name, Int64 value)
        {
            if (s_Managed != nullptr)
            {
                s_Managed->AddEnumValue(ToStringView(name), value);
            }
        }

        Bool Scripting_EndClass()
        {
            return s_Managed != nullptr && s_Managed->EndClass();
        }
    }

    namespace ManagedScripts
    {
        bool IsAvailable()
        {
            return DotNetHost::IsRunning();
        }

        bool LoadAssembly(const std::filesystem::path& path, const std::filesystem::path& buildPath)
        {
            TR_CORE_ASSERT(s_Managed != nullptr, "The C# script backend is not initialized.");

            const std::u8string l_Name = path.filename().u8string();
            const std::string_view l_File(reinterpret_cast<const char*>(l_Name.data()), l_Name.size());
            if (!DotNetHost::IsRunning())
            {
                TR_CORE_ERROR("Scripting: {} is not loaded, since C# is off", l_File);

                return false;
            }

            if (s_Managed->IsLoaded())
            {
                TR_CORE_ERROR("Scripting: {} is not loaded, since another C# assembly is", l_File);

                return false;
            }

            s_Managed->BeginLoad();
            const std::int32_t l_Described = DotNetHost::LoadAssembly(path, buildPath);
            s_Managed->EndLoad();
            if (l_Described < 0)
            {
                ScriptClassRegistry::UnregisterAll(ScriptLanguage::CSharp);
                s_Managed->Clear();

                return false;
            }

            s_Managed->SetLoaded(true);
            TR_CORE_INFO("Scripting: loaded {}, with {} C# script class(es){}", l_File, s_Managed->GetClassCount(), static_cast<std::size_t>(l_Described) != s_Managed->GetClassCount() ? std::format(" of the {} it has", l_Described) : std::string());

            return true;
        }

        bool UnloadAssembly()
        {
            if (s_Managed == nullptr || !s_Managed->IsLoaded())
            {
                return true;
            }

            ScriptClassRegistry::UnregisterAll(ScriptLanguage::CSharp);
            s_Managed->Clear();
            s_Managed->SetLoaded(false);

            return DotNetHost::UnloadAssembly();
        }

        bool IsAssemblyLoaded()
        {
            return s_Managed != nullptr && s_Managed->IsLoaded();
        }

        HeapStatistics GetHeapStatistics()
        {
            return DotNetHost::GetHeapStatistics();
        }
    }
}