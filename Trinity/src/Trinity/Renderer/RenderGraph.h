#pragma once

#include <functional>
#include <vector>
#include <string>
#include <unordered_map>

namespace Trinity
{
    class RenderGraph
    {
    public:
        using PassFunc = std::function<void()>;

        enum class ResourceType
        {
            Texture,
            Buffer
        };

        struct Resource
        {
            ResourceType Type;
            bool Transient = false;
        };

        struct Barrier
        {
            std::string Resource;
            std::string SrcPass;
            std::string DstPass;
        };

        struct Pass
        {
            std::string Name;
            PassFunc Func;
            std::vector<std::string> Inputs;
            std::vector<std::string> Outputs;
            bool Offscreen = false;
        };

        void AddPass(const std::string& name, const PassFunc& func,
            const std::vector<std::string>& inputs = {},
            const std::vector<std::string>& outputs = {},
            bool offscreen = false);

        void AddPass(const std::string& name, const PassFunc& func, bool offscreen);

        void DeclareTransientTexture(const std::string& name);
        void DeclareTransientBuffer(const std::string& name);

        void Execute();
        void Clear();

    private:
        void BuildDependencyGraph();
        std::vector<size_t> TopologicalSort() const;
        void InsertBarriers(const std::vector<size_t>& order);

        std::vector<Pass> m_Passes;
        std::vector<Barrier> m_Barriers;
        std::unordered_map<std::string, Resource> m_Resources;
        std::vector<std::vector<size_t>> m_DependencyGraph;
    };
}