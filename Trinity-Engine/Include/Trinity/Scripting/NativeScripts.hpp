#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scripting/Script.hpp"
#include "Trinity/Scripting/ScriptClassRegistry.hpp"

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Trinity
{
    // C++ script classes, which the code that has them registers with how to make and free their instances, so a module's instances are made and freed by the module itself. Main thread only
    namespace NativeScripts
    {
        using CreateFunction = Script * (*)();
        using DestroyFunction = void (*)(Script* script);

        // Registers the class with the script class registry as a C++ class, its language set to C++. Refused as the registry refuses it, or without both functions
        TRINITY_API bool Register(ScriptClassDescription description, CreateFunction create, DestroyFunction destroy);
        TRINITY_API void Unregister(std::string_view name);

        // The callbacks T overrides, found from the type of each one through T, which names Script while T and its bases leave it alone
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

            if constexpr (!std::is_same_v<decltype(&T::OnContactBegin), decltype(&Script::OnContactBegin)> || !std::is_same_v<decltype(&T::OnContactStay), decltype(&Script::OnContactStay)> || !std::is_same_v<decltype(&T::OnContactEnd), decltype(&Script::OnContactEnd)> || !std::is_same_v<decltype(&T::OnTriggerEnter), decltype(&Script::OnTriggerEnter)> || !std::is_same_v<decltype(&T::OnTriggerExit), decltype(&Script::OnTriggerExit)> || !std::is_same_v<decltype(&T::OnJointBreak), decltype(&Script::OnJointBreak)>)
            {
                l_Callbacks |= ScriptCallbacks::Physics;
            }

            return l_Callbacks;
        }

        // T's instances are made and freed here, in the code that registers it, under the Game tag, and only the callbacks it overrides are called
        template<std::derived_from<Script> T>
            requires std::default_initializable<T>
        bool Register(ScriptClassDescription description)
        {
            description.Callbacks = GetCallbacks<T>();

            return Register(std::move(description), []() -> Script* { return Memory::New<T>(MemoryTag::Game); }, [](Script* script) { Memory::Delete(static_cast<T*>(script)); });
        }
    }
}