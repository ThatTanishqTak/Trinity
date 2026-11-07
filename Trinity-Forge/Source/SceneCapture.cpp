#include "SceneCapture.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <vector>

namespace
{
    // The first primary camera in hierarchy order, as the Game panel shows the scene through
    Trinity::Entity FindPrimaryCamera(Trinity::Scene& scene)
    {
        const Trinity::SceneRegistry& l_Registry = scene.GetRegistry();
        for (Trinity::Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            if (const Trinity::CameraComponent* l_Camera = l_Registry.try_get<Trinity::CameraComponent>(it_Entity.GetHandle()); l_Camera != nullptr && l_Camera->Primary)
            {
                return it_Entity;
            }
        }

        return {};
    }

    bool IsSettled(Trinity::UUID id)
    {
        const Trinity::AssetState l_State = id ? Trinity::AssetManager::GetState(id) : Trinity::AssetState::Ready;

        return l_State == Trinity::AssetState::Ready || l_State == Trinity::AssetState::Failed;
    }
}

SceneCapture::~SceneCapture()
{
    if (m_View)
    {
        Trinity::Application::Get().GetRenderer().DestroyView(*m_View);
    }
}

void SceneCapture::Start(std::filesystem::path file)
{
    m_File = std::move(file);
    m_View = Trinity::Application::Get().GetRenderer().CreateView(c_Width, c_Height);
    m_Deadline = std::chrono::steady_clock::now() + c_Timeout;
    TR_INFO("Capture: the scene is captured to {} once it has imported and loaded", m_File->string());
}

// Counted again from 0 whenever something is still on its way, so the capture waits for the last of it
void SceneCapture::Update()
{
    if (!m_File)
    {
        return;
    }

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    if (m_Requested)
    {
        if (std::optional<Trinity::ViewCapture> l_Capture = l_Renderer.TakeCapture(*m_View))
        {
            std::vector<char> l_Bytes;
            stbi_write_png_to_func([](void* context, void* data, int length)
            {
                std::vector<char>& l_Out = *static_cast<std::vector<char>*>(context);
                l_Out.insert(l_Out.end(), static_cast<const char*>(data), static_cast<const char*>(data) + length);
            }, &l_Bytes, static_cast<int>(l_Capture->Width), static_cast<int>(l_Capture->Height), 4, l_Capture->Pixels.data(), static_cast<int>(l_Capture->Width * 4));

            std::ofstream l_Stream(*m_File, std::ios::binary | std::ios::trunc);
            l_Stream.write(l_Bytes.data(), static_cast<std::streamsize>(l_Bytes.size()));
            l_Stream.close();
            if (!l_Stream || l_Bytes.empty())
            {
                TR_ERROR("Capture: {} could not be written", m_File->string());
                Finish(false);

                return;
            }

            TR_INFO("Capture: wrote {}, {}x{}, {} bytes", m_File->string(), l_Capture->Width, l_Capture->Height, l_Bytes.size());
            Finish(true);
        }

        return;
    }

    if (!m_Session.HasProject())
    {
        TR_ERROR("Capture: no project is open, so there is nothing to capture");
        Finish(false);

        return;
    }

    if (std::chrono::steady_clock::now() >= m_Deadline)
    {
        TR_ERROR("Capture: the scene had not finished importing and loading after {} s, so nothing is captured", c_Timeout.count());
        Finish(false);

        return;
    }

    m_Settled = !m_Session.IsImportingAssets() && IsSceneLoaded() ? m_Settled + 1 : 0;
    if (m_Settled >= c_SettleFrames)
    {
        l_Renderer.RequestCapture(*m_View);
        m_Requested = true;
    }
}

void SceneCapture::Prepare()
{
    if (!m_File || !m_Session.HasProject())
    {
        return;
    }

    Trinity::Scene& l_Scene = m_Session.GetMainScene();
    const Trinity::Entity l_Camera = FindPrimaryCamera(l_Scene);
    if (!l_Camera)
    {
        TR_ERROR("Capture: the scene has no primary camera to capture it through");
        Finish(false);

        return;
    }

    const Trinity::CameraComponent& l_Component = l_Camera.Get<Trinity::CameraComponent>();
    Trinity::SceneOptions l_Options;
    l_Options.SampleCount = Trinity::Renderer3D::c_SampleCount;
    l_Options.Sprites = true;

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    l_Renderer.SetToneMapping(l_Component.GetToneMapping(), *m_View);
    l_Renderer.SubmitScene(l_Scene, Trinity::RenderView::FromCamera(l_Component, l_Camera.Get<Trinity::WorldTransformComponent>().Matrix, static_cast<float>(c_Width) / static_cast<float>(c_Height)), l_Options, *m_View);
}

// The textures sprites draw, the meshes and materials meshes draw, and the environments lighting the scene
bool SceneCapture::IsSceneLoaded() const
{
    Trinity::SceneRegistry& l_Registry = m_Session.GetMainScene().GetRegistry();
    for (const auto [it_Entity, it_Sprite] : l_Registry.view<Trinity::SpriteRendererComponent>().each())
    {
        if (!IsSettled(it_Sprite.Texture))
        {
            return false;
        }
    }

    for (const auto [it_Entity, it_Mesh] : l_Registry.view<Trinity::MeshRendererComponent>().each())
    {
        if (!IsSettled(it_Mesh.Mesh) || !std::ranges::all_of(it_Mesh.Materials, IsSettled))
        {
            return false;
        }
    }

    for (const auto [it_Entity, it_Environment] : l_Registry.view<Trinity::EnvironmentComponent>().each())
    {
        if (!IsSettled(it_Environment.Environment))
        {
            return false;
        }
    }

    return true;
}

void SceneCapture::Finish(bool written)
{
    m_File.reset();
    Trinity::Application::Get().Close(written ? 0 : 1);
}