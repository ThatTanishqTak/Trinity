#include "Trinity/Core/CpuFeatures.hpp"

#include <array>
#include <cstdint>
#include <string_view>

#if defined(_M_X64) || defined(__x86_64__)
#define TR_CPU_X64 1
#if defined(_MSC_VER)
#include <intrin.h>
#include <immintrin.h>
#else
#include <cpuid.h>
#endif
#else
#define TR_CPU_X64 0
#endif

namespace Trinity
{
    namespace CpuFeatures
    {
#if TR_CPU_X64
        namespace
        {
            // EAX, EBX, ECX and EDX for a leaf and subleaf, all zero for a leaf past the highest the processor has
            std::array<std::uint32_t, 4> GetCpuid(std::uint32_t leaf, std::uint32_t subleaf)
            {
                std::array<std::uint32_t, 4> l_Registers{};
#if defined(_MSC_VER)
                std::array<int, 4> l_Values{};
                __cpuid(l_Values.data(), static_cast<int>(leaf & 0x80000000u));
                if (static_cast<std::uint32_t>(l_Values[0]) < leaf)
                {
                    return l_Registers;
                }

                __cpuidex(l_Values.data(), static_cast<int>(leaf), static_cast<int>(subleaf));
                for (std::size_t it_Register = 0; it_Register < l_Registers.size(); ++it_Register)
                {
                    l_Registers[it_Register] = static_cast<std::uint32_t>(l_Values[it_Register]);
                }
#else
                if (__get_cpuid_max(leaf & 0x80000000u, nullptr) < leaf)
                {
                    return l_Registers;
                }

                __cpuid_count(leaf, subleaf, l_Registers[0], l_Registers[1], l_Registers[2], l_Registers[3]);
#endif

                return l_Registers;
            }

#if TR_PHYSICS_AVX2
            // The register state the operating system saves on a thread switch, which must include the YMM registers for AVX code to be safe
            std::uint64_t GetEnabledRegisterState()
            {
#if defined(_MSC_VER)
                return _xgetbv(0);
#else
                std::uint32_t l_Low = 0;
                std::uint32_t l_High = 0;
                __asm__ volatile("xgetbv" : "=a"(l_Low), "=d"(l_High) : "c"(0));

                return (static_cast<std::uint64_t>(l_High) << 32) | l_Low;
#endif
            }
#endif

            bool HasBit(std::uint32_t value, std::uint32_t bit)
            {
                return ((value >> bit) & 1u) != 0;
            }
        }

        // As Jolt's build has them: SSE4.1, SSE4.2 and POPCNT for every build, and AVX, AVX2, FMA, F16C, LZCNT and TZCNT, which is BMI1, for an AVX2 build, with the operating system saving the AVX registers
        std::string GetMissingPhysicsInstructions()
        {
            const std::array<std::uint32_t, 4> l_Features = GetCpuid(1, 0);
            const std::array<std::uint32_t, 4> l_Extended = GetCpuid(7, 0);
            const std::array<std::uint32_t, 4> l_AmdFeatures = GetCpuid(0x80000001u, 0);
            const std::uint32_t l_Ecx = l_Features[2];

            std::string l_Missing;
            const auto a_Require = [&l_Missing](bool present, std::string_view name)
            {
                if (!present)
                {
                    l_Missing.append(l_Missing.empty() ? "" : ", ").append(name);
                }
            };

            a_Require(HasBit(l_Ecx, 19), "SSE4.1");
            a_Require(HasBit(l_Ecx, 20), "SSE4.2");
            a_Require(HasBit(l_Ecx, 23), "POPCNT");

#if TR_PHYSICS_AVX2
            const bool l_OsSavesAvx = HasBit(l_Ecx, 27) && (GetEnabledRegisterState() & 0x6u) == 0x6u;
            a_Require(HasBit(l_Ecx, 28) && l_OsSavesAvx, "AVX");
            a_Require(HasBit(l_Extended[1], 5) && l_OsSavesAvx, "AVX2");
            a_Require(HasBit(l_Ecx, 12), "FMA");
            a_Require(HasBit(l_Ecx, 29), "F16C");
            a_Require(HasBit(l_AmdFeatures[2], 5), "LZCNT");
            a_Require(HasBit(l_Extended[1], 3), "TZCNT");
#else
            static_cast<void>(l_Extended);
            static_cast<void>(l_AmdFeatures);
#endif

            return l_Missing;
        }
#else
        // Jolt's ARM builds use NEON, which every 64-bit ARM processor has
        std::string GetMissingPhysicsInstructions()
        {
            return {};
        }
#endif
    }
}