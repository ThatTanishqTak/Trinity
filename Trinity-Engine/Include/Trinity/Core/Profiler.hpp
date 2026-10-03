#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Trinity
{
    struct ProfilerSpecification
    {
        bool LogSummary = false;
        std::filesystem::path CapturePath;
        std::uint64_t CaptureFrames = 300;
    };

    namespace Profiler
    {
        TRINITY_API void Initialize(const ProfilerSpecification& specification);
        TRINITY_API void Shutdown();

        TRINITY_API void BeginFrame();
        TRINITY_API void SetThreadName(std::string name);

        [[nodiscard]] TRINITY_API bool IsActive();
    }

    class TRINITY_API ProfileScope
    {
    public:
        explicit ProfileScope(const char* name);
        ~ProfileScope();

        ProfileScope(const ProfileScope&) = delete;
        ProfileScope& operator=(const ProfileScope&) = delete;

    private:
        const char* m_Name = nullptr;
        ProfileScope* m_Parent = nullptr;
        std::uint64_t m_StartNanoseconds = 0;
        std::uint64_t m_ChildNanoseconds = 0;
    };
}

#if defined(TR_ENABLE_PROFILING)
#define TR_INTERNAL_PROFILE_CONCAT_IMPL(a, b) a##b
#define TR_INTERNAL_PROFILE_CONCAT(a, b) TR_INTERNAL_PROFILE_CONCAT_IMPL(a, b)

#define TR_PROFILE_FRAME() ::Trinity::Profiler::BeginFrame()
#define TR_PROFILE_SCOPE(name) const ::Trinity::ProfileScope TR_INTERNAL_PROFILE_CONCAT(l_ProfileScope, __LINE__)(name)
#define TR_PROFILE_FUNCTION() TR_PROFILE_SCOPE(__FUNCTION__)
#else
#define TR_PROFILE_FRAME() ((void)0)
#define TR_PROFILE_SCOPE(name) ((void)0)
#define TR_PROFILE_FUNCTION() ((void)0)
#endif