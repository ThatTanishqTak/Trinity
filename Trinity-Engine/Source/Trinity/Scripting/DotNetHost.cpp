#include "Trinity/Scripting/DotNetHost.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#if defined(TR_DOTNET)
#include "Trinity/Core/Platform.hpp"

#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>

#include <array>
#include <chrono>
#include <format>
#include <string>
#include <system_error>
#endif

#include <cstddef>

namespace Trinity
{
#if defined(TR_DOTNET)
    namespace
    {
        // hostfxr takes its strings as the platform's characters, which are UTF-16 on Windows
#if defined(_WIN32)
#define TR_DOTNET_TEXT(text) L##text
#else
#define TR_DOTNET_TEXT(text) text
#endif

        constexpr const char_t* c_HostType = TR_DOTNET_TEXT("Trinity.Interop.Host, Trinity-ScriptCore");

        // What the runtime was started with and the entry points of ScriptCore. None of it is allocated, since .NET outlives every memory tag
        struct State
        {
            void* HostFxr = nullptr;
            bool Started = false;
            void (CORECLR_DELEGATE_CALLTYPE* Shutdown)() = nullptr;
            std::array<char, 64> Description{};
            std::size_t DescriptionLength = 0;
        };

        State s_State;

        // What hostfxr writes about why it could not start, kept to go out as one error
        std::string* s_HostErrors = nullptr;

        std::string ToUtf8(const char_t* text)
        {
#if defined(_WIN32)
            const std::u8string l_Text = std::filesystem::path(text).u8string();

            return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
#else
            return std::string(text);
#endif
        }

        // On one line, since hostfxr writes its reasons over several
        void HOSTFXR_CALLTYPE CollectHostError(const char_t* message)
        {
            if (s_HostErrors == nullptr)
            {
                return;
            }

            for (const char it_Character : ToUtf8(message))
            {
                const bool l_Space = it_Character == ' ' || it_Character == '\n' || it_Character == '\r' || it_Character == '\t';
                if (!l_Space)
                {
                    *s_HostErrors += it_Character;
                }
                else if (!s_HostErrors->empty() && s_HostErrors->back() != ' ')
                {
                    *s_HostErrors += ' ';
                }
            }

            if (!s_HostErrors->empty() && s_HostErrors->back() != ' ')
            {
                *s_HostErrors += ' ';
            }
        }

        std::int32_t Echo(std::int32_t value)
        {
            return value + 1;
        }

        template<typename T>
        T GetHostFxrFunction(const char* name)
        {
            return reinterpret_cast<T>(Platform::GetSharedLibrarySymbol(s_State.HostFxr, name));
        }

        // The installed hostfxr, which nethost finds through DOTNET_ROOT, the registered install and the default folders, in that order. It stays loaded once the runtime starts
        bool LoadHostFxr(std::string& error)
        {
            if (s_State.HostFxr != nullptr)
            {
                return true;
            }

            std::array<char_t, 1024> l_Path{};
            std::size_t l_Size = l_Path.size();
            if (get_hostfxr_path(l_Path.data(), &l_Size, nullptr) != 0)
            {
                error = "no .NET runtime is installed. Install the .NET 10 Runtime from https://dotnet.microsoft.com/download/dotnet/10.0";

                return false;
            }

            std::string l_LoadError;
            s_State.HostFxr = Platform::LoadSharedLibrary(std::filesystem::path(l_Path.data()), l_LoadError);
            if (s_State.HostFxr == nullptr)
            {
                error = std::format("hostfxr could not be loaded: {}", l_LoadError);

                return false;
            }

            return true;
        }

        template<typename T>
        bool GetEntryPoint(get_function_pointer_fn getFunction, const char_t* method, T& function, std::string& error)
        {
            void* l_Function = nullptr;
            if (getFunction(c_HostType, method, UNMANAGEDCALLERSONLY_METHOD, nullptr, nullptr, &l_Function) != 0 || l_Function == nullptr)
            {
                error = std::format("Trinity-ScriptCore has no entry point {}", ToUtf8(method));

                return false;
            }

            function = reinterpret_cast<T>(l_Function);

            return true;
        }

