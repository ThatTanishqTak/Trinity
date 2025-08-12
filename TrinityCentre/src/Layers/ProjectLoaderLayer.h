#pragma once

#include <string>

#include "Trinity/UI/Layer.h"

class ProjectLoaderLayer : public Trinity::Layer
{
public:
    ProjectLoaderLayer();

    void OnUIRender() override;
    void Open();
    void Close();
    bool IsOpen() const;

private:
    char m_ProjectDirectory[256];
    bool m_IsOpen;
    bool m_ShowMessage;
    bool m_IsError;
    std::string m_StatusMessage;
};