#include "Trinity/trpch.h"

#include "Trinity/Renderer/RenderGraph.h"

namespace Trinity
{
    void RenderGraph::AddPass(const std::string& name, const PassFunc& func, bool offscreen)
    {
        Pass l_Pass{ name, func, offscreen };
        if (offscreen)
        {
            m_OffscreenPasses.push_back(l_Pass);
        }

        else
        {
            m_OnscreenPasses.push_back(l_Pass);
        }
    }

    void RenderGraph::Execute()
    {
        for (auto& it_Pass : m_OffscreenPasses)
        {
            if (it_Pass.Func)
            {
                it_Pass.Func();
            }
        }

        for (auto& it_Pass : m_OnscreenPasses)
        {
            if (it_Pass.Func)
            {
                it_Pass.Func();
            }
        }
    }

    void RenderGraph::Clear()
    {
        m_OffscreenPasses.clear();
        m_OnscreenPasses.clear();
    }
}