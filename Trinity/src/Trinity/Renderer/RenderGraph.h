#pragma once

#include <functional>
#include <vector>
#include <string>

namespace Trinity
{
    class RenderGraph
    {
    public:
        using PassFunc = std::function<void()>;

        struct Pass
        {
            std::string Name;
            PassFunc Func;
            bool Offscreen = false;
        };

        void AddPass(const std::string& name, const PassFunc& func, bool offscreen = false);
        void Execute();
        void Clear();

    private:
        std::vector<Pass> m_OffscreenPasses;
        std::vector<Pass> m_OnscreenPasses;
    };
}