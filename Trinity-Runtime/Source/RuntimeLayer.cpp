#include "RuntimeLayer.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    // Where Forge builds the project's module: Build/<configuration>/Modules, in Debug beside a Debug engine and in Release otherwise
    [[maybe_unused]] std::filesystem::path GetModulePath(const Trinity::Project& project)
    {
#if defined(TR_DEBUG)
        constexpr std::string_view c_Configuration = "Debug";
#else
        constexpr std::string_view c_Configuration = "Release";
#endif
#if defined(TR_PLATFORM_WINDOWS)
        constexpr std::string_view c_Extension = ".dll";
#else
        constexpr std::string_view c_Extension = ".so";
#endif

        return project.GetBuildDirectory() / c_Configuration / "Modules" / FromUtf8(std::format("{}{}", project.GetModuleName(), c_Extension));
    }

    // The first primary camera in hierarchy order, as the Game panel finds it
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
}

RuntimeLayer::RuntimeLayer() : Layer("Runtime")
{

}

// A player that cannot find what to play says why once, and closes
void RuntimeLayer::OnAttach()
{
    Trinity::Application& l_Application = Trinity::Application::Get();
    const Trinity::ApplicationCommandLineArgs& l_Args = l_Application.GetSpecification().CommandLineArgs;
    const std::optional<std::string_view> l_Project = l_Args.GetOption("project");
    if (!l_Project || l_Project->empty())
    {
        TR_ERROR("Runtime: no project was given. Pass --project=<path to its .trproj>, and --scene=<a scene's path in its Assets folder> to start somewhere other than its start scene");
        l_Application.Close();

        return;
    }

    if (!OpenProject(FromUtf8(*l_Project)) || !LoadStartScene(l_Args))
    {
        l_Application.Close();
    }
}

// The scene and its scripts go before the module whose code made them, and the project's assets before the registry that finds them
void RuntimeLayer::OnDetach()
{
    m_Scenes.reset();
    m_Mixer = {};
    UnloadModule();

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    l_Renderer.GetRenderer2D().ReleaseTextures();
    l_Renderer.GetRenderer3D().ReleaseAssets();
    if (m_Registry)
    {
        Trinity::AssetManager::SetRegistry(nullptr);
        m_Registry.reset();
    }

    m_Project.reset();
}

// The mix is the project's from the frame its mixer is ready, as Play's is in Forge
void RuntimeLayer::OnUpdate(Trinity::Timestep timestep)
{
    if (!m_MixerApplied && m_Mixer.IsReady())
    {
        m_MixerApplied = true;
        if (Trinity::AudioEngine* l_Engine = Trinity::Application::Get().GetAudioEngine(); l_Engine != nullptr)
        {
            static_cast<void>(l_Engine->GetMixer().Apply(m_Mixer.Get()->GetData()));
        }
    }

    if (m_Scenes)
    {
        m_Scenes->Update(timestep);
    }
}

// Through the scene's primary camera, for the window's shape, with the camera's exposure and curve, the scene's sprites and 4x MSAA, as the Game panel draws it. Nothing is drawn while the start scene loads, or in a scene with no camera
void RuntimeLayer::OnPrepareRender([[maybe_unused]] Trinity::RHI::CommandList& commands)
{
    Trinity::Scene* l_Scene = m_Scenes ? m_Scenes->GetScene() : nullptr;
    if (l_Scene == nullptr)
    {
        return;
    }

    l_Scene->UpdateWorldTransforms();
    const Trinity::Entity l_Camera = FindPrimaryCamera(*l_Scene);
    if (!l_Camera)
    {
        if (!std::exchange(m_WarnedNoCamera, true))
        {
            TR_WARN("Runtime: the scene has no primary camera, so nothing is drawn");
        }

        return;
    }

    m_WarnedNoCamera = false;
    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const float l_AspectRatio = static_cast<float>(l_Renderer.GetSceneWidth()) / static_cast<float>(std::max(l_Renderer.GetSceneHeight(), 1u));
    const Trinity::CameraComponent& l_Component = l_Camera.Get<Trinity::CameraComponent>();

    Trinity::SceneOptions l_Options;
    l_Options.SampleCount = Trinity::Renderer3D::c_SampleCount;
    l_Options.Sprites = true;

    l_Renderer.SetToneMapping(l_Component.GetToneMapping());
    l_Renderer.SubmitScene(*l_Scene, Trinity::RenderView::FromCamera(l_Component, l_Camera.Get<Trinity::WorldTransformComponent>().Matrix, l_AspectRatio), l_Options);
}

// Opened, mounted and scanned as Forge opens it, but read only: nothing is imported, so the scenes find what Forge last imported into the project's cache
bool RuntimeLayer::OpenProject(const std::filesystem::path& path)
{
    Trinity::Expected<Trinity::Scope<Trinity::Project>, std::string> l_Project = Trinity::Project::Open(path);
    if (!l_Project)
    {
        TR_ERROR("Runtime: the project could not be opened: {}", l_Project.GetError());

        return false;
    }

    m_Project = std::move(*l_Project);
    m_Registry = Trinity::CreateScope<Trinity::AssetRegistry>(Trinity::Project::c_AssetsMount);
    Trinity::AssetManager::SetRegistry(m_Registry.get());
    static_cast<void>(m_Registry->Scan());

    Trinity::Application::Get().GetRenderer().SetTitle(m_Project->GetName());
    LoadModule();
    m_Mixer = Trinity::AssetRef<Trinity::AudioMixerAsset>(m_Project->GetAudioSettings().Mixer);
    TR_INFO("Runtime: opened {} from {}", m_Project->GetName(), ToUtf8(m_Project->GetDirectory()));

    return true;
}

