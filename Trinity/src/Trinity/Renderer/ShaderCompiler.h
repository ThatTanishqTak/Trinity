#pragma once

#include <string>
#include <vector>

namespace Trinity
{
    class ShaderCompiler
    {
    public:
        static std::vector<uint32_t> CompileToSpv(const std::string& a_SourcePath);
    };
}