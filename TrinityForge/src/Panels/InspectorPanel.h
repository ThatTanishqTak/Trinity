#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/Core/ResourceManager.h"
#include <entt/entt.hpp>
#include <filesystem>
#include <string>

class InspectorPanel : public Trinity::Panel
{
public:
    InspectorPanel(Trinity::Scene* p_Context, entt::entity* p_SelectionContext, Trinity::ResourceManager* p_ResourceManager);

    void OnUIRender() override;

private:
    enum class FileDialogTarget
    {
        None,
        Mesh,
        Texture,
        NormalMap,
        RoughnessMap,
        MetallicMap
    };

    Trinity::Scene* m_Context = nullptr;
    entt::entity* m_SelectionContext = nullptr;
    Trinity::ResourceManager* m_ResourceManager = nullptr;
    std::filesystem::path m_FileDialogCurrentPath;
    std::string m_SelectedFile;
    FileDialogTarget m_DialogTarget = FileDialogTarget::None;
};