// Beside the shared engine, the module's build as Forge last made it, loaded as Forge loads it. The static engine cannot load a module, so a game's player has it linked in by trinity_add_game, and registers its classes now that the engine has started
void RuntimeLayer::LoadModule()
{
#if !defined(TR_ENGINE_SHARED)
    if (Trinity::GameModule::Detail::GetLinked())
    {
        Trinity::GameModule::Detail::RegisterAll();
        m_ModuleLinked = true;

        return;
    }
#endif

    if (!m_Project->HasModule())
    {
        return;
    }

#if defined(TR_ENGINE_SHARED)
    const std::filesystem::path l_Path = GetModulePath(*m_Project);
    std::error_code l_Error;
    if (!std::filesystem::exists(l_Path, l_Error))
    {
        TR_ERROR("Runtime: {} has the C++ module {}, which is not built at {}, so its scripts and components are missing. Open the project in Forge to build it", m_Project->GetName(), m_Project->GetModuleName(), ToUtf8(l_Path));

        return;
    }

    Trinity::Expected<Trinity::SharedLibrary, std::string> l_Library = Trinity::SharedLibrary::Load(l_Path);
    if (!l_Library)
    {
        TR_ERROR("Runtime: {}", l_Library.GetError());

        return;
    }

    if (const Trinity::GameModule::ApiVersionFunction l_Version = l_Library->GetFunction<Trinity::GameModule::ApiVersionFunction>(Trinity::GameModule::c_ApiVersionFunction))
    {
        if (const std::uint32_t l_Built = l_Version(); l_Built != Trinity::GameModule::c_ApiVersion)
        {
            TR_ERROR("Runtime: {} was built for version {} of the module API, and this player has version {}, so it is not loaded. Building it again in Forge builds it for this one", m_Project->GetModuleName(), l_Built, Trinity::GameModule::c_ApiVersion);

            return;
        }
    }

    m_Module = std::move(*l_Library);
    m_UnloadModule = m_Module.GetFunction<Trinity::GameModule::UnloadFunction>(Trinity::GameModule::c_UnloadFunction);
    TR_INFO("Runtime: loaded the C++ module {} from {}", m_Project->GetModuleName(), ToUtf8(l_Path));
    if (const Trinity::GameModule::LoadFunction l_Load = m_Module.GetFunction<Trinity::GameModule::LoadFunction>(Trinity::GameModule::c_LoadFunction))
    {
        l_Load();
    }
    else
    {
        TR_WARN("Runtime: {} exports no {}, so nothing in it runs", m_Project->GetModuleName(), Trinity::GameModule::c_LoadFunction);
    }
#else
    TR_ERROR("Runtime: {} has the C++ module {}, which this player cannot load, since its engine is linked statically, so its scripts and components are missing. An export links the module into the player", m_Project->GetName(), m_Project->GetModuleName());
#endif
}

void RuntimeLayer::UnloadModule()
{
    if (std::exchange(m_ModuleLinked, false))
    {
        Trinity::GameModule::Detail::UnregisterAll();

        return;
    }

    if (!m_Module.IsLoaded())
    {
        return;
    }

    if (const Trinity::GameModule::UnloadFunction l_Unload = std::exchange(m_UnloadModule, nullptr))
    {
        l_Unload();
    }

    m_Module.Unload();
}

// The scene --scene names, by its path in the Assets folder or its UUID, or the project's start scene, run by a scene manager with the project's physics, input actions and mix, as Play runs it in Forge
bool RuntimeLayer::LoadStartScene(const Trinity::ApplicationCommandLineArgs& args)
{
    Trinity::Application& l_Application = Trinity::Application::Get();
    m_Scenes = Trinity::CreateScope<Trinity::SceneManager>(m_Project->GetPhysicsSettings(), m_Registry.get(), m_Project->GetInputActions(), l_Application.GetAudioEngine(), m_Project->GetAudioSettings());
    if (const std::optional<std::string_view> l_Scene = args.GetOption("scene"); l_Scene && !l_Scene->empty())
    {
        const std::optional<Trinity::UUID> l_ID = Trinity::UUID::Parse(*l_Scene);

        return l_ID ? m_Scenes->Load(*l_ID) : m_Scenes->Load(*l_Scene);
    }

    static_cast<void>(m_Project->ResolveStartScenePath(*m_Registry));
    const Trinity::UUID l_StartScene = m_Project->GetStartScene();
    if (!l_StartScene)
    {
        TR_ERROR("Runtime: {} has no start scene. Set one in Forge, or pass --scene=<a scene's path in its Assets folder>", m_Project->GetName());

        return false;
    }

    return m_Scenes->Load(l_StartScene);
}