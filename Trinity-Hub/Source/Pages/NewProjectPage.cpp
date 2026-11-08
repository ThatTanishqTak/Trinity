#include "Pages/NewProjectPage.hpp"

#include "HubText.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <utility>

namespace
{
    // The card chosen first, as Unity Hub's New project starts on its 3D template
    constexpr std::string_view c_FirstTemplate = "3D";
    constexpr std::string_view c_FirstName = "My Project";
    // A thumbnail is a small picture, so a file larger than this a side is not decoded
    constexpr int c_MaximumThumbnailSide = 4096;
    // Windows keeps these for devices, alone or before an extension, so no folder anywhere is named one
    constexpr std::array<std::string_view, 22> c_DeviceNames{ "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };

    struct StbDeleter
    {
        void operator()(stbi_uc* pixels) const
        {
            stbi_image_free(pixels);
        }
    };

    // The PNG as an ImGui texture of its own, which ImGui's renderer creates at the next frame, or none when it cannot be read
    std::unique_ptr<ImTextureData> LoadThumbnail(const std::filesystem::path& file)
    {
        std::ifstream l_Stream(file, std::ios::binary);
        const std::string l_Bytes((std::istreambuf_iterator<char>(l_Stream)), std::istreambuf_iterator<char>());
        int l_Width = 0;
        int l_Height = 0;
        int l_Channels = 0;
        const std::unique_ptr<stbi_uc, StbDeleter> l_Pixels(l_Bytes.empty() ? nullptr : stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(l_Bytes.data()), static_cast<int>(l_Bytes.size()), &l_Width, &l_Height, &l_Channels, 4));
        if (!l_Pixels || l_Width > c_MaximumThumbnailSide || l_Height > c_MaximumThumbnailSide)
        {
            const std::string l_Why = l_Bytes.empty() ? std::string("it could not be read") : !l_Pixels ? std::format("it is not a PNG it can read: {}", stbi_failure_reason()) : std::format("it is {}x{}, larger than {} a side", l_Width, l_Height, c_MaximumThumbnailSide);
            TR_WARN("Hub: the thumbnail {} is not shown, as {}", HubText::ToUtf8(file), l_Why);

            return nullptr;
        }

        std::unique_ptr<ImTextureData> l_Texture = std::make_unique<ImTextureData>();
        l_Texture->Create(ImTextureFormat_RGBA32, l_Width, l_Height);
        std::memcpy(l_Texture->GetPixels(), l_Pixels.get(), static_cast<std::size_t>(l_Width) * static_cast<std::size_t>(l_Height) * 4);
        ImGui::RegisterUserTexture(l_Texture.get());

        return l_Texture;
    }

    // Why a name cannot name a project's folder, by the rules of Windows, the narrowest of the platforms, so a project moves between them, or empty when it can. Spaces and any other letters are welcome
    std::string GetNameRefusal(std::string_view name)
    {
        if (name.find_first_not_of(' ') == std::string_view::npos)
        {
            return "Give the project a name";
        }

        if (name.find_first_of("<>:\"/\\|?*") != std::string_view::npos || std::ranges::any_of(name, [](char character) { return static_cast<unsigned char>(character) < 0x20; }))
        {
            return "A name cannot hold < > : \" / \\ | ? * or a control character";
        }

        if (name.front() == ' ' || name.back() == ' ' || name.back() == '.')
        {
            return "A name cannot begin or end with a space, or end with a dot";
        }

        std::string l_Stem(name.substr(0, name.find('.')));
        std::ranges::transform(l_Stem, l_Stem.begin(), [](char character) { return static_cast<char>(std::toupper(static_cast<unsigned char>(character))); });
        if (std::ranges::find(c_DeviceNames, std::string_view(l_Stem)) != c_DeviceNames.end())
        {
            return std::format("{} is a name Windows keeps for a device", name);
        }

        return std::string();
    }

