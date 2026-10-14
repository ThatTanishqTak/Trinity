#pragma once

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scene/ComponentType.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"
#include "Trinity/Scripting/NativeComponents.hpp"
#include "Trinity/Scripting/NativeScripts.hpp"
#include "Trinity/Scripting/Script.hpp"
#include "Trinity/Scripting/ScriptTypes.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// What a game module exports for Forge to call, each optional: TrinityModuleLoad once Forge has loaded the module, which it does after each good build, and TrinityModuleUnload before it unloads it, which it does as a new build takes its place and as the project closes. A module's symbols are hidden but those it marks with TR_GAME_MODULE_EXPORT
#if defined(_WIN32)
#define TR_GAME_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define TR_GAME_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define TR_DETAIL_CONCAT_INNER(First, Second) First##Second
#define TR_DETAIL_CONCAT(First, Second) TR_DETAIL_CONCAT_INNER(First, Second)

// What TR_SCRIPT and TR_COMPONENT declare: a function for the block that follows, a registration linking it into the module's list, and the start of the block's own definition
#define TR_DETAIL_REGISTER(Kind, IsComponent, Class, Describe) \
    static void Describe(::Trinity::GameModule::ClassBuilder<Class>& builder); \
    static ::Trinity::GameModule::Detail::Registration TR_DETAIL_CONCAT(Describe, _Registration){ #Class, IsComponent, &::Trinity::GameModule::Detail::Register##Kind<Class, &Describe>, &::Trinity::GameModule::Detail::Unregister##Kind<Class> }; \
    static void Describe([[maybe_unused]] ::Trinity::GameModule::ClassBuilder<Class>& builder)

// A script class of the module, written once at global scope in a .cpp with its full name, followed by a block that lists its fields with TR_FIELD in the order Properties shows them. Fields are public members, and their defaults are the values a default-made instance has:
// TR_SCRIPT(Game::Player)
// {
//     TR_FIELD(Speed);
//     TR_FIELD(Target);
// }
#define TR_SCRIPT(Class) TR_DETAIL_REGISTER(Script, false, Class, TR_DETAIL_CONCAT(TrinityDescribe, __COUNTER__))

// A component of the module, written as TR_SCRIPT is, for a struct that names itself with static constexpr std::string_view c_TypeName = "Game.Health";. It saves, loads and shows in Properties through its fields, and an entity field is a Trinity::EntityReference
#define TR_COMPONENT(Class) TR_DETAIL_REGISTER(Component, true, Class, TR_DETAIL_CONCAT(TrinityDescribe, __COUNTER__))

// One field of the class, inside its TR_SCRIPT or TR_COMPONENT block, by the member's name
#define TR_FIELD(Member) builder.Field<&std::remove_reference_t<decltype(builder)>::Type::Member>(#Member)

// The module's exports, which register every TR_SCRIPT and TR_COMPONENT of the module as it loads and unregister them before it unloads. Written once, in one .cpp of the module
#define TR_GAME_MODULE() \
    TR_GAME_MODULE_EXPORT void TrinityModuleLoad() { ::Trinity::GameModule::Detail::RegisterAll(); } \
    TR_GAME_MODULE_EXPORT void TrinityModuleUnload() { ::Trinity::GameModule::Detail::UnregisterAll(); } \
    TR_GAME_MODULE_EXPORT std::uint32_t TrinityModuleGetApiVersion() { return ::Trinity::GameModule::c_ApiVersion; } \
    static_assert(true)

namespace Trinity::GameModule
{
    using LoadFunction = void (*)();
    using UnloadFunction = void (*)();
    using ApiVersionFunction = std::uint32_t(*)();

    inline constexpr const char* c_LoadFunction = "TrinityModuleLoad";
    inline constexpr const char* c_UnloadFunction = "TrinityModuleUnload";
    inline constexpr const char* c_ApiVersionFunction = "TrinityModuleGetApiVersion";

    // Raised whenever what TR_GAME_MODULE exports, or how it registers, changes, so Forge refuses a module built against other engine headers rather than calling into it
    inline constexpr std::uint32_t c_ApiVersion = 2;

    // What a TR_SCRIPT or TR_COMPONENT block describes through TR_FIELD: each field's name, its type from the member's, and its default from a default-made T, with how its value reaches an instance, or a component's is read and written
    template<typename T>
    class ClassBuilder
    {
    public:
        using Type = T;

        ClassBuilder() = default;

        ClassBuilder(const ClassBuilder&) = delete;
        ClassBuilder& operator=(const ClassBuilder&) = delete;

        template<auto Member>
        void Field(std::string_view name)
        {
            using FieldType = std::remove_cvref_t<decltype(std::declval<T&>().*Member)>;
            static_assert(ScriptFieldMember<FieldType>, "TR_FIELD: a field is bool, a 32- or 64-bit integer, float, double, a glm vector or quaternion, Trinity::Color, std::string, an enum named by TR_ENUM, Trinity::Entity, Trinity::EntityReference or Trinity::AssetRef");
            if constexpr (ScriptFieldMember<FieldType>)
            {
                ScriptFieldDescription l_Field;
                l_Field.Name = name;
                l_Field.Default = ScriptFieldTraits<FieldType>::ToValue(m_Default.*Member);
                ScriptFieldTraits<FieldType>::Describe(l_Field);
                m_Fields.push_back(std::move(l_Field));

                if constexpr (std::derived_from<T, Script>)
                {
                    m_ScriptFields.push_back({ [](const Script* script) { return ScriptFieldTraits<FieldType>::ToValue(static_cast<const T*>(script)->*Member); }, [](Script* script, const ScriptFieldValue& value, Scene& scene) { ScriptFieldTraits<FieldType>::FromValue(static_cast<T*>(script)->*Member, value, &scene); } });
                }
                else
                {
                    static_assert(!ScriptFieldTraits<FieldType>::c_NeedsScene, "TR_FIELD: a component names an entity by Trinity::EntityReference, since it outlives the handles of the scene it was copied from");
                    m_ComponentFields.push_back({ [](const void* component) { return ScriptFieldTraits<FieldType>::ToValue(static_cast<const T*>(component)->*Member); }, [](void* component, const ScriptFieldValue& value) { ScriptFieldTraits<FieldType>::FromValue(static_cast<T*>(component)->*Member, value, nullptr); } });
                }
            }
        }

        [[nodiscard]] auto& GetFields() { return m_Fields; }
        [[nodiscard]] const auto& GetScriptFields() const { return m_ScriptFields; }
        [[nodiscard]] const auto& GetComponentFields() const { return m_ComponentFields; }

    private:
        T m_Default{};
        std::vector<ScriptFieldDescription, TaggedAllocator<ScriptFieldDescription, MemoryTag::Scripting>> m_Fields;
        std::vector<NativeScripts::FieldAccess, TaggedAllocator<NativeScripts::FieldAccess, MemoryTag::Scripting>> m_ScriptFields;
        std::vector<NativeComponents::FieldAccess, TaggedAllocator<NativeComponents::FieldAccess, MemoryTag::Scripting>> m_ComponentFields;
    };

    namespace Detail
    {
        // One TR_SCRIPT or TR_COMPONENT of this module, linked into the module's own list as its statics are made, in the order a file declares them, so TR_GAME_MODULE registers each as the module loads and unregisters each, in reverse, before it unloads
        struct Registration
        {
            using RegisterFunction = bool (*)(std::string_view name);
            using UnregisterFunction = void (*)(std::string_view name);

            Registration(const char* name, bool component, RegisterFunction registerFunction, UnregisterFunction unregisterFunction);

            Registration(const Registration&) = delete;
            Registration& operator=(const Registration&) = delete;

            const char* Name = nullptr;
            bool Component = false;
            RegisterFunction Register = nullptr;
            UnregisterFunction Unregister = nullptr;
            Registration* Previous = nullptr;
            Registration* Next = nullptr;
            bool Registered = false;
        };

        // Pointers made before any Registration is, since they are constant
        inline Registration*& GetFirst()
        {
            static Registration* s_First = nullptr;

            return s_First;
        }

        inline Registration*& GetLast()
        {
            static Registration* s_Last = nullptr;

            return s_Last;
        }

        inline Registration::Registration(const char* name, bool component, RegisterFunction registerFunction, UnregisterFunction unregisterFunction) : Name(name), Component(component), Register(registerFunction), Unregister(unregisterFunction), Previous(GetLast())
        {
            (Previous != nullptr ? Previous->Next : GetFirst()) = this;
            GetLast() = this;
        }

        // The class as TR_SCRIPT spells it, with spaces dropped and each :: a dot, so Game::Player is Game.Player
        inline std::string MakeClassName(std::string_view spelling)
        {
            std::string l_Name;
            for (std::size_t it_Index = 0; it_Index < spelling.size(); ++it_Index)
            {
                const char l_Character = spelling[it_Index];
                if (l_Character == ':' && it_Index + 1 < spelling.size() && spelling[it_Index + 1] == ':')
                {
                    l_Name += l_Name.empty() ? "" : ".";
                    ++it_Index;
                }
                else if (l_Character != ' ' && l_Character != '\t' && l_Character != '\n')
                {
                    l_Name += l_Character;
                }
            }

            return l_Name;
        }

        template<typename T, void (*Describe)(ClassBuilder<T>&)>
        bool RegisterScript(std::string_view name)
        {
            static_assert(std::derived_from<T, Script>, "TR_SCRIPT: the class derives from Trinity::Script");
            static_assert(std::default_initializable<T>, "TR_SCRIPT: the class can be made with no arguments");

            ClassBuilder<T> l_Builder;
            Describe(l_Builder);

            ScriptClassDescription l_Description;
            l_Description.Name = MakeClassName(name);
            l_Description.Fields = std::move(l_Builder.GetFields());

            return NativeScripts::Register<T>(std::move(l_Description), l_Builder.GetScriptFields());
        }

        template<typename T>
        void UnregisterScript(std::string_view name)
        {
            NativeScripts::Unregister(MakeClassName(name));
        }

        template<Trinity::Component T>
        void SaveComponent(const T& component, ComponentWriter& writer)
        {
            NativeComponents::WriteFields(T::c_TypeName, &component, writer);
        }

        template<Trinity::Component T>
        void LoadComponent(T& component, const ComponentReader& reader)
        {
            NativeComponents::ReadFields(T::c_TypeName, &component, reader);
        }

        // Known by its c_TypeName rather than by its spelling, since its pool is found by that name in the engine and in the module alike
        template<typename T, void (*Describe)(ClassBuilder<T>&)>
        bool RegisterComponent([[maybe_unused]] std::string_view name)
        {
            static_assert(Trinity::Component<T>, "TR_COMPONENT: the struct names itself, as static constexpr std::string_view c_TypeName = \"Game.Health\";");
            static_assert(!std::derived_from<T, Script>, "TR_COMPONENT: a script class is registered with TR_SCRIPT");
            static_assert(std::default_initializable<T> && std::copy_constructible<T>, "TR_COMPONENT: the struct can be made with no arguments, and copied");

            ClassBuilder<T> l_Builder;
            Describe(l_Builder);

            NativeComponentDescription l_Description;
            l_Description.Name = T::c_TypeName;
            l_Description.Fields = std::move(l_Builder.GetFields());

            return NativeComponents::Register(l_Description, l_Builder.GetComponentFields(), MakeComponentSerializer<T, &SaveComponent<T>, &LoadComponent<T>>());
        }

        template<Trinity::Component T>
        void UnregisterComponent([[maybe_unused]] std::string_view name)
        {
            NativeComponents::Unregister(T::c_TypeName);
        }

        inline void RegisterAll()
        {
            std::size_t l_Scripts = 0;
            std::size_t l_Components = 0;
            std::size_t l_Refused = 0;
            for (Registration* it_Registration = GetFirst(); it_Registration != nullptr; it_Registration = it_Registration->Next)
            {
                it_Registration->Registered = it_Registration->Register(it_Registration->Name);
                l_Refused += it_Registration->Registered ? 0 : 1;
                l_Scripts += it_Registration->Registered && !it_Registration->Component ? 1 : 0;
                l_Components += it_Registration->Registered && it_Registration->Component ? 1 : 0;
            }

            TR_CORE_INFO("Scripting: the game module registered {} script class(es) and {} component(s){}", l_Scripts, l_Components, l_Refused == 0 ? std::string() : std::format(", and {} were refused", l_Refused));
        }

        // The components keep their values in every scene as YAML until they are registered again
        inline void UnregisterAll()
        {
            for (Registration* it_Registration = GetLast(); it_Registration != nullptr; it_Registration = it_Registration->Previous)
            {
                if (it_Registration->Registered)
                {
                    it_Registration->Unregister(it_Registration->Name);
                    it_Registration->Registered = false;
                }
            }
        }
    }
}