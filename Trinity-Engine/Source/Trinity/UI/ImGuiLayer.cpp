#include "Trinity/UI/ImGuiLayer.hpp"

#include "Trinity/Core/Application.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Core/Window.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"

#include <imgui.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace
    {
        constexpr std::string_view c_SettingsPath = "/saves/imgui.ini";

        void* AllocateUI(std::size_t size, [[maybe_unused]] void* userData)
        {
            return Memory::TryAllocate(size, MemoryTag::UI);
        }

        void FreeUI(void* memory, [[maybe_unused]] void* userData)
        {
            Memory::Free(memory);
        }
    }

    ImGuiLayer::ImGuiLayer() : Layer("ImGui")
    {

    }

    // ImGui writes no files itself: its settings go through /saves, and are saved when ImGui asks and at shutdown
    void ImGuiLayer::OnAttach()
    {
        ImGui::SetAllocatorFunctions(&AllocateUI, &FreeUI, nullptr);
        IMGUI_CHECKVERSION();
        m_Context = ImGui::CreateContext();

        ImGuiIO& l_IO = ImGui::GetIO();
        l_IO.IniFilename = nullptr;
        l_IO.LogFilename = nullptr;
        l_IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

        const Expected<std::string, FileError> l_Settings = FileSystem::ReadText(c_SettingsPath);
        if (l_Settings)
        {
            ImGui::LoadIniSettingsFromMemory(l_Settings->data(), l_Settings->size());
        }

        TR_CORE_INFO("ImGui: {} context created, with settings {} {}", IMGUI_VERSION, l_Settings ? "loaded from" : "to be saved in", c_SettingsPath);
    }

    void ImGuiLayer::OnDetach()
    {
        SaveSettings();
        AcknowledgeTextures(true);

        ImGui::DestroyContext(m_Context);
        m_Context = nullptr;

        const MemoryTagStats l_Stats = Memory::GetStats(MemoryTag::UI);
        TR_CORE_INFO("ImGui: context destroyed after {} frame(s), and UI holds {} in {} live allocation(s)", m_FrameCount, Memory::FormatBytes(l_Stats.CurrentBytes), l_Stats.LiveAllocations);
    }

    // Overlays update after every layer, so the UI is built from what this frame's updates left behind
    void ImGuiLayer::OnUpdate(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        Application& l_Application = Application::Get();
        const Window& l_Window = l_Application.GetWindow();

        ImGuiIO& l_IO = ImGui::GetIO();
        l_IO.DisplaySize = ImVec2(static_cast<float>(l_Window.GetWidth()), static_cast<float>(l_Window.GetHeight()));
        l_IO.DeltaTime = timestep.GetSeconds() > 0.0f ? timestep.GetSeconds() : 1.0f / 60.0f;

        ImGui::NewFrame();
        for (const Scope<Layer>& it_Layer : l_Application.GetLayerStack())
        {
            it_Layer->OnImGuiRender();
        }

        ImGui::Render();
        AcknowledgeTextures(false);

        if (l_IO.WantSaveIniSettings)
        {
            SaveSettings();
        }

        ++m_FrameCount;
    }

    void ImGuiLayer::SaveSettings()
    {
        std::size_t l_Size = 0;
        const char* l_Settings = ImGui::SaveIniSettingsToMemory(&l_Size);

        const Expected<void, FileError> l_Result = FileSystem::WriteText(c_SettingsPath, std::string_view(l_Settings, l_Size));
        if (!l_Result)
        {
            TR_CORE_WARN("ImGui: could not save {}: {}", c_SettingsPath, ToString(l_Result.GetError()));
        }

        ImGui::GetIO().WantSaveIniSettings = false;
    }

    // Without a renderer backend, texture requests are accepted without anything being created, so the font atlas can still grow and be freed
    void ImGuiLayer::AcknowledgeTextures(bool shutdown)
    {
        for (ImTextureData* it_Texture : ImGui::GetPlatformIO().Textures)
        {
            if (shutdown)
            {
                if (it_Texture->RefCount == 1)
                {
                    it_Texture->SetTexID(ImTextureID_Invalid);
                    it_Texture->SetStatus(ImTextureStatus_Destroyed);
                }

                continue;
            }

            switch (it_Texture->Status)
            {
                case ImTextureStatus_WantCreate:
                {
                    it_Texture->SetTexID(static_cast<ImTextureID>(it_Texture->UniqueID) + 1);
                    it_Texture->SetStatus(ImTextureStatus_OK);

                    break;
                }
                case ImTextureStatus_WantUpdates:
                {
                    it_Texture->SetStatus(ImTextureStatus_OK);

                    break;
                }
                case ImTextureStatus_WantDestroy:
                {
                    it_Texture->SetTexID(ImTextureID_Invalid);
                    it_Texture->SetStatus(ImTextureStatus_Destroyed);

                    break;
                }
                default:
                {
                    break;
                }
            }
        }
    }
}