    // A label in the column the project's settings share, with its field after it
    void DrawField(const char* label)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetFontSize() * 5.5f);
        ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
    }

    std::string DescribeVersion(const HubInstall& install)
    {
        return std::format("{}  {}", HubInstalls::Describe(install.Manifest), install.Source == HubInstall::Origin::Installed ? "Installed" : "Located");
    }
}

NewProjectPage::NewProjectPage(const HubInstalls& installs, const HubSettings& settings, HubPathPrompt& prompt, HubLauncher& launcher, std::filesystem::path defaultProjectsDirectory, std::function<void(HubLauncher::Report report)> created, std::function<void()> cancelled) : m_Installs(installs), m_Settings(settings), m_Prompt(prompt), m_Launcher(launcher), m_DefaultProjectsDirectory(std::move(defaultProjectsDirectory)), m_Created(std::move(created)), m_Cancelled(std::move(cancelled))
{

}

// Layers are detached the last pushed first, so ImGui's overlay has gone by now, having destroyed every texture it drew, and only their memory is left. Were ImGui still here, each would be let go of first, so it keeps no pointer to one freed
NewProjectPage::~NewProjectPage()
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    for (std::vector<std::unique_ptr<ImTextureData>>* it_List : { &m_Thumbnails, &m_Released })
    {
        for (const std::unique_ptr<ImTextureData>& it_Texture : *it_List)
        {
            ImGui::UnregisterUserTexture(it_Texture.get());
            ImGui::GetPlatformIO().Textures.find_erase(it_Texture.get());
        }
    }
}

const char* NewProjectPage::GetIcon() const
{
    return Trinity::Icons::c_Plus;
}

// The cards are read again at the next frame, as the version's folder may have been built again since
void NewProjectPage::OnShow()
{
    const HubInstall* l_Newest = m_Installs.FindNewestUsable();
    m_Version = l_Newest != nullptr ? l_Newest->Directory : std::filesystem::path();
    ReleaseThumbnails();
    m_Cards.clear();
    m_ReadVersion.reset();
    m_Location = m_Settings.GetProjectsDirectory(m_DefaultProjectsDirectory);
    m_Name = MakeName();
    m_Language.reset();
    m_Problem.clear();
}

// A thumbnail never changes once uploaded, so its pixels are let go of, and ImGui's renderer destroys one released at its next frame
void NewProjectPage::Update()
{
    for (const std::unique_ptr<ImTextureData>& it_Texture : m_Thumbnails)
    {
        if (it_Texture->Status == ImTextureStatus_OK && it_Texture->Pixels != nullptr)
        {
            it_Texture->DestroyPixels();
        }
    }

    std::erase_if(m_Released, [](const std::unique_ptr<ImTextureData>& texture)
    {
        if (texture->Status != ImTextureStatus_Destroyed)
        {
            return false;
        }

        ImGui::UnregisterUserTexture(texture.get());

        return true;
    });
}

void NewProjectPage::Draw()
{
    HubPages::DrawTitle("New Project");
    const HubInstall* l_Version = GetVersion();
    if (l_Version == nullptr)
    {
        if (ImGui::Button(std::format("{} Back", Trinity::Icons::c_ChevronLeft).c_str()))
        {
            Leave();
        }

        HubPages::DrawEmpty("No version to make a project in", "A project is made by a version of Trinity installed here. The Installs page installs one from GitHub or from a file, or lists a build with Locate.");

        return;
    }

    if (!m_ReadVersion || *m_ReadVersion != l_Version->Directory)
    {
        ReadTemplates();
    }

    DrawVersion();
    ImGui::Spacing();

    const float l_Spacing = ImGui::GetStyle().ItemSpacing.x * 3.0f;
    const float l_Available = ImGui::GetContentRegionAvail().x;
    const float l_DetailsWidth = std::clamp(l_Available * 0.4f, ImGui::GetFontSize() * 18.0f, ImGui::GetFontSize() * 26.0f);
    if (ImGui::BeginChild("##Templates", ImVec2(std::max(l_Available - l_DetailsWidth - l_Spacing, ImGui::GetFontSize() * 10.0f), 0.0f)))
    {
        DrawCards();
    }

    ImGui::EndChild();
    ImGui::SameLine(0.0f, l_Spacing);
    if (ImGui::BeginChild("##Details", ImVec2(0.0f, 0.0f)))
    {
        DrawDetails();
        DrawSettings();
    }

    ImGui::EndChild();
}