        bool StartRuntime(const std::filesystem::path& scriptingDirectory, std::string& error)
        {
            const std::filesystem::path l_Assembly = scriptingDirectory / "Trinity-ScriptCore.dll";
            const std::filesystem::path l_Config = scriptingDirectory / "Trinity-ScriptCore.runtimeconfig.json";
            std::error_code l_Exists;
            if (!std::filesystem::exists(l_Assembly, l_Exists) || !std::filesystem::exists(l_Config, l_Exists))
            {
                error = std::format("Trinity-ScriptCore is missing from {}. Build the Trinity-ScriptCore target", ToUtf8(scriptingDirectory.c_str()));

                return false;
            }

            if (!LoadHostFxr(error))
            {
                return false;
            }

            const auto a_SetErrorWriter = GetHostFxrFunction<hostfxr_set_error_writer_fn>("hostfxr_set_error_writer");
            const auto a_Initialize = GetHostFxrFunction<hostfxr_initialize_for_runtime_config_fn>("hostfxr_initialize_for_runtime_config");
            const auto a_GetDelegate = GetHostFxrFunction<hostfxr_get_runtime_delegate_fn>("hostfxr_get_runtime_delegate");
            const auto a_Close = GetHostFxrFunction<hostfxr_close_fn>("hostfxr_close");
            if (a_SetErrorWriter == nullptr || a_Initialize == nullptr || a_GetDelegate == nullptr || a_Close == nullptr)
            {
                error = "the installed hostfxr lacks the hosting functions of .NET 8 and later";

                return false;
            }

            // A runtime that cannot start says why through the error writer, which goes out as one error
            std::string l_HostErrors;
            s_HostErrors = &l_HostErrors;
            static_cast<void>(a_SetErrorWriter(CollectHostError));
            hostfxr_handle l_Context = nullptr;
            const std::int32_t l_Initialized = a_Initialize(l_Config.c_str(), nullptr, &l_Context);
            static_cast<void>(a_SetErrorWriter(nullptr));
            s_HostErrors = nullptr;
            if (l_Initialized < 0 || l_Context == nullptr)
            {
                while (!l_HostErrors.empty() && l_HostErrors.back() == ' ')
                {
                    l_HostErrors.pop_back();
                }

                error = l_HostErrors.empty() ? std::format(".NET could not start (0x{:08x})", static_cast<std::uint32_t>(l_Initialized)) : std::format(".NET could not start: {}", l_HostErrors);
                if (l_Context != nullptr)
                {
                    static_cast<void>(a_Close(l_Context));
                }

                return false;
            }

            load_assembly_fn l_LoadAssembly = nullptr;
            get_function_pointer_fn l_GetFunction = nullptr;
            const bool l_Delegates = a_GetDelegate(l_Context, hdt_load_assembly, reinterpret_cast<void**>(&l_LoadAssembly)) == 0 && a_GetDelegate(l_Context, hdt_get_function_pointer, reinterpret_cast<void**>(&l_GetFunction)) == 0;
            static_cast<void>(a_Close(l_Context));
            if (!l_Delegates || l_LoadAssembly == nullptr || l_GetFunction == nullptr)
            {
                error = "the runtime gave no way to load an assembly";

                return false;
            }

            // Into the default load context, which the game's assemblies, in their own collectible contexts, then share
            if (l_LoadAssembly(l_Assembly.c_str(), nullptr, nullptr) != 0)
            {
                error = std::format("{} could not be loaded", ToUtf8(l_Assembly.c_str()));

                return false;
            }

            std::int32_t(CORECLR_DELEGATE_CALLTYPE * l_HostInitialize)(const DotNetHost::NativeFunctions * functions, std::int32_t size) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE * l_GetDescription)(char* buffer, std::int32_t capacity) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE * l_Handshake)(std::int32_t value) = nullptr;
            if (!GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("Initialize"), l_HostInitialize, error) || !GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("Shutdown"), s_State.Shutdown, error) || !GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("GetRuntimeDescription"), l_GetDescription, error) || !GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("Handshake"), l_Handshake, error))
            {
                return false;
            }

            static constexpr DotNetHost::NativeFunctions c_Functions{ &Echo };
            if (l_HostInitialize(&c_Functions, static_cast<std::int32_t>(sizeof(c_Functions))) != 0)
            {
                error = "Trinity-ScriptCore was built for another engine, and took none of its functions";

                return false;
            }

            // Into C# and back: ScriptCore doubles the engine's Echo of the value
            constexpr std::int32_t c_Value = 20;
            if (const std::int32_t l_Answer = l_Handshake(c_Value); l_Answer != (c_Value + 1) * 2)
            {
                error = std::format("a call into C# that calls back into the engine returned {}, where {} was expected", l_Answer, (c_Value + 1) * 2);

                return false;
            }

            const std::int32_t l_Length = l_GetDescription(s_State.Description.data(), static_cast<std::int32_t>(s_State.Description.size()));
            s_State.DescriptionLength = l_Length > 0 && static_cast<std::size_t>(l_Length) <= s_State.Description.size() ? static_cast<std::size_t>(l_Length) : 0;

            return true;
        }
    }

    namespace DotNetHost
    {
        bool Start(const std::filesystem::path& scriptingDirectory)
        {
            TR_CORE_ASSERT(!s_State.Started, ".NET is already started.");

            const auto l_Start = std::chrono::steady_clock::now();
            std::string l_Error;
            if (!StartRuntime(scriptingDirectory, l_Error))
            {
                s_State.Shutdown = nullptr;
                TR_CORE_ERROR("Scripting: C# is off, since {}", l_Error);

                return false;
            }

            s_State.Started = true;
            const double l_Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l_Start).count();
            TR_CORE_INFO("Scripting: {} started in {:.1f} ms", GetRuntimeDescription(), l_Milliseconds);

            return true;
        }

        void Stop()
        {
            if (!s_State.Started)
            {
                return;
            }

            s_State.Shutdown();
            s_State.Shutdown = nullptr;
            s_State.Started = false;
            s_State.DescriptionLength = 0;
        }

        bool IsRunning()
        {
            return s_State.Started;
        }

        std::string_view GetRuntimeDescription()
        {
            return std::string_view(s_State.Description.data(), s_State.DescriptionLength);
        }
    }
#else
    namespace DotNetHost
    {
        bool Start([[maybe_unused]] const std::filesystem::path& scriptingDirectory)
        {
            TR_CORE_WARN("Scripting: C# is off, since this build of Trinity was configured without a .NET 10 SDK");

            return false;
        }

        void Stop()
        {

        }

        bool IsRunning()
        {
            return false;
        }

        std::string_view GetRuntimeDescription()
        {
            return {};
        }
    }
#endif
}