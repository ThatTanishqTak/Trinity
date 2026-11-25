#include "Toasts.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <format>
#include <mutex>
#include <string_view>

namespace
{
    constexpr float c_WidthInFonts = 24.0f;
    // Seconds a toast is shown, by its kind, the last of them fading
    constexpr std::array<float, 3> c_Lives{ 3.0f, 6.0f, 9.0f };
    constexpr float c_FadeSeconds = 0.4f;
    // More than these at once and the rest are counted in one toast, so a build's errors do not push each other out
    constexpr std::size_t c_Shown = 4;
    // A toast is a line or two, and the Console has the rest
    constexpr std::size_t c_TextLimit = 180;
    // Every line of Forge's own begins with it, which a toast has no need to say
    constexpr std::string_view c_OwnPrefix = "Forge: ";
    constexpr float c_EdgeInFonts = 0.25f;

    std::mutex s_NotifiedLock;
    std::vector<std::string> s_Notified;

    // Without Forge's prefix, the word after which then begins the toast and so takes a capital. Cut between two characters, not through one
    std::string Shorten(std::string_view text)
    {
        const bool l_Own = text.starts_with(c_OwnPrefix);
        if (l_Own)
        {
            text.remove_prefix(c_OwnPrefix.size());
        }

        std::size_t l_End = std::min(text.size(), c_TextLimit);
        while (l_End < text.size() && l_End > 0 && (static_cast<unsigned char>(text[l_End]) & 0xC0) == 0x80)
        {
            --l_End;
        }

        std::string l_Text = l_End < text.size() ? std::format("{}...", text.substr(0, l_End)) : std::string(text);
        if (l_Own && !l_Text.empty())
        {
            l_Text.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(l_Text.front())));
        }

        return l_Text;
    }
}

void Toasts::Notify(std::string text)
{
    const std::scoped_lock l_Lock(s_NotifiedLock);
    s_Notified.push_back(std::move(text));
}

// Forge's own warnings and errors, and the engine's errors, since the engine warns of much that is no one's to act on
void Toasts::ReadLog()
{
    std::vector<std::pair<Kind, std::string>> l_Logged;
    {
        // Copied out, so the log is locked no longer than it takes to read
        const Trinity::LogHistory::Reader l_History = Trinity::LogHistory::Read();
        const std::size_t l_Count = l_History.GetCount();
        const std::uint64_t l_First = l_Count > 0 ? l_History[0].Sequence : m_NextSequence;
        for (std::size_t it_Index = m_NextSequence > l_First ? static_cast<std::size_t>(m_NextSequence - l_First) : 0; it_Index < l_Count; ++it_Index)
        {
            const Trinity::LogEntry& l_Entry = l_History[it_Index];
            if (l_Entry.Level >= (l_Entry.Channel == Trinity::LogChannel::Client ? Trinity::LogLevel::Warn : Trinity::LogLevel::Error))
            {
                l_Logged.emplace_back(l_Entry.Level == Trinity::LogLevel::Warn ? Kind::Warning : Kind::Error, std::string(l_Entry.Text));
            }
        }

        if (l_Count > 0)
        {
            m_NextSequence = l_History[l_Count - 1].Sequence + 1;
        }
    }

    for (std::pair<Kind, std::string>& it_Logged : l_Logged)
    {
        Add(it_Logged.first, std::move(it_Logged.second));
    }

    std::vector<std::string> l_Notified;
    {
        const std::scoped_lock l_Lock(s_NotifiedLock);
        l_Notified.swap(s_Notified);
    }

    for (std::string& it_Text : l_Notified)
    {
        Add(Kind::Success, std::move(it_Text));
    }
}

// One shown already is counted again and stays its time anew. With the stack full, it is counted in the toast that tells how many more there were
void Toasts::Add(Kind kind, std::string text)
{
    text = Shorten(text);
    if (const auto a_Same = std::ranges::find_if(m_Toasts, [kind, &text](const Toast& toast) { return !toast.Overflow && toast.Type == kind && toast.Text == text; }); a_Same != m_Toasts.end())
    {
        ++a_Same->Count;
        a_Same->Age = 0.0f;

        return;
    }

    if (static_cast<std::size_t>(std::ranges::count(m_Toasts, false, &Toast::Overflow)) < c_Shown)
    {
        m_Toasts.push_back({ .ID = m_NextID++, .Type = kind, .Text = std::move(text) });

        return;
    }

    auto a_Overflow = std::ranges::find(m_Toasts, true, &Toast::Overflow);
    if (a_Overflow == m_Toasts.end())
    {
        m_Toasts.push_back({ .ID = m_NextID++, .Type = kind, .Count = 0, .Overflow = true });
        a_Overflow = m_Toasts.end() - 1;
    }

    // As grave as the gravest it counts
    ++a_Overflow->Count;
    a_Overflow->Type = std::max(a_Overflow->Type, kind);
    a_Overflow->Age = 0.0f;
    a_Overflow->Text = std::format("{} more in the Console", a_Overflow->Count);
}

