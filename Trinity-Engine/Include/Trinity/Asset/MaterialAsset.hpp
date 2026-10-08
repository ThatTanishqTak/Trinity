#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Asset/MaterialData.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <string_view>

namespace Trinity
{
    class MaterialLoader;

    // A material file, loaded into a record of the renderer's material table, which shaders read through the table's bindless index
    class TRINITY_API MaterialAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "Material";

        MaterialAsset() = default;
        ~MaterialAsset() override;

        MaterialAsset(const MaterialAsset&) = delete;
        MaterialAsset& operator=(const MaterialAsset&) = delete;

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        [[nodiscard]] const MaterialData& GetData() const { return m_Data; }
        // The default material holds record 0, which a material still loading reads as
        [[nodiscard]] std::uint32_t GetTableIndex() const { return m_TableIndex; }

    private:
        friend class MaterialLoader;

        const MaterialLoader* m_Loader = nullptr;
        MaterialData m_Data;
        std::uint32_t m_TableIndex = 0;
    };
}