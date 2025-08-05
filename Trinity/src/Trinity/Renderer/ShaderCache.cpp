#include "Trinity/trpch.h"

#include "Trinity/Renderer/ShaderCache.h"
#include "Trinity/Utilities/Utilities.h"

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Trinity
{
    ShaderCache& ShaderCache::Get()
    {
        static ShaderCache s_Instance;
        return s_Instance;
    }

    ShaderCache::ShaderCache()
    {
        Load();
    }

    ShaderCache::~ShaderCache()
    {
        Save();
    }

    const CachedStage* ShaderCache::GetStage(const std::string& path) const
    {
        auto it = m_Cache.find(path);
        if (it != m_Cache.end())
        {
            return &it->second;
        }

        return nullptr;
    }

    void ShaderCache::StoreStage(const std::string& path, size_t hash, const std::filesystem::file_time_type& timestamp, const std::vector<uint32_t>& spirv)
    {
        m_Cache[path] = { hash, timestamp, spirv };
    }

    void ShaderCache::Load()
    {
        std::filesystem::path l_Path = "Assets/Shaders/cache.json";
        if (!std::filesystem::exists(l_Path))
        {
            return;
        }

        std::ifstream l_File(l_Path);
        if (!l_File)
        {
            TR_CORE_WARN("Failed to open shader cache file: {}", l_Path.string());

            return;
        }

        nlohmann::json l_Json;
        l_File >> l_Json;

        for (auto it = l_Json.begin(); it != l_Json.end(); ++it)
        {
            CachedStage l_Stage;
            l_Stage.Hash = it.value().at("hash").get<size_t>();
            auto a_Time = it.value().at("timestamp").get<long long>();

            l_Stage.Timestamp = std::filesystem::file_time_type(std::chrono::duration_cast<std::filesystem::file_time_type::duration>(std::chrono::nanoseconds(a_Time)));
            l_Stage.Spirv = it.value().at("spirv").get<std::vector<uint32_t>>();
            m_Cache[it.key()] = std::move(l_Stage);
        }
    }

    void ShaderCache::Save() const
    {
        std::filesystem::path l_Path = "Assets/Shaders/cache.json";
        std::filesystem::create_directories(l_Path.parent_path());

        nlohmann::json l_Json;
        for (const auto& [path, stage] : m_Cache)
        {
            l_Json[path] =
            {
                { "hash", stage.Hash },
                { "timestamp", stage.Timestamp.time_since_epoch().count() },
                { "spirv", stage.Spirv }
            };
        }

        std::ofstream l_File(l_Path);
        if (!l_File)
        {
            TR_CORE_WARN("Failed to save shader cache file: {}", l_Path.string());

            return;
        }

        l_File << l_Json.dump(4);
    }
}