#pragma once

#include "Trinity/UI/Panel.h"
#include "Layers/ProjectCreatorLayer.h"
#include "Layers/ProjectLoaderLayer.h"

class LauncherLayer : public Trinity::Panel
{
public:
    LauncherLayer() = default;
    void OnUIRender() override;

private:
    ProjectCreatorLayer m_ProjectCreator;
    ProjectLoaderLayer m_ProjectLoader;
};