// The versions this machine can make projects in, as the Installs page lists them, newest first, in a box as wide as the longest of them
void NewProjectPage::DrawVersion()
{
    const HubInstall* l_Version = GetVersion();
    float l_Width = 0.0f;
    for (const HubInstall& it_Install : m_Installs.GetInstalls())
    {
        if (HubInstalls::IsUsable(it_Install))
        {
            l_Width = std::max(l_Width, ImGui::CalcTextSize(DescribeVersion(it_Install).c_str()).x);
        }
    }

    HubPages::DrawLabel("Version");
    ImGui::SetNextItemWidth(l_Width + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f);
    const bool l_Open = ImGui::BeginCombo("##Version", DescribeVersion(*l_Version).c_str());
    if (!l_Open && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Its own Forge makes the project in its formats, and the project opens in it from then on");
    }

    if (l_Open)
    {
        for (const HubInstall& it_Install : m_Installs.GetInstalls())
        {
            if (!HubInstalls::IsUsable(it_Install))
            {
                continue;
            }

            const std::string l_Folder = HubText::ToUtf8(it_Install.Directory);
            ImGui::PushID(l_Folder.c_str());
            if (ImGui::Selectable(DescribeVersion(it_Install).c_str(), &it_Install == l_Version))
            {
                m_Version = it_Install.Directory;
                m_Problem.clear();
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("%s", l_Folder.c_str());
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();
    }
}

// As many to a row as fit, each its thumbnail over its name
void NewProjectPage::DrawCards()
{
    HubPages::DrawSection("Templates");
    const float l_Width = ImGui::GetFontSize() * 12.0f;
    const float l_Spacing = ImGui::GetStyle().ItemSpacing.x;
    const std::size_t l_PerRow = static_cast<std::size_t>(std::max(1.0f, (ImGui::GetContentRegionAvail().x + l_Spacing) / (l_Width + l_Spacing)));
    for (std::size_t it_Index = 0; it_Index < m_Cards.size(); ++it_Index)
    {
        if (it_Index % l_PerRow != 0)
        {
            ImGui::SameLine();
        }

        DrawCard(it_Index, l_Width);
    }
}

// The whole card is one selectable, drawn over with its thumbnail and its name, the chosen one lit and outlined
void NewProjectPage::DrawCard(std::size_t index, float width)
{
    const Card& l_Card = m_Cards[index];
    const float l_Padding = ImGui::GetStyle().FramePadding.x;
    const float l_PictureHeight = (width - l_Padding * 2.0f) * 9.0f / 16.0f;
    const float l_Height = l_PictureHeight + ImGui::GetTextLineHeight() + l_Padding * 3.0f;
    const ImVec2 l_Start = ImGui::GetCursorScreenPos();

    ImGui::PushID(static_cast<int>(index));
    if (ImGui::Selectable("##Card", index == m_Card, ImGuiSelectableFlags_None, ImVec2(width, l_Height)))
    {
        m_Card = index;
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("%s", l_Card.Template ? l_Card.Template->Description.c_str() : "An empty start scene, and nothing else");
    }

    ImGui::PopID();

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    const float l_Rounding = ImGui::GetStyle().FrameRounding;
    l_DrawList.AddRect(l_Start, ImVec2(l_Start.x + width, l_Start.y + l_Height), ImGui::GetColorU32(index == m_Card ? ImGuiCol_HeaderActive : ImGuiCol_Border), l_Rounding, index == m_Card ? 2.0f : 1.0f);
    DrawPicture(l_Card, ImVec2(l_Start.x + l_Padding, l_Start.y + l_Padding), ImVec2(l_Start.x + width - l_Padding, l_Start.y + l_Padding + l_PictureHeight));

    const char* l_Title = l_Card.Template ? l_Card.Template->Title.c_str() : "Empty";
    ImFont* l_Bold = Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold);
    const ImVec2 l_TitleAt(l_Start.x + l_Padding, l_Start.y + l_Padding * 2.0f + l_PictureHeight);
    l_DrawList.PushClipRect(l_TitleAt, ImVec2(l_Start.x + width - l_Padding, l_Start.y + l_Height), true);
    l_DrawList.AddText(l_Bold, ImGui::GetFontSize(), l_TitleAt, ImGui::GetColorU32(ImGuiCol_Text), l_Title);
    l_DrawList.PopClipRect();
}

// The chosen card's picture at the width of the column, its name and what it holds
void NewProjectPage::DrawDetails()
{
    if (m_Card >= m_Cards.size())
    {
        return;
    }

    const Card& l_Card = m_Cards[m_Card];
    const float l_Width = ImGui::GetContentRegionAvail().x;
    const ImVec2 l_Start = ImGui::GetCursorScreenPos();
    const ImVec2 l_End(l_Start.x + l_Width, l_Start.y + l_Width * 9.0f / 16.0f);
    DrawPicture(l_Card, l_Start, l_End);
    ImGui::Dummy(ImVec2(l_Width, l_End.y - l_Start.y));

    ImGui::Spacing();
    ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), ImGui::GetStyle().FontSizeBase * 1.2f);
    ImGui::TextUnformatted(l_Card.Template ? l_Card.Template->Title.c_str() : "Empty");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", l_Card.Template ? l_Card.Template->Description.c_str() : "An empty start scene, ticked in the build, and nothing else");
    ImGui::PopStyleColor();
}

