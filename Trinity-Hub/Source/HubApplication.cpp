#include "HubLayer.hpp"

#include <Trinity/Core/EntryPoint.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    // How often Hub draws while behind another window or minimized, so it costs next to nothing
    constexpr float c_BackgroundFrameRate = 10.0f;

    std::optional<std::filesystem::path> GetSavesOption(const Trinity::ApplicationCommandLineArgs& args)
    {
        if (const std::optional<std::string_view> l_Saves = args.GetOption("saves"); l_Saves && !l_Saves->empty())
        {
            return std::filesystem::path(*l_Saves);
        }

        return std::nullopt;
    }

    // Hub in Trinity's user data folder, where every Forge finds it, or the folder --saves names
    std::filesystem::path GetHubDirectory(const Trinity::ApplicationCommandLineArgs& args)
    {
        return GetSavesOption(args).value_or(Trinity::ProjectList::GetHubDirectory());
    }

    // GitHub's API, or the stand-in --releases-url names, serving GitHub's replies, as a check does
    std::string GetReleasesUrl(const Trinity::ApplicationCommandLineArgs& args)
    {
        if (const std::optional<std::string_view> l_Url = args.GetOption("releases-url"); l_Url && !l_Url->empty())
        {
            return std::string(*l_Url);
        }

        return HubReleases::GetDefaultUrl();
    }

    // Versions beside Hub's own folder, or in the folder --saves names, unless the settings say
    std::filesystem::path GetInstallsDirectory(const Trinity::ApplicationCommandLineArgs& args)
    {
        const std::optional<std::filesystem::path> l_Saves = GetSavesOption(args);

        return l_Saves ? *l_Saves / "Versions" : Trinity::ProjectList::GetHubDirectory().parent_path() / "Versions";
    }
}

// Holds Hub.lock for as long as it runs, so no second Hub starts while it does
class HubApplication final : public Trinity::Application
{
public:
    HubApplication(Trinity::ApplicationSpecification specification, Trinity::FileLock lock) : Application(std::move(specification)), m_Lock(std::move(lock))
    {
        // One window, so ImGui keeps no imgui.ini and never leaves it
        PushOverlay<Trinity::ImGuiLayer>(Trinity::ImGuiLayerSpecification{ .KeepSettings = false, .Viewports = false });
        PushLayer<HubLayer>(GetSpecification().SavesDirectory, GetInstallsDirectory(GetSpecification().CommandLineArgs), GetReleasesUrl(GetSpecification().CommandLineArgs));
    }

private:
    Trinity::FileLock m_Lock;
};

// One Hub runs at a time: a second finds the first holding Hub.lock, brings it forward and ends
Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args)
{
    const std::filesystem::path l_Directory = GetHubDirectory(args);
    Expected<FileLock, FileLockRefusal> l_Lock = FileLock::TryAcquire(l_Directory / "Hub.lock");
    if (!l_Lock && l_Lock.GetError().Held)
    {
        const std::uint32_t l_Holder = l_Lock.GetError().HolderID;
        const bool l_Forward = l_Holder != 0 && Process::BringToFront(l_Holder);
        TR_INFO("Hub: Trinity Hub is already running{}, so this one {}ends", l_Holder != 0 ? std::format(" as process {}", l_Holder) : std::string(), l_Forward ? "brings it forward and " : "");

        return nullptr;
    }

    if (!l_Lock)
    {
        TR_WARN("Hub: {}, so a second Hub could start beside this one", l_Lock.GetError().Reason);
    }

    ApplicationSpecification l_Specification;
    l_Specification.Name = "Trinity Hub";
    l_Specification.Window.Width = 1920; 
    l_Specification.Window.Height = 1080;
    l_Specification.CommandLineArgs = args;
    l_Specification.StartDotNet = false;
    l_Specification.StartAudio = false;
    l_Specification.StartGameUI = false;
    l_Specification.SavesDirectory = l_Directory;
    l_Specification.BackgroundFrameRate = c_BackgroundFrameRate;

    return new HubApplication(std::move(l_Specification), l_Lock ? std::move(*l_Lock) : FileLock());
}