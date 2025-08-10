#pragma once

#include "Trinity/UI/Panel.h"
#include "ProjectWizardLayer.h"

class LauncherLayer : public Trinity::Panel
{
public:
    LauncherLayer() = default;
    void OnUIRender() override;

private:
    ProjectWizardLayer m_ProjectWizard;
};