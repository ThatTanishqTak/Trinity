#pragma once

#include <filesystem>
#include <unordered_map>
#include <vector>
#include <string>

namespace Trinity
{
    struct CachedStage
    {
        size_t Hash = 0;
        std::filesystem::file_time_type Timestamp{};
        std::vector<uint32_t> Spirv;
    };

    class ShaderCache
    {
    public:
        static ShaderCache& Get();

        const CachedStage* GetStage(const std::string& path) const;
        void StoreStage(const std::string& path, size_t hash, const std::filesystem::file_time_type& timestamp, const std::vector<uint32_t>& spirv);

    private:
        ShaderCache();
        ~ShaderCache();

        void Load();
        void Save() const;

        std::unordered_map<std::string, CachedStage> m_Cache;
    };
}