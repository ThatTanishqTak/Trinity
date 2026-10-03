#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Events/Event.hpp"

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

        [[nodiscard]] const std::string& GetName() const { return m_Name; }

    private:
        std::string m_Name;
    };
}