// The name, the folder it goes in and the language, then the folder the project is made in, or why it cannot be, and Create
void NewProjectPage::DrawSettings()
{
    HubPages::DrawSection("Project");

    DrawField("Name");
    ImGui::InputText("##Name", &m_Name);

    DrawField("Location");
    const float l_Browse = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - l_Browse - ImGui::GetStyle().ItemSpacing.x);
    std::string l_Location = HubText::ToUtf8(m_Location);
    if (ImGui::InputText("##Location", &l_Location))
    {
        m_Location = HubText::FromUtf8(l_Location);
    }

    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        m_Prompt.Ask({ HubPathPrompt::Kind::Folder, "The folder the project is made in",{}, m_Location, [this](const std::filesystem::path& path) { m_Location = path; } });
    }

    DrawField("Language");
    if (ImGui::RadioButton("C#", m_Language == Trinity::ScriptLanguage::CSharp))
    {
        m_Language = Trinity::ScriptLanguage::CSharp;
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("C++", m_Language == Trinity::ScriptLanguage::Cpp))
    {
        m_Language = Trinity::ScriptLanguage::Cpp;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Picked once, as the project is made, and never changed. Forge shows only that language's menus, templates and scripts, and builds only it. Its platform is picked in Forge's Build panel.");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    std::string l_Refusal;
    const std::optional<HubLauncher::Creation> l_Creation = GetCreation(l_Refusal);
    if (l_Creation)
    {
        ImGui::TextDisabled("Made in");
        ImGui::TextWrapped("%s", HubText::ToUtf8(l_Creation->Directory).c_str());
    }
    else
    {
        HubPages::DrawWarning(l_Refusal);
    }

    if (!m_Problem.empty())
    {
        HubPages::DrawWarning(m_Problem);
    }

    ImGui::Spacing();
    if (ImGui::Button("Cancel"))
    {
        Leave();

        return;
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!l_Creation);
    if (ImGui::Button(std::format("{} Create Project", Trinity::Icons::c_Plus).c_str()))
    {
        Create();
    }

    ImGui::EndDisabled();
    if (!l_Creation && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", l_Refusal.c_str());
    }
}

void NewProjectPage::DrawPicture(const Card& card, const ImVec2& minimum, const ImVec2& maximum)
{
    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
    const float l_Width = maximum.x - minimum.x;
    const float l_Height = maximum.y - minimum.y;
    // Once ImGui's renderer has made the texture, which it does in the frame after the thumbnail is read, so a texture it could not make is never drawn
    if (card.Thumbnail != nullptr && card.Thumbnail->Status == ImTextureStatus_OK && card.Thumbnail->Width > 0 && card.Thumbnail->Height > 0)
    {
        const float l_Scale = std::min(l_Width / static_cast<float>(card.Thumbnail->Width), l_Height / static_cast<float>(card.Thumbnail->Height));
        const ImVec2 l_Size(static_cast<float>(card.Thumbnail->Width) * l_Scale, static_cast<float>(card.Thumbnail->Height) * l_Scale);
        const ImVec2 l_At(minimum.x + (l_Width - l_Size.x) * 0.5f, minimum.y + (l_Height - l_Size.y) * 0.5f);
        l_DrawList.AddImage(ImTextureRef(card.Thumbnail->TexID), l_At, ImVec2(l_At.x + l_Size.x, l_At.y + l_Size.y));

        return;
    }

    const char* l_Icon = card.Template ? Trinity::Icons::c_FileImage : Trinity::Icons::c_File;
    const float l_IconSize = l_Height * 0.35f;
    const ImVec2 l_IconExtent = ImGui::CalcTextSize(l_Icon);
    const float l_IconScale = l_IconSize / ImGui::GetFontSize();
    l_DrawList.AddText(nullptr, l_IconSize, ImVec2(minimum.x + (l_Width - l_IconExtent.x * l_IconScale) * 0.5f, minimum.y + (l_Height - l_IconExtent.y * l_IconScale) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), l_Icon);
}

// Each reason in the order the page is filled in, the language last, as it is picked last
std::optional<HubLauncher::Creation> NewProjectPage::GetCreation(std::string& refusal) const
{
    if (GetVersion() == nullptr)
    {
        refusal = "No version is here to make it";

        return std::nullopt;
    }

    if (m_Card >= m_Cards.size())
    {
        refusal = "Pick a template";

        return std::nullopt;
    }

    refusal = GetNameRefusal(m_Name);
    if (!refusal.empty())
    {
        return std::nullopt;
    }

    std::error_code l_Error;
    if (m_Location.empty() || !m_Location.is_absolute())
    {
        refusal = "Choose the folder it goes in, as a whole path";

        return std::nullopt;
    }

    if (std::filesystem::exists(m_Location, l_Error) && !std::filesystem::is_directory(m_Location, l_Error))
    {
        refusal = std::format("{} is a file, not a folder", HubText::ToUtf8(m_Location));

        return std::nullopt;
    }

    const std::filesystem::path l_Directory = m_Location / HubText::FromUtf8(m_Name);
    if (std::filesystem::exists(l_Directory, l_Error) && (!std::filesystem::is_directory(l_Directory, l_Error) || !std::filesystem::is_empty(l_Directory, l_Error)))
    {
        refusal = std::format("{} is there already and is not an empty folder", HubText::ToUtf8(l_Directory));

        return std::nullopt;
    }

    if (!m_Language)
    {
        refusal = "Pick the language its scripts are written in";

        return std::nullopt;
    }

    const Card& l_Card = m_Cards[m_Card];

    return HubLauncher::Creation{ l_Directory, l_Card.Template ? l_Card.Template->Name : std::string(), *m_Language };
}

void NewProjectPage::Create()
{
    std::string l_Refusal;
    const std::optional<HubLauncher::Creation> l_Creation = GetCreation(l_Refusal);
    const HubInstall* l_Version = GetVersion();
    if (!l_Creation || l_Version == nullptr)
    {
        return;
    }

    HubLauncher::Report l_Report = m_Launcher.Create(*l_Creation, *l_Version);
    if (l_Report.Failed)
    {
        TR_WARN("Hub: {}", l_Report.Message);
        m_Problem = std::move(l_Report.Message);

        return;
    }

    ReleaseThumbnails();
    m_Cards.clear();
    m_ReadVersion.reset();
    m_Created(std::move(l_Report));
}

void NewProjectPage::Leave()
{
    ReleaseThumbnails();
    m_Cards.clear();
    m_ReadVersion.reset();
    m_Cancelled();
}

// The card chosen before is chosen again in the version read, by its template's name, and the 3D one at first
void NewProjectPage::ReadTemplates()
{
    std::string l_Chosen(c_FirstTemplate);
    if (m_Card < m_Cards.size())
    {
        l_Chosen = m_Cards[m_Card].Template ? m_Cards[m_Card].Template->Name : std::string();
    }

    ReleaseThumbnails();
    m_Cards.clear();
    const HubInstall* l_Version = GetVersion();
    m_ReadVersion = l_Version != nullptr ? l_Version->Directory : std::filesystem::path();
    if (l_Version != nullptr)
    {
        for (Trinity::ProjectTemplate& it_Template : Trinity::FindProjectTemplates(l_Version->Directory, l_Version->Manifest.Formats.Project))
        {
            Card l_Card{ std::move(it_Template), nullptr };
            if (!l_Card.Template->Thumbnail.empty())
            {
                if (std::unique_ptr<ImTextureData> l_Texture = LoadThumbnail(l_Card.Template->Thumbnail))
                {
                    l_Card.Thumbnail = l_Texture.get();
                    m_Thumbnails.push_back(std::move(l_Texture));
                }
            }

            m_Cards.push_back(std::move(l_Card));
        }
    }

    m_Cards.push_back({});
    const auto a_Chosen = std::ranges::find_if(m_Cards, [&l_Chosen](const Card& card) { return card.Template ? card.Template->Name == l_Chosen : l_Chosen.empty(); });
    m_Card = a_Chosen != m_Cards.end() ? static_cast<std::size_t>(a_Chosen - m_Cards.begin()) : 0;
    TR_INFO("Hub: New Project offers {} template(s) of {}", m_Cards.size() - 1, l_Version != nullptr ? HubInstalls::Describe(l_Version->Manifest) : std::string("no version"));
}

// Destroyed by ImGui's renderer at its next frame, then freed by Update
void NewProjectPage::ReleaseThumbnails()
{
    for (std::unique_ptr<ImTextureData>& it_Texture : m_Thumbnails)
    {
        it_Texture->WantDestroyNextFrame = true;
        m_Released.push_back(std::move(it_Texture));
    }

    m_Thumbnails.clear();
    for (Card& it_Card : m_Cards)
    {
        it_Card.Thumbnail = nullptr;
    }
}

const HubInstall* NewProjectPage::GetVersion() const
{
    if (const HubInstall* l_Install = m_Version.empty() ? nullptr : m_Installs.Find(m_Version); l_Install != nullptr && HubInstalls::IsUsable(*l_Install))
    {
        return l_Install;
    }

    return m_Installs.FindNewestUsable();
}

std::string NewProjectPage::MakeName() const
{
    std::string l_Name(c_FirstName);
    std::error_code l_Error;
    for (int it_Number = 1; it_Number < 1000 && std::filesystem::exists(m_Location / HubText::FromUtf8(l_Name), l_Error); ++it_Number)
    {
        l_Name = std::format("{} ({})", c_FirstName, it_Number);
    }

    return l_Name;
}