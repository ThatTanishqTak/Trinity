#include "Trinity/Scripting/DotNetHost.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#if defined(TR_DOTNET)
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/Scripting/ManagedScripts.hpp"

#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>

#include <algorithm>
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
        constexpr const char_t* c_ScriptHostType = TR_DOTNET_TEXT("Trinity.Interop.ScriptHost, Trinity-ScriptCore");

        // ScriptCore's entry points the engine keeps, all set once the runtime has started
        struct EntryPoints
        {
            void (CORECLR_DELEGATE_CALLTYPE* Shutdown)() = nullptr;
            std::int64_t(CORECLR_DELEGATE_CALLTYPE* MeasureInterop)(std::int32_t calls) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE* IsDebuggerAttached)() = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE* LoadAssembly)(const char* path, std::int32_t length, const char* buildPath, std::int32_t buildLength) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE* UnloadAssembly)() = nullptr;
            void (CORECLR_DELEGATE_CALLTYPE* GetHeapStatistics)(std::int64_t* bytes, std::int32_t* collections) = nullptr;
            std::intptr_t(CORECLR_DELEGATE_CALLTYPE* CreateInstance)(std::int32_t classIndex, std::uint64_t entity) = nullptr;
            void (CORECLR_DELEGATE_CALLTYPE* DestroyInstance)(std::intptr_t instance) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE* SetField)(std::intptr_t instance, std::int32_t field, const Interop::FieldValue* value) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE* GetField)(std::intptr_t instance, std::int32_t field, Interop::FieldValue* value) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE* Invoke)(std::intptr_t instance, std::int32_t callback, float timestep) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE* InvokePhysics)(std::intptr_t instance, std::int32_t type, const Interop::Collision* collision) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE* ResumeTask)(std::intptr_t task, std::int32_t* waitType, float* seconds, std::int32_t* frames) = nullptr;
            std::int32_t(CORECLR_DELEGATE_CALLTYPE* CheckTask)(std::intptr_t task, std::int32_t* met) = nullptr;
            void (CORECLR_DELEGATE_CALLTYPE* FreeTask)(std::intptr_t task) = nullptr;
            void (CORECLR_DELEGATE_CALLTYPE* RaiseInputEvents)() = nullptr;
        };

        // What the runtime was started with and the entry points of ScriptCore. None of it is allocated, since .NET outlives every memory tag
        struct State
        {
            void* HostFxr = nullptr;
            bool Started = false;
            EntryPoints Entry;
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
            std::array<std::string_view, 6> FieldNames{};
            Interop::TypeLayout Layout{};
        };

        template<typename T, typename... TFields>
        SharedType Describe(std::string_view name, std::array<std::string_view, 6> fieldNames, const T& value, const TFields&... fields)
        {
            static_assert(sizeof...(TFields) <= 6, "A shared type has at most six fields here.");

            SharedType l_Type{ name, fieldNames,{} };
            l_Type.Layout.Size = static_cast<std::int32_t>(sizeof(T));
            l_Type.Layout.FieldCount = static_cast<std::int32_t>(sizeof...(TFields));
            std::size_t l_Index = 0;
            ((l_Type.Layout.Offsets[l_Index++] = static_cast<std::int32_t>(reinterpret_cast<const std::byte*>(&fields) - reinterpret_cast<const std::byte*>(&value))), ...);

            return l_Type;
        }

        // In the order of Host.DescribeLayouts
        std::array<SharedType, 14> DescribeSharedTypes()
        {
            const Interop::Vector2 l_Vector2{};
            const Interop::Vector3 l_Vector3{};
            const Interop::Vector4 l_Vector4{};
            const Interop::Quaternion l_Quaternion{};
            const Interop::Color l_Color{};
            const Interop::Matrix4x4 l_Matrix{};
            const Interop::Utf8 l_Text{};
            const Interop::FieldValue l_Value{};
            const Interop::Collision l_Collision{};
            const Interop::QueryFilter l_Filter{};
            const Interop::QueryShape l_Shape{};
            const Interop::QueryShape2D l_Shape2D{};
            const Interop::PhysicsHit l_Hit{};
            const Interop::PhysicsHit2D l_Hit2D{};

            return
            {
                Describe("Vector2",{ "X", "Y" }, l_Vector2, l_Vector2.x, l_Vector2.y),
                Describe("Vector3",{ "X", "Y", "Z" }, l_Vector3, l_Vector3.x, l_Vector3.y, l_Vector3.z),
                Describe("Vector4",{ "X", "Y", "Z", "W" }, l_Vector4, l_Vector4.x, l_Vector4.y, l_Vector4.z, l_Vector4.w),
                Describe("Quaternion",{ "X", "Y", "Z", "W" }, l_Quaternion, l_Quaternion.x, l_Quaternion.y, l_Quaternion.z, l_Quaternion.w),
                Describe("Color",{ "R", "G", "B", "A" }, l_Color, l_Color.r, l_Color.g, l_Color.b, l_Color.a),
                Describe("Matrix4x4",{ "Column0", "Column1", "Column2", "Column3" }, l_Matrix, l_Matrix[0], l_Matrix[1], l_Matrix[2], l_Matrix[3]),
                Describe("Utf8",{ "Data", "Length" }, l_Text, l_Text.Data, l_Text.Length),
                Describe("FieldValue",{ "Type", "Reserved", "Integer", "Real", "Floats", "Text" }, l_Value, l_Value.Type, l_Value.Reserved, l_Value.Integer, l_Value.Real, l_Value.Floats, l_Value.Text),
                Describe("Collision",{ "Other", "Point", "Normal", "Is2D" }, l_Collision, l_Collision.Other, l_Collision.Point, l_Collision.Normal, l_Collision.Is2D),
                Describe("QueryFilter",{ "Layers", "HitTriggers", "Ignore" }, l_Filter, l_Filter.Layers, l_Filter.HitTriggers, l_Filter.Ignore),
                Describe("QueryShape",{ "Type", "Radius", "HalfExtents", "Height" }, l_Shape, l_Shape.Type, l_Shape.Radius, l_Shape.HalfExtents, l_Shape.Height),
                Describe("QueryShape2D",{ "Type", "Radius", "HalfExtents", "Height" }, l_Shape2D, l_Shape2D.Type, l_Shape2D.Radius, l_Shape2D.HalfExtents, l_Shape2D.Height),
                Describe("PhysicsHit",{ "Entity", "Body", "Point", "Normal", "Distance", "Trigger" }, l_Hit, l_Hit.Entity, l_Hit.Body, l_Hit.Point, l_Hit.Normal, l_Hit.Distance, l_Hit.Trigger),
                Describe("PhysicsHit2D",{ "Entity", "Body", "Point", "Normal", "Distance", "Trigger" }, l_Hit2D, l_Hit2D.Entity, l_Hit2D.Body, l_Hit2D.Point, l_Hit2D.Normal, l_Hit2D.Distance, l_Hit2D.Trigger)
            };
        }

        // Each shared type as C# lays it out against the engine's layout, so a C# struct that drifts from glm turns C# off with the type and field named, rather than corrupting what crosses
        bool CheckLayouts(std::int32_t(CORECLR_DELEGATE_CALLTYPE* describeLayouts)(Interop::TypeLayout* layouts, std::int32_t capacity, std::int32_t layoutSize), std::string& error)
        {
            const std::array<SharedType, 14> l_Native = DescribeSharedTypes();
            std::array<Interop::TypeLayout, 14> l_Managed{};
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
        bool GetEntryPoint(get_function_pointer_fn getFunction, const char_t* type, const char_t* method, T& function, std::string& error)
        {
            void* l_Function = nullptr;
            if (getFunction(type, method, UNMANAGEDCALLERSONLY_METHOD, nullptr, nullptr, &l_Function) != 0 || l_Function == nullptr)
            {
                const std::string l_Type = ToUtf8(type);
                error = std::format("Trinity-ScriptCore has no entry point {}.{}", std::string_view(l_Type).substr(0, l_Type.find(',')), ToUtf8(method));

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
            EntryPoints& l_Entry = s_State.Entry;
            const bool l_Found = GetEntryPoint(l_GetFunction, c_HostType, TR_DOTNET_TEXT("Initialize"), l_HostInitialize, error) && GetEntryPoint(l_GetFunction, c_HostType, TR_DOTNET_TEXT("Shutdown"), l_Entry.Shutdown, error) && GetEntryPoint(l_GetFunction, c_HostType, TR_DOTNET_TEXT("GetRuntimeDescription"), l_GetDescription, error)
                && GetEntryPoint(l_GetFunction, c_HostType, TR_DOTNET_TEXT("Handshake"), l_Handshake, error) && GetEntryPoint(l_GetFunction, c_HostType, TR_DOTNET_TEXT("DescribeLayouts"), l_DescribeLayouts, error) && GetEntryPoint(l_GetFunction, c_HostType, TR_DOTNET_TEXT("MeasureInterop"), l_Entry.MeasureInterop, error) && GetEntryPoint(l_GetFunction, c_HostType, TR_DOTNET_TEXT("IsDebuggerAttached"), l_Entry.IsDebuggerAttached, error)
                && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("LoadAssembly"), l_Entry.LoadAssembly, error) && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("UnloadAssembly"), l_Entry.UnloadAssembly, error) && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("GetHeapStatistics"), l_Entry.GetHeapStatistics, error)
                && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("CreateInstance"), l_Entry.CreateInstance, error) && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("DestroyInstance"), l_Entry.DestroyInstance, error)
                && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("SetField"), l_Entry.SetField, error) && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("GetField"), l_Entry.GetField, error)
                && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("Invoke"), l_Entry.Invoke, error) && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("InvokePhysics"), l_Entry.InvokePhysics, error)
                && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("ResumeTask"), l_Entry.ResumeTask, error) && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("CheckTask"), l_Entry.CheckTask, error) && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("FreeTask"), l_Entry.FreeTask, error)
                && GetEntryPoint(l_GetFunction, c_ScriptHostType, TR_DOTNET_TEXT("RaiseInputEvents"), l_Entry.RaiseInputEvents, error);
            if (!l_Found)
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

            static_cast<void>(s_State.Entry.MeasureInterop(l_Calls));
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
                s_State.Entry = {};
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

            static_cast<void>(ManagedScripts::UnloadAssembly());
            s_State.Entry.Shutdown();
            s_State.Entry = {};
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

        std::int32_t LoadAssembly(const std::filesystem::path& path, const std::filesystem::path& buildPath)
        {
            if (!s_State.Started)
            {
                return -1;
            }


            const std::u8string l_Path = path.u8string();
            const std::u8string l_BuildPath = buildPath.u8string();

            return s_State.Entry.LoadAssembly(reinterpret_cast<const char*>(l_Path.data()), static_cast<std::int32_t>(l_Path.size()), reinterpret_cast<const char*>(l_BuildPath.data()), static_cast<std::int32_t>(l_BuildPath.size()));
        }

        bool UnloadAssembly()
        {
            return !s_State.Started || s_State.Entry.UnloadAssembly() != 0;
        }

        bool IsDebuggerAttached()
        {
            return s_State.Started && s_State.Entry.IsDebuggerAttached() != 0;
        }

        HeapStatistics GetHeapStatistics()
        {
            HeapStatistics l_Statistics;
            if (s_State.Started)
            {
                std::int64_t l_Bytes = 0;
                std::int32_t l_Collections = 0;
                s_State.Entry.GetHeapStatistics(&l_Bytes, &l_Collections);
                l_Statistics.Bytes = static_cast<std::uint64_t>(std::max<std::int64_t>(l_Bytes, 0));
                l_Statistics.Collections = static_cast<std::uint32_t>(std::max<std::int32_t>(l_Collections, 0));
            }

            return l_Statistics;
        }


        std::intptr_t CreateInstance(std::int32_t classIndex, UUID entity)
        {
            return s_State.Started ? s_State.Entry.CreateInstance(classIndex, entity.GetValue()) : 0;
        }

        void DestroyInstance(std::intptr_t instance)
        {
            if (s_State.Started && instance != 0)
            {
                s_State.Entry.DestroyInstance(instance);
            }
        }

        bool SetField(std::intptr_t instance, std::int32_t field, const Interop::FieldValue& value)
        {
            return s_State.Started && s_State.Entry.SetField(instance, field, &value) == 0;
        }

        bool GetField(std::intptr_t instance, std::int32_t field, Interop::FieldValue& value)
        {
            return s_State.Started && s_State.Entry.GetField(instance, field, &value) == 0;
        }

        bool Invoke(std::intptr_t instance, ManagedCallback callback, float timestep)
        {
            return s_State.Started && s_State.Entry.Invoke(instance, static_cast<std::int32_t>(callback), timestep) == 0;
        }

        bool InvokePhysics(std::intptr_t instance, PhysicsEventType type, const Interop::Collision& collision)
        {
            return s_State.Started && s_State.Entry.InvokePhysics(instance, static_cast<std::int32_t>(type), &collision) == 0;
        }

        bool ResumeTask(std::intptr_t task, std::int32_t& waitType, float& seconds, std::int32_t& frames)
        {
            return s_State.Started && s_State.Entry.ResumeTask(task, &waitType, &seconds, &frames) == 0;
        }

        bool CheckTask(std::intptr_t task, bool& met)
        {
            std::int32_t l_Met = 0;
            const bool l_Asked = s_State.Started && s_State.Entry.CheckTask(task, &l_Met) == 0;
            met = l_Met != 0;

            return l_Asked;
        }

        void FreeTask(std::intptr_t task)
        {
            if (s_State.Started && task != 0)
            {
                s_State.Entry.FreeTask(task);
            }
        }

        void RaiseInputEvents()
        {
            if (s_State.Started)
            {
                s_State.Entry.RaiseInputEvents();
            }
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

        std::int32_t LoadAssembly([[maybe_unused]] const std::filesystem::path& path, [[maybe_unused]] const std::filesystem::path& buildPath)
        {
            return -1;
        }

        bool UnloadAssembly()
        {
            return true;
        }

        bool IsDebuggerAttached()
        {
            return false;
        }

        HeapStatistics GetHeapStatistics()
        {
            return {};
        }

        std::intptr_t CreateInstance([[maybe_unused]] std::int32_t classIndex, [[maybe_unused]] UUID entity)
        {
            return 0;
        }

        void DestroyInstance([[maybe_unused]] std::intptr_t instance)
        {

        }

        bool SetField([[maybe_unused]] std::intptr_t instance, [[maybe_unused]] std::int32_t field, [[maybe_unused]] const Interop::FieldValue& value)
        {
            return false;
        }

        bool GetField([[maybe_unused]] std::intptr_t instance, [[maybe_unused]] std::int32_t field, [[maybe_unused]] Interop::FieldValue& value)
        {
            return false;
        }

        bool Invoke([[maybe_unused]] std::intptr_t instance, [[maybe_unused]] ManagedCallback callback, [[maybe_unused]] float timestep)
        {
            return false;
        }

        bool InvokePhysics([[maybe_unused]] std::intptr_t instance, [[maybe_unused]] PhysicsEventType type, [[maybe_unused]] const Interop::Collision& collision)
        {
            return false;
        }

        bool ResumeTask([[maybe_unused]] std::intptr_t task, [[maybe_unused]] std::int32_t& waitType, [[maybe_unused]] float& seconds, [[maybe_unused]] std::int32_t& frames)
        {
            return false;
        }

        bool CheckTask([[maybe_unused]] std::intptr_t task, bool& met)
        {
            met = false;

            return false;
        }

        void FreeTask([[maybe_unused]] std::intptr_t task)
        {

        }

        void RaiseInputEvents()
        {

        }
    }
#endif
}