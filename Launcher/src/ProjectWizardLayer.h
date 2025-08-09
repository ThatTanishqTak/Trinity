#pragma once

#include <string>
#include <vector>

namespace Trinity
{
    class Layer
    {
    public:
        virtual ~Layer() = default;
        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float) {}
        virtual void OnUIRender() {}
    };
}

class ProjectWizardLayer : public Trinity::Layer
{
public:
    ProjectWizardLayer();

    void OnUIRender() override;

private:
    char m_ProjectName[256];
    char m_ProjectDirectory[256];
    std::vector<std::string> m_Templates;
    int m_SelectedTemplate;
};