void Toasts::Draw()
{
    ReadLog();

    constexpr std::array<Trinity::ThemeColor, 3> c_Colors{ Trinity::ThemeColor::Success, Trinity::ThemeColor::Warning, Trinity::ThemeColor::Error };
    constexpr std::array<const char*, 3> c_Icons{ Trinity::Icons::c_CheckCircle, Trinity::Icons::c_Warning, Trinity::Icons::c_TimesCircle };
    const ImGuiViewport& l_Viewport = *ImGui::GetMainViewport();
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_Margin = ImGui::GetFontSize();
    const float l_Edge = std::max(1.0f, ImGui::GetFontSize() * c_EdgeInFonts);
    const float l_Delta = ImGui::GetIO().DeltaTime;
    float l_Bottom = l_Viewport.WorkPos.y + l_Viewport.WorkSize.y - l_Margin;
    bool l_ShowConsole = false;
    bool l_AnyHovered = false;

    // From the newest up, each above the one before it
    for (std::size_t it_Index = m_Toasts.size(); it_Index-- > 0;)
    {
        Toast& l_Toast = m_Toasts[it_Index];
        const std::size_t l_Kind = static_cast<std::size_t>(l_Toast.Type);
        const float l_Left = c_Lives[l_Kind] - l_Toast.Age;
        const ImVec4& l_Color = Trinity::Theme::GetColor(c_Colors[l_Kind]);

        // Kept in the main window, where a panel torn out of it would otherwise take them with it
        ImGui::SetNextWindowViewport(l_Viewport.ID);
        ImGui::SetNextWindowPos(ImVec2(l_Viewport.WorkPos.x + l_Viewport.WorkSize.x - l_Margin, l_Bottom), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * c_WidthInFonts, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, l_Style.Alpha * std::clamp(l_Left / c_FadeSeconds, 0.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(l_Style.WindowPadding.x + l_Edge, l_Style.WindowPadding.y));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
        bool l_Hovered = false;
        if (ImGui::Begin(std::format("##Toast{}", l_Toast.ID).c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking))
        {
            // Over whatever panel was clicked last, which would otherwise come in front of it, though under a menu or a popup that is open
            if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            {
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            }
            const ImVec2 l_Min = ImGui::GetWindowPos();
            const ImVec2 l_Size = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddRectFilled(l_Min, ImVec2(l_Min.x + l_Edge, l_Min.y + l_Size.y), ImGui::GetColorU32(l_Color), l_Style.WindowRounding, ImDrawFlags_RoundCornersLeft);

            ImGui::TextColored(l_Color, "%s", c_Icons[l_Kind]);
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(l_Toast.Count > 1 && !l_Toast.Overflow ? std::format("{} (x{})", l_Toast.Text, l_Toast.Count).c_str() : l_Toast.Text.c_str());
            ImGui::PopTextWrapPos();

            // A click lets it go, and for a warning or an error leads to the Console first
            l_Hovered = ImGui::IsWindowHovered();
            if (l_Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                l_ShowConsole = l_ShowConsole || l_Toast.Type != Kind::Success;
                l_Toast.Age = c_Lives[l_Kind];
            }
            else if (l_Hovered && l_Toast.Type != Kind::Success)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }

            l_Bottom -= l_Size.y + l_Style.ItemSpacing.y;
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        l_AnyHovered = l_AnyHovered || l_Hovered;
        if (!m_Hovered && !l_Hovered)
        {
            l_Toast.Age += l_Delta;
        }
    }

    m_Hovered = l_AnyHovered;
    std::erase_if(m_Toasts, [](const Toast& toast) { return toast.Age >= c_Lives[static_cast<std::size_t>(toast.Type)]; });
    if (l_ShowConsole && m_ShowConsole)
    {
        m_ShowConsole();
    }
}