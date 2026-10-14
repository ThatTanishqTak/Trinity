#include "Trinity/Scripting/DotNetHost.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#if defined(TR_DOTNET)
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/Scripting/Interop.hpp"

#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>

#include <array>
#include <charconv>
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
            std::int64_t(CORECLR_DELEGATE_CALLTYPE* MeasureInterop)(std::int32_t calls) = nullptr;
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

        // A type the engine shares with C#, as the engine lays it out
        struct SharedType
        {
            std::string_view Name;
            std::array<std::string_view, 4> FieldNames{};
            Interop::TypeLayout Layout{};
        };

        template<typename T, typename... TFields>
        SharedType Describe(std::string_view name, std::array<std::string_view, 4> fieldNames, const T& value, const TFields&... fields)
        {
            static_assert(sizeof...(TFields) <= 4, "A shared type has at most four fields here.");

            SharedType l_Type{ name, fieldNames,{} };
            l_Type.Layout.Size = static_cast<std::int32_t>(sizeof(T));
            l_Type.Layout.FieldCount = static_cast<std::int32_t>(sizeof...(TFields));
            std::size_t l_Index = 0;
            ((l_Type.Layout.Offsets[l_Index++] = static_cast<std::int32_t>(reinterpret_cast<const std::byte*>(&fields) - reinterpret_cast<const std::byte*>(&value))), ...);

            return l_Type;
        }

        // In the order of Host.DescribeLayouts
        std::array<SharedType, 7> DescribeSharedTypes()
        {
            const Interop::Vector2 l_Vector2{};
            const Interop::Vector3 l_Vector3{};
            const Interop::Vector4 l_Vector4{};
            const Interop::Quaternion l_Quaternion{};
            const Interop::Color l_Color{};
            const Interop::Matrix4x4 l_Matrix{};
            const Interop::Utf8 l_Text{};

            return
            {
                Describe("Vector2",{ "X", "Y" }, l_Vector2, l_Vector2.x, l_Vector2.y),
                Describe("Vector3",{ "X", "Y", "Z" }, l_Vector3, l_Vector3.x, l_Vector3.y, l_Vector3.z),
                Describe("Vector4",{ "X", "Y", "Z", "W" }, l_Vector4, l_Vector4.x, l_Vector4.y, l_Vector4.z, l_Vector4.w),
                Describe("Quaternion",{ "X", "Y", "Z", "W" }, l_Quaternion, l_Quaternion.x, l_Quaternion.y, l_Quaternion.z, l_Quaternion.w),
                Describe("Color",{ "R", "G", "B", "A" }, l_Color, l_Color.r, l_Color.g, l_Color.b, l_Color.a),
                Describe("Matrix4x4",{ "Column0", "Column1", "Column2", "Column3" }, l_Matrix, l_Matrix[0], l_Matrix[1], l_Matrix[2], l_Matrix[3]),
                Describe("Utf8",{ "Data", "Length" }, l_Text, l_Text.Data, l_Text.Length)
            };
        }

        // Each shared type as C# lays it out against the engine's layout, so a C# struct that drifts from glm turns C# off with the type and field named, rather than corrupting what crosses
        bool CheckLayouts(std::int32_t(CORECLR_DELEGATE_CALLTYPE* describeLayouts)(Interop::TypeLayout* layouts, std::int32_t capacity, std::int32_t layoutSize), std::string& error)
        {
            const std::array<SharedType, 7> l_Native = DescribeSharedTypes();
            std::array<Interop::TypeLayout, 7> l_Managed{};
            if (describeLayouts(l_Managed.data(), static_cast<std::int32_t>(l_Managed.size()), static_cast<std::int32_t>(sizeof(Interop::TypeLayout))) != static_cast<std::int32_t>(l_Managed.size()))
            {
                error = "Trinity-ScriptCore was built for another engine, and describes other shared types";

                return false;
            }

            for (std::size_t it_Type = 0; it_Type < l_Native.size(); ++it_Type)
            {
                const SharedType& l_Type = l_Native[it_Type];
                const Interop::TypeLayout& l_Layout = l_Managed[it_Type];
                if (l_Layout.Size != l_Type.Layout.Size || l_Layout.FieldCount != l_Type.Layout.FieldCount)
                {
                    error = std::format("{} is {} bytes with {} fields in C#, and {} bytes with {} fields in the engine", l_Type.Name, l_Layout.Size, l_Layout.FieldCount, l_Type.Layout.Size, l_Type.Layout.FieldCount);
                    error = std::format("{} is {} bytes with {} fields in C#, and {} bytes with {} fields in the engine", l_Type.Name, l_Layout.Size, l_Layout.FieldCount, l_Type.Layout.Size, l_Type.Layout.FieldCount);

                    return false;
                }

                for (std::size_t it_Field = 0; it_Field < static_cast<std::size_t>(l_Type.Layout.FieldCount); ++it_Field)
                {
                    if (l_Layout.Offsets[it_Field] != l_Type.Layout.Offsets[it_Field])
                    {
                        error = std::format("{}.{} is at offset {} in C#, and {} in the engine", l_Type.Name, l_Type.FieldNames[it_Field], l_Layout.Offsets[it_Field], l_Type.Layout.Offsets[it_Field]);

                        return false;
                    }
                }
            }

            return true;
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

            std::int32_t(CORECLR_DELEGATE_CALLTYPE * l_HostInitialize)(const Interop::NativeFunctions * functions, std::int32_t size) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE * l_GetDescription)(char* buffer, std::int32_t capacity) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE * l_Handshake)(std::int32_t value) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE * l_DescribeLayouts)(Interop::TypeLayout * layouts, std::int32_t capacity, std::int32_t layoutSize) = nullptr;
            if (!GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("Initialize"), l_HostInitialize, error) || !GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("Shutdown"), s_State.Shutdown, error) || !GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("GetRuntimeDescription"), l_GetDescription, error) || !GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("Handshake"), l_Handshake, error) || !GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("DescribeLayouts"), l_DescribeLayouts, error) || !GetEntryPoint(l_GetFunction, TR_DOTNET_TEXT("MeasureInterop"), s_State.MeasureInterop, error))
            {
                return false;
            }

            // Checked before ScriptCore takes any function, since one that takes a shared type would read it wrong
            if (!CheckLayouts(l_DescribeLayouts, error))
            {
                return false;
            }

            static const Interop::NativeFunctions s_Functions{};
            if (l_HostInitialize(&s_Functions, static_cast<std::int32_t>(sizeof(s_Functions))) != 0)
            {
                error = "Trinity-ScriptCore was built for another engine, and took none of its functions";

                return false;
            }

            // Into C# and back: ScriptCore doubles the engine's echo of the value
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

#if !defined(TR_DISTRIBUTION)
        // What a call from C# into the engine costs, which should stay under 50 ns, with the number of calls to time, a million by default
        void MeasureInteropCommand(std::string_view arguments)
        {
            std::int32_t l_Calls = 1000000;
            if (!arguments.empty())
            {
                const auto [a_End, a_Error] = std::from_chars(arguments.data(), arguments.data() + arguments.size(), l_Calls);
                if (a_Error != std::errc{} || a_End != arguments.data() + arguments.size() || l_Calls <= 0)
                {
                    TR_CORE_WARN("scripting.measure_interop takes a number of calls above 0, not '{}'", arguments);

                    return;
                }
            }

            if (!s_State.Started)
            {
                TR_CORE_WARN("Scripting: C# is off, so there is nothing to measure");

                return;
            }

            static_cast<void>(s_State.MeasureInterop(l_Calls));
        }

        ConsoleCommand s_MeasureInterop("scripting.measure_interop", "Times calls from C# into the engine and logs the cost of each and what they allocated. Takes the number of calls, a million by default", &MeasureInteropCommand);
#endif
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
                s_State.MeasureInterop = nullptr;
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
            s_State.MeasureInterop = nullptr;
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