#include "Trinity/trpch.h"

#include "Trinity/Renderer/RenderGraph.h"

namespace Trinity
{
    void RenderGraph::AddPass(const std::string& name, const PassFunc& func, bool offscreen)
    {
        Pass pass{ name, func, offscreen };
        if (offscreen)
        {
            m_OffscreenPasses.push_back(pass);
        }
        else
        {
            m_OnscreenPasses.push_back(pass);
        }
    }

    void RenderGraph::Execute()
    {
        for (auto& pass : m_OffscreenPasses)
        {
            if (pass.Func)
            {
                pass.Func();
            }
        }

        for (auto& pass : m_OnscreenPasses)
        {
            if (pass.Func)
            {
                pass.Func();
            }
        }
    }

    void RenderGraph::Clear()
    {
        m_OffscreenPasses.clear();
        m_OnscreenPasses.clear();
    }
}