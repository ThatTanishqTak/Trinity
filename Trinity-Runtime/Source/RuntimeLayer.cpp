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

    [[maybe_unused]] std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    // The module's build beside the pack, as the export copies it there
    [[maybe_unused]] std::filesystem::path GetModulePath(const std::filesystem::path& gameDirectory, std::string_view module)
    {
#if defined(TR_PLATFORM_WINDOWS)
        constexpr std::string_view c_Extension = ".dll";
#else
        constexpr std::string_view c_Extension = ".so";
#endif

        return gameDirectory / FromUtf8(std::format("{}{}", module, c_Extension));
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

RuntimeLayer::RuntimeLayer(std::optional<Trinity::GameSettings> game, std::filesystem::path gameDirectory, Trinity::DisplaySettings display) : Layer("Runtime"), m_Game(std::move(game)), m_GameDirectory(std::move(gameDirectory)), m_DisplaySettings(std::move(display))
{

}

// A game that cannot be played was reported where it failed, so the player only closes. The screen is the scripts' from the first one created
void RuntimeLayer::OnAttach()
{
    Trinity::Application& l_Application = Trinity::Application::Get();
    if (m_Game)
    {
        m_Display = Trinity::CreateScope<RuntimeDisplay>(m_DisplaySettings);
    }

    if (!m_Game || !OpenAssets() || !LoadStartScene(l_Application.GetSpecification().CommandLineArgs))
    {
        l_Application.Close();
    }
}

// The scene and its scripts go before the module whose code made them, and the game's assets before the registry that finds them. The window's settings are kept once no script can change them
void RuntimeLayer::OnDetach()
{
    m_Scenes.reset();
    m_Display.reset();
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
}

// The mix is the game's from the frame its mixer is ready, as Play's is in Forge
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

    if (m_Display)
    {
        m_Display->Apply();
    }
}

void RuntimeLayer::OnEvent(Trinity::Event& event)
{
    if (m_Display)
    {
        m_Display->OnEvent(event);
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

// The pack's asset table, which finds each asset the build holds by its UUID or its path in the project, and reads it where the pack has it. No .meta file is read, and no project folder
bool RuntimeLayer::OpenAssets()
{
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Table = Trinity::FileSystem::ReadFile(Trinity::AssetTable::GetPath());
    if (!l_Table)
    {
        TR_ERROR("Runtime: the game's pack holds no asset table that can be read: {}", Trinity::ToString(l_Table.GetError()));

        return false;
    }

    m_Registry = Trinity::CreateScope<Trinity::AssetRegistry>(Trinity::Project::c_AssetsMount);
    if (const Trinity::Expected<void, std::string> l_Loaded = m_Registry->LoadTable(*l_Table); !l_Loaded)
    {
        TR_ERROR("Runtime: the game's asset table could not be read: {}", l_Loaded.GetError());

        return false;
    }

    Trinity::AssetManager::SetRegistry(m_Registry.get());
    LoadModule();
    m_Mixer = Trinity::AssetRef<Trinity::AudioMixerAsset>(m_Game->Audio.Mixer);
    TR_INFO("Runtime: playing {} {} by {}, with {} scene(s) in its build", m_Game->Product, m_Game->Version, m_Game->Company, m_Game->Scenes.size());

    return true;
}

// A player trinity_add_game made has the module linked in, and registers its classes now that the engine has started. Beside the shared engine, the module's build beside the pack is loaded as Forge loads it. The prebuilt player with the static engine cannot load one
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

    if (m_Game->Module.empty())
    {
        return;
    }

#if defined(TR_ENGINE_SHARED)
    const std::filesystem::path l_Path = GetModulePath(m_GameDirectory, m_Game->Module);
    std::error_code l_Error;
    if (!std::filesystem::exists(l_Path, l_Error))
    {
        TR_ERROR("Runtime: {} has the C++ module {}, which is not at {}, so its scripts and components are missing", m_Game->Product, m_Game->Module, ToUtf8(l_Path));

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
            TR_ERROR("Runtime: {} was built for version {} of the module API, and this player has version {}, so it is not loaded. Exporting the game again builds it for this one", m_Game->Module, l_Built, Trinity::GameModule::c_ApiVersion);

            return;
        }
    }

    m_Module = std::move(*l_Library);
    m_UnloadModule = m_Module.GetFunction<Trinity::GameModule::UnloadFunction>(Trinity::GameModule::c_UnloadFunction);
    TR_INFO("Runtime: loaded the C++ module {} from {}", m_Game->Module, ToUtf8(l_Path));
    if (const Trinity::GameModule::LoadFunction l_Load = m_Module.GetFunction<Trinity::GameModule::LoadFunction>(Trinity::GameModule::c_LoadFunction))
    {
        l_Load();
    }
    else
    {
        TR_WARN("Runtime: {} exports no {}, so nothing in it runs", m_Game->Module, Trinity::GameModule::c_LoadFunction);
    }
#else
    TR_ERROR("Runtime: {} has the C++ module {}, which this player cannot load, since its engine is linked statically, so its scripts and components are missing. An export links the module into the player", m_Game->Product, m_Game->Module);
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

// The scene --scene names, by its path in the project or its UUID, which must be one of the build's, or the build's first, run by a scene manager with the game's physics, input actions and mix, as Play runs it in Forge
bool RuntimeLayer::LoadStartScene(const Trinity::ApplicationCommandLineArgs& args)
{
    Trinity::Application& l_Application = Trinity::Application::Get();
    m_Scenes = Trinity::CreateScope<Trinity::SceneManager>(m_Game->Physics, m_Registry.get(), m_Game->InputActions, l_Application.GetAudioEngine(), m_Game->Audio);
    if (const std::optional<std::string_view> l_Scene = args.GetOption("scene"); l_Scene && !l_Scene->empty())
    {
        const std::optional<Trinity::UUID> l_ID = Trinity::UUID::Parse(*l_Scene);

        return l_ID ? m_Scenes->Load(*l_ID) : m_Scenes->Load(*l_Scene);
    }

    return m_Scenes->Load(m_Game->Scenes.front());
}