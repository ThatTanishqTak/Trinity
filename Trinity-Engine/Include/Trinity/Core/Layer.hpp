#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Events/Event.hpp"
#include "Trinity/Renderer/FrameGraph.hpp"
#include "Trinity/RHI/CommandList.hpp"

#include <string>
#include <utility>

namespace Trinity
{
    class TRINITY_API Layer
    {
    public:
        explicit Layer(std::string name = "Layer") : m_Name(std::move(name))
        {

        }

        virtual ~Layer() = default;

        Layer(const Layer&) = delete;
        Layer& operator=(const Layer&) = delete;

        virtual void OnAttach()
        {

        }

        virtual void OnDetach()
        {

        }

        virtual void OnUpdate([[maybe_unused]] Timestep timestep)
        {

        }

        virtual void OnEvent([[maybe_unused]] Event& event)
        {

        }

        virtual void OnPrepareRender([[maybe_unused]] RHI::CommandList& commands)
        {

        }

        virtual void OnRender([[maybe_unused]] RHI::CommandList& commands)
        {

        }

        virtual void OnRenderUI([[maybe_unused]] RHI::CommandList& commands)
        {

        }

        // Adds passes to the frame's graph after the scene pass, where OnRender draws, and before the output pass, where OnRenderUI draws. They can read the scene colour or draw over it. sceneColor is invalid when the scene target could not be created
        virtual void OnBuildFrameGraph([[maybe_unused]] FrameGraph& graph, [[maybe_unused]] FrameGraphTexture sceneColor)
        {

        }

        virtual void OnImGuiRender()
        {

        }

        [[nodiscard]] virtual bool OnCloseRequested()
        {
            return true;
        }

        [[nodiscard]] const std::string& GetName() const { return m_Name; }

    private:
        std::string m_Name;
    };
}