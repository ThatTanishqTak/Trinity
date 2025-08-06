#pragma once

#include <vector>
#include <cstdint>

namespace Trinity
{
    class BloomPass
    {
    public:
        bool Initialize();
        void Execute();

    private:
        std::vector<uint32_t> m_VertexSpv;
        std::vector<uint32_t> m_FragmentSpv;
    };
}