#include "RuntimeLayer.hpp"

#include <Trinity/Core/EntryPoint.hpp>

#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::string_view c_PackFileName = "Game.trpak";

    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    // The game's pack mounted at /cache, where every cooked asset is read, and the Game.trgame it holds
    Trinity::Expected<Trinity::GameSettings, std::string> OpenGame(const std::filesystem::path& pack)
    {
        if (!Trinity::FileSystem::MountPack(Trinity::Project::c_CacheMount, pack))
        {
            return Trinity::Unexpected{ std::format("{} could not be opened as the game's pack", ToUtf8(pack)) };
        }

        const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(Trinity::GetGameSettingsPath());
        if (!l_Text)
        {
            return Trinity::Unexpected{ std::format("{} holds no {} that can be read: {}", ToUtf8(pack), Trinity::GameSettings::c_FileName, Trinity::ToString(l_Text.GetError())) };
        }

        return Trinity::ParseGameSettings(*l_Text);
    }
}

// The player. Its main, with the Agility SDK's exports, is in this file, so every player that links the runtime's objects has them, the prebuilt one and a game's alike. The display was read as the window was made
class RuntimeApplication final : public Trinity::Application
{
public:
    RuntimeApplication(Trinity::ApplicationSpecification specification, std::optional<Trinity::GameSettings> game, std::filesystem::path gameDirectory, Trinity::Ref<Trinity::DisplaySettings> display) : Application(std::move(specification))
    {
        PushLayer<RuntimeLayer>(std::move(game), std::move(gameDirectory), std::move(*display));
    }
};

// The game's pack, Game.trpak beside the executable as an export leaves it, or the one --pack names, is opened before the application starts, so the game's saves and log go under its company and product, its window is named for it and opens as its player last left it, and its graphics APIs are tried in its order. A game that cannot be opened is reported, in a box unless headless, and the player closes at once without a window. C# scripts run in a player once their ahead-of-time library does, so .NET is not started
Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification l_Specification;
    l_Specification.Name = "Trinity Runtime";
    l_Specification.CommandLineArgs = args;
    l_Specification.StartDotNet = false;

    const std::optional<std::string_view> l_PackOption = args.GetOption("pack");
    const std::filesystem::path l_Pack = l_PackOption && !l_PackOption->empty() ? FromUtf8(*l_PackOption) : Process::GetExecutableDirectory() / c_PackFileName;
    Expected<GameSettings, std::string> l_Game = OpenGame(l_Pack);
    if (!l_Game)
    {
        const std::string l_Message = std::format("The game cannot start: {}", l_Game.GetError());
        TR_ERROR("Runtime: {}", l_Message);
        if (!args.HasOption("headless"))
        {
            Process::ShowErrorMessage(l_Specification.Name, l_Message);
        }

        l_Specification.Window.Headless = true;
        l_Specification.StartAudio = false;

        return new RuntimeApplication(std::move(l_Specification), std::nullopt, {}, CreateRef<DisplaySettings>());
    }

    l_Specification.Name = l_Game->Product;
    l_Specification.Company = l_Game->Company;
    l_Specification.GraphicsOrder = l_Game->GraphicsAPIs;

    // The player's choice is in its saves, which are mounted once the application knows the company and product
    const Ref<DisplaySettings> l_Display = CreateRef<DisplaySettings>();
    l_Specification.ConfigureWindow = [l_Display, l_Window = l_Game->Window](WindowSpecification& window)
    {
        *l_Display = RuntimeDisplay::Load(l_Window);
        RuntimeDisplay::Configure(*l_Display, window);
    };

    return new RuntimeApplication(std::move(l_Specification), std::move(*l_Game), l_Pack.parent_path(), l_Display);
}