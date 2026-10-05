#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    enum class AssetState : std::uint8_t
    {
        None,
        Loading,
        Ready,
        Failed
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(AssetState state);

    class TRINITY_API Asset
    {
    public:
        virtual ~Asset() = default;

        [[nodiscard]] virtual std::string_view GetAssetType() const = 0;
    };

    class TRINITY_API AssetLoader
    {
    public:
        virtual ~AssetLoader() = default;

        [[nodiscard]] virtual std::string_view GetAssetType() const = 0;
        [[nodiscard]] virtual Expected<Asset*, std::string> Load(std::span<const std::byte> data) const = 0;
        [[nodiscard]] virtual const Asset* GetPlaceholder() const
        {
            return nullptr;
        }
    };

    class TRINITY_API BinaryAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "Binary";

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        std::vector<std::byte, TaggedAllocator<std::byte, MemoryTag::Assets>> Data;
    };
}