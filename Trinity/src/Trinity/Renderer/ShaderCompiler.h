#pragma once

#include <filesystem>
#include <vector>

namespace Trinity
{
    class ShaderCompiler
    {
    public:
        static std::vector<uint32_t> CompileToSpv(const std::string& path);
        static void CompileAllShaders(const std::filesystem::path& directory);
    };
}