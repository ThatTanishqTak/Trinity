#pragma once

#include <string>
#include <vector>

#include "Trinity/UI/Layer.h"

class ProjectCreatorLayer : public Trinity::Layer
{
public:
    ProjectCreatorLayer();

    void OnUIRender() override;
    void Open();
    void Close();
    bool IsOpen() const;

private:
    char m_ProjectName[256];
    char m_ProjectDirectory[256];
    std::vector<std::string> m_Templates;
    int m_SelectedTemplate;
    bool m_IsOpen;
    bool m_ShowMessage;
    bool m_IsError;
    std::string m_StatusMessage;
};