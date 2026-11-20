#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scripting/Script.hpp"
#include "Trinity/Scripting/ScriptClassRegistry.hpp"

#include <concepts>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Trinity
{
    // The code that registers a class makes and frees its instances. Main thread only
    namespace NativeScripts
    {
        using CreateFunction = Script * (*)();
        using DestroyFunction = void (*)(Script* script);
        // Before its OnCreate. The scene is the one the instance runs in, where an entity field is found
        using SetFieldFunction = void (*)(Script* script, const ScriptFieldValue& value, Scene& scene);
        // As the instance has it now, which a reload while the scene runs carries over to the new instance
        using GetFieldFunction = ScriptFieldValue(*)(const Script* script);

        struct FieldAccess
        {
            GetFieldFunction Get = nullptr;
            SetFieldFunction Set = nullptr;
        };

        // Refused as the registry refuses it, without both functions, or without both for every field
        TRINITY_API bool Register(ScriptClassDescription description, CreateFunction create, DestroyFunction destroy, std::span<const FieldAccess> fields = {}, const void* type = nullptr);
        TRINITY_API void Unregister(std::string_view name);
        // The name a class was registered under by its type, or empty
        [[nodiscard]] TRINITY_API std::string_view FindClassName(const void* type);

        // The callbacks T overrides, found from the type of each one through T
        template<std::derived_from<Script> T>
        [[nodiscard]] constexpr std::uint32_t GetCallbacks()
        {
            std::uint32_t l_Callbacks = 0;
            if constexpr (!std::is_same_v<decltype(&T::OnCreate), decltype(&Script::OnCreate)>)
            { 
                l_Callbacks |= ScriptCallbacks::Create;
            }
            
            if constexpr (!std::is_same_v<decltype(&T::OnFixedUpdate), decltype(&Script::OnFixedUpdate)>)
            {
                l_Callbacks |= ScriptCallbacks::FixedUpdate;
            }

            if constexpr (!std::is_same_v<decltype(&T::OnUpdate), decltype(&Script::OnUpdate)>)
            {
                l_Callbacks |= ScriptCallbacks::Update;
            }

            if constexpr (!std::is_same_v<decltype(&T::OnLateUpdate), decltype(&Script::OnLateUpdate)>)
            {
                l_Callbacks |= ScriptCallbacks::LateUpdate;
            }

            if constexpr (!std::is_same_v<decltype(&T::OnDestroy), decltype(&Script::OnDestroy)>)
            {
                l_Callbacks |= ScriptCallbacks::Destroy;
            }

            if constexpr (!std::is_same_v<decltype(&T::OnReloaded), decltype(&Script::OnReloaded)>)
            {
                l_Callbacks |= ScriptCallbacks::Reloaded;
            }

            if constexpr (!std::is_same_v<decltype(&T::OnContactBegin), decltype(&Script::OnContactBegin)> || !std::is_same_v<decltype(&T::OnContactStay), decltype(&Script::OnContactStay)> || !std::is_same_v<decltype(&T::OnContactEnd), decltype(&Script::OnContactEnd)> || !std::is_same_v<decltype(&T::OnTriggerEnter), decltype(&Script::OnTriggerEnter)> || !std::is_same_v<decltype(&T::OnTriggerExit), decltype(&Script::OnTriggerExit)> || !std::is_same_v<decltype(&T::OnJointBreak), decltype(&Script::OnJointBreak)> || !std::is_same_v<decltype(&T::OnCharacterHit), decltype(&Script::OnCharacterHit)>)
            {
                l_Callbacks |= ScriptCallbacks::Physics;
            }

            return l_Callbacks;
        }

        // Made and freed here, in the code that registers it, under the Game tag
        template<std::derived_from<Script> T>
            requires std::default_initializable<T>
        bool Register(ScriptClassDescription description, std::span<const FieldAccess> fields = {})
        {
            description.Callbacks = GetCallbacks<T>();

            return Register(std::move(description), []() -> Script* { return Memory::New<T>(MemoryTag::Game); }, [](Script* script) { Memory::Delete(static_cast<T*>(script)); }, fields, &Detail::s_ScriptType<T>);
        }
    }
}