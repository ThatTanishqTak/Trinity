#include "ProjectThumbnail.hpp"

#include "SceneCapture.hpp"

#include <string>
#include <system_error>
#include <utility>

namespace
{
    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }
}

ProjectThumbnail::~ProjectThumbnail()
{
    if (m_View)
    {
        Trinity::Application::Get().GetRenderer().DestroyView(*m_View);
    }
}

// One already drawn is finished first, as it shows a save only moments older
void ProjectThumbnail::Request()
{
    if (!m_Session.HasProject() || m_State == State::Capturing)
    {
        return;
    }

    m_File = Trinity::Project::GetThumbnailFile(m_Session.GetProject()->GetDirectory());
    m_Deadline = std::chrono::steady_clock::now() + c_Wait;
    m_State = State::Waiting;
}

void ProjectThumbnail::Update()
{
    if (m_State == State::Idle)
    {
        return;
    }

    if (!m_Session.HasProject() || Trinity::Project::GetThumbnailFile(m_Session.GetProject()->GetDirectory()) != m_File)
    {
        Stop();

        return;
    }

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    if (m_State == State::Capturing)
    {
        if (const std::optional<Trinity::ViewCapture> l_Capture = l_Renderer.TakeCapture(*m_View))
        {
            Write(*l_Capture);
            Stop();
        }

        return;
    }

    // While it plays, the scene drawn is the game's and not the one saved, so the picture waits for Stop
    const std::chrono::steady_clock::time_point l_Now = std::chrono::steady_clock::now();
    if (m_Session.IsPlaying())
    {
        m_Deadline = l_Now + c_Wait;

        return;
    }

    if ((m_Session.IsImportingAssets() || !SceneCapture::IsLoaded(m_Session.GetMainScene())) && l_Now < m_Deadline)
    {
        return;
    }

    m_View = l_Renderer.CreateView(SceneCapture::c_Width, SceneCapture::c_Height);
    l_Renderer.RequestCapture(*m_View);
    m_State = State::Capturing;
    m_Draw = true;
}

// Drawn as the Game panel draws it, so the picture is what the game would show
void ProjectThumbnail::Prepare(const Trinity::RenderView& sceneCamera)
{
    if (!std::exchange(m_Draw, false) || !m_View)
    {
        return;
    }

    Trinity::SceneOptions l_Options;
    l_Options.SampleCount = Trinity::Renderer3D::c_SampleCount;
    l_Options.Sprites = true;

    Trinity::Scene& l_Scene = m_Session.GetMainScene();
    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    if (const Trinity::Entity l_Camera = SceneCapture::FindPrimaryCamera(l_Scene))
    {
        const Trinity::CameraComponent& l_Component = l_Camera.Get<Trinity::CameraComponent>();
        l_Renderer.SetToneMapping(l_Component.GetToneMapping(), *m_View);
        l_Renderer.SubmitScene(l_Scene, Trinity::RenderView::FromCamera(l_Component, l_Camera.Get<Trinity::WorldTransformComponent>().Matrix, static_cast<float>(SceneCapture::c_Width) / static_cast<float>(SceneCapture::c_Height)), l_Options, *m_View);

        return;
    }

    // No primary camera, so from where the Scene panel looks, tone mapped as that panel is
    l_Renderer.SetToneMapping(l_Renderer.GetToneMapping(), *m_View);
    l_Renderer.SubmitScene(l_Scene, sceneCamera, l_Options, *m_View);
}

// Beside the file first and then moved over it, so Hub never reads half a picture
void ProjectThumbnail::Write(const Trinity::ViewCapture& capture) const
{
    std::filesystem::path l_Temporary = m_File;
    l_Temporary += ".tmp";
    std::error_code l_Error;
    if (SceneCapture::WritePng(capture, l_Temporary) != 0)
    {
        std::filesystem::rename(l_Temporary, m_File, l_Error);
        if (!l_Error)
        {
            return;
        }
    }

    // Only a picture is lost, so this is told in the log and not as a warning over the editor
    TR_INFO("Forge: the project's picture for Hub was not written to {}", ToUtf8(m_File));
    std::filesystem::remove(l_Temporary, l_Error);
}

void ProjectThumbnail::Stop()
{
    if (m_View)
    {
        Trinity::Application::Get().GetRenderer().DestroyView(*m_View);
        m_View.reset();
    }

    m_State = State::Idle;
    m_Draw = false;
}