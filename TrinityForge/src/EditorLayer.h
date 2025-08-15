#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/Core/AssetManager.h"
#include "Trinity/UI/ImGuiLayer.h"

#include <filesystem>
#include <memory>
#include <vector>

#include <entt/entt.hpp>

class EditorLayer : public Trinity::Panel
{
public:
    EditorLayer(Trinity::Scene* scene, Trinity::AssetManager* assetManager, Trinity::ImGuiLayer* imguiLayer);

    void OnUIRender() override;
    void RegisterPanel(std::unique_ptr<Trinity::Panel> panel);

    entt::entity* GetSelectionContext() { return &m_SelectedEntity; }

private:
    Trinity::Scene* m_Scene = nullptr;
    Trinity::AssetManager* m_AssetManager = nullptr;
    Trinity::ImGuiLayer* m_ImGuiLayer = nullptr;
    std::filesystem::path m_LayoutDirectory;
    bool m_ShowSaveLayoutPopup = false;
    bool m_ShowLoadLayoutPopup = false;
    char m_LayoutName[256]{};
    std::vector<std::unique_ptr<Trinity::Panel>> m_Panels;
    entt::entity m_SelectedEntity{ entt::null };
};