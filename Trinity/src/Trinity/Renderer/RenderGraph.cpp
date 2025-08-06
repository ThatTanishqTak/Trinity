#include "Trinity/trpch.h"

#include "Trinity/Renderer/RenderGraph.h"

#include <queue>
#include <limits>
#include <algorithm>

namespace Trinity
{
    void RenderGraph::AddPass(const std::string& name, const PassFunc& func, const std::vector<std::string>& inputs, const std::vector<std::string>& outputs, bool offscreen)
    {
        Pass l_Pass;
        l_Pass.Name = name;
        l_Pass.Func = func;
        l_Pass.Inputs = inputs;
        l_Pass.Outputs = outputs;
        l_Pass.Offscreen = offscreen;
        m_Passes.push_back(std::move(l_Pass));

        for (const auto& it_Input : inputs)
        {
            if (!m_Resources.count(it_Input))
            {
                m_Resources[it_Input] = { ResourceType::Buffer, false };
            }
        }

        for (const auto& it_Output : outputs)
        {
            if (!m_Resources.count(it_Output))
            {
                m_Resources[it_Output] = { ResourceType::Buffer, false };
            }
        }
    }

    void RenderGraph::AddPass(const std::string& name, const PassFunc& func, bool offscreen)
    {
        AddPass(name, func, {}, {}, offscreen);
    }

    void RenderGraph::DeclareTransientTexture(const std::string& name)
    {
        m_Resources[name] = { ResourceType::Texture, true };
    }

    void RenderGraph::DeclareTransientBuffer(const std::string& name)
    {
        m_Resources[name] = { ResourceType::Buffer, true };
    }

    void RenderGraph::BuildDependencyGraph()
    {
        m_DependencyGraph.clear();
        m_DependencyGraph.resize(m_Passes.size());

        std::unordered_map<std::string, size_t> l_LastProducer;
        for (size_t l_Index = 0; l_Index < m_Passes.size(); ++l_Index)
        {
            auto& l_Pass = m_Passes[l_Index];

            for (const auto& it_Input : l_Pass.Inputs)
            {
                auto a_ItProducer = l_LastProducer.find(it_Input);
                if (a_ItProducer != l_LastProducer.end())
                {
                    m_DependencyGraph[a_ItProducer->second].push_back(l_Index);
                }
            }

            for (const auto& it_Output : l_Pass.Outputs)
            {
                l_LastProducer[it_Output] = l_Index;
            }
        }
    }

    std::vector<size_t> RenderGraph::TopologicalSort() const
    {
        std::vector<size_t> l_Indegree(m_Passes.size(), 0);
        for (const auto& it_Adjacency : m_DependencyGraph)
        {
            for (size_t l_Vertex : it_Adjacency)
            {
                ++l_Indegree[l_Vertex];
            }
        }

        std::queue<size_t> l_Queue;
        for (size_t l_I = 0; l_I < l_Indegree.size(); ++l_I)
        {
            if (l_Indegree[l_I] == 0)
            {
                l_Queue.push(l_I);
            }
        }

        std::vector<size_t> l_Order;
        while (!l_Queue.empty())
        {
            size_t l_U = l_Queue.front();
            l_Queue.pop();
            l_Order.push_back(l_U);

            for (size_t l_V : m_DependencyGraph[l_U])
            {
                if (--l_Indegree[l_V] == 0)
                {
                    l_Queue.push(l_V);
                }
            }
        }

        return l_Order;
    }

    void RenderGraph::InsertBarriers(const std::vector<size_t>& order)
    {
        m_Barriers.clear();
        std::unordered_map<std::string, size_t> l_LastWrite;
        for (size_t l_Pos = 0; l_Pos < order.size(); ++l_Pos)
        {
            size_t l_Index = order[l_Pos];
            auto& a_Pass = m_Passes[l_Index];

            for (const auto& it_Input : a_Pass.Inputs)
            {
                auto l_Last = l_LastWrite.find(it_Input);
                if (l_Last != l_LastWrite.end())
                {
                    Barrier l_Barrier;
                    l_Barrier.Resource = it_Input;
                    l_Barrier.SrcPass = m_Passes[l_Last->second].Name;
                    l_Barrier.DstPass = a_Pass.Name;
                    m_Barriers.push_back(l_Barrier);
                }
            }

            for (const auto& it_Output : a_Pass.Outputs)
            {
                l_LastWrite[it_Output] = l_Index;
            }
        }
    }

    void RenderGraph::Execute()
    {
        BuildDependencyGraph();
        std::vector<size_t> l_Order = TopologicalSort();
        InsertBarriers(l_Order);

        struct Lifetime
        {
            size_t First = std::numeric_limits<size_t>::max();
            size_t Last = 0;
        };

        std::unordered_map<std::string, Lifetime> l_Lifetimes;

        for (size_t l_Pos = 0; l_Pos < l_Order.size(); ++l_Pos)
        {
            size_t l_Index = l_Order[l_Pos];
            const Pass& l_Pass = m_Passes[l_Index];

            auto a_Record = [&](const std::string& l_Name)
                {
                    auto& a_Life = l_Lifetimes[l_Name];
                    a_Life.First = std::min(a_Life.First, l_Pos);
                    a_Life.Last = std::max(a_Life.Last, l_Pos);
                };

            for (const auto& it_Input : l_Pass.Inputs)
            {
                a_Record(it_Input);
            }

            for (const auto& it_Output : l_Pass.Outputs)
            {
                a_Record(it_Output);
            }
        }

        for (size_t l_Pos = 0; l_Pos < l_Order.size(); ++l_Pos)
        {
            size_t l_Index = l_Order[l_Pos];
            Pass& l_Pass = m_Passes[l_Index];

            for (const auto& it_ResourcePair : m_Resources)
            {
                const auto& a_Name = it_ResourcePair.first;
                const auto& a_Info = it_ResourcePair.second;
                auto a_LifeIt = l_Lifetimes.find(a_Name);
                if (a_Info.Transient && a_LifeIt != l_Lifetimes.end() && a_LifeIt->second.First == l_Pos)
                {
                    // allocate transient resource
                }
            }

            for (const auto& it_Barrier : m_Barriers)
            {
                if (it_Barrier.DstPass == l_Pass.Name)
                {
                    // synchronize
                }
            }

            if (l_Pass.Func)
            {
                l_Pass.Func();
            }

            for (const auto& it_ResourcePair : m_Resources)
            {
                const auto& a_Name = it_ResourcePair.first;
                const auto& a_Info = it_ResourcePair.second;
                auto a_LifeIt = l_Lifetimes.find(a_Name);
                if (a_Info.Transient && a_LifeIt != l_Lifetimes.end() && a_LifeIt->second.Last == l_Pos)
                {
                    // release transient resource
                }
            }
        }
    }

    void RenderGraph::Clear()
    {
        m_Passes.clear();
        m_Barriers.clear();
        m_Resources.clear();
        m_DependencyGraph.clear();
    }
}