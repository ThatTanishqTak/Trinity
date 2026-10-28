#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Trinity
{
    namespace Hash
    {
        // XXH64, as xxHash's specification defines it, so a pack's hashes can be checked by any tool that has it
        [[nodiscard]] TRINITY_API std::uint64_t XXHash64(std::span<const std::byte> data, std::uint64_t seed = 0);
    }
}