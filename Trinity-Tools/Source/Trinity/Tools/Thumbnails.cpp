#include "Trinity/Tools/Thumbnails.hpp"

#include <Trinity.hpp>

#include <imgui_internal.h>
#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace
{
    // A thumbnail is a small picture, so a file larger than this a side is not decoded
    constexpr int c_MaximumSide = 4096;
    // A picture is decoded where it is drawn, so only this many are in one frame
    constexpr std::uint32_t c_LoadsPerFrame = 2;
    // The icon's height against the box it stands in for a picture in
    constexpr float c_IconScale = 0.35f;

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    struct StbDeleter
    {
        void operator()(stbi_uc* pixels) const
        {
            stbi_image_free(pixels);
        }
    };
}

namespace Trinity::Tools
{
    // Layers detach last pushed first, so ImGui has destroyed every texture by now. Only memory is left
    Thumbnails::~Thumbnails()
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }

        for (Entry& it_Entry : m_Entries)
        {
            if (it_Entry.Texture)
            {
                m_Released.push_back(std::move(it_Entry.Texture));
            }
        }

        for (const std::unique_ptr<ImTextureData>& it_Texture : m_Released)
        {
            ImGui::UnregisterUserTexture(it_Texture.get());
            ImGui::GetPlatformIO().Textures.find_erase(it_Texture.get());
        }
    }

    std::unique_ptr<ImTextureData> Thumbnails::Load(const std::filesystem::path& file)
    {
        std::ifstream l_Stream(file, std::ios::binary);
        const std::string l_Bytes((std::istreambuf_iterator<char>(l_Stream)), std::istreambuf_iterator<char>());
        int l_Width = 0;
        int l_Height = 0;
        int l_Channels = 0;
        const std::unique_ptr<stbi_uc, StbDeleter> l_Pixels(l_Bytes.empty() ? nullptr : stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(l_Bytes.data()), static_cast<int>(l_Bytes.size()), &l_Width, &l_Height, &l_Channels, 4));
        if (!l_Pixels || l_Width > c_MaximumSide || l_Height > c_MaximumSide)
        {
            const std::string l_Why = l_Bytes.empty() ? std::string("it could not be read") : !l_Pixels ? std::format("it is not a PNG it can read: {}", stbi_failure_reason()) : std::format("it is {}x{}, larger than {} a side", l_Width, l_Height, c_MaximumSide);
            TR_WARN("The thumbnail {} is not shown, as {}", ToUtf8(file), l_Why);

            return nullptr;
        }

        std::unique_ptr<ImTextureData> l_Texture = std::make_unique<ImTextureData>();
        l_Texture->Create(ImTextureFormat_RGBA32, l_Width, l_Height);
        std::memcpy(l_Texture->GetPixels(), l_Pixels.get(), static_cast<std::size_t>(l_Width) * static_cast<std::size_t>(l_Height) * 4);
        ImGui::RegisterUserTexture(l_Texture.get());

        return l_Texture;
    }

    void Thumbnails::Draw(const ImTextureData* texture, const char* icon, const ImVec2& minimum, const ImVec2& maximum)
    {
        ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
        const float l_Rounding = ImGui::GetStyle().FrameRounding;
        l_DrawList.AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGuiCol_FrameBg), l_Rounding);
        const float l_Width = maximum.x - minimum.x;
        const float l_Height = maximum.y - minimum.y;
        // Once ImGui's renderer has made the texture, a frame after it is read, so a failed one never draws
        if (texture != nullptr && texture->Status == ImTextureStatus_OK && texture->Width > 0 && texture->Height > 0)
        {
            const float l_Scale = std::min(l_Width / static_cast<float>(texture->Width), l_Height / static_cast<float>(texture->Height));
            const ImVec2 l_Size(static_cast<float>(texture->Width) * l_Scale, static_cast<float>(texture->Height) * l_Scale);
            const ImVec2 l_At(minimum.x + (l_Width - l_Size.x) * 0.5f, minimum.y + (l_Height - l_Size.y) * 0.5f);
            l_DrawList.AddImageRounded(ImTextureRef(texture->TexID), l_At, ImVec2(l_At.x + l_Size.x, l_At.y + l_Size.y), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, l_Rounding);

            return;
        }

        const float l_IconSize = l_Height * c_IconScale;
        const ImVec2 l_IconExtent = ImGui::CalcTextSize(icon);
        const float l_IconScale = l_IconSize / ImGui::GetFontSize();
        l_DrawList.AddText(nullptr, l_IconSize, ImVec2(minimum.x + (l_Width - l_IconExtent.x * l_IconScale) * 0.5f, minimum.y + (l_Height - l_IconExtent.y * l_IconScale) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), icon);
    }

    const ImTextureData* Thumbnails::Get(const std::filesystem::path& file)
    {
        auto a_Entry = std::ranges::find_if(m_Entries, [&file](const Entry& entry) { return entry.File == file; });
        if (a_Entry == m_Entries.end())
        {
            a_Entry = m_Entries.insert(m_Entries.end(), Entry{ file });
        }

        // A file as it was when last read keeps its texture. One written since, or gone, lets go of it
        if (!a_Entry->Checked && m_Loads < c_LoadsPerFrame)
        {
            std::error_code l_Error;
            const std::filesystem::file_time_type l_Written = std::filesystem::last_write_time(file, l_Error);
            const std::optional<std::filesystem::file_time_type> l_Time = l_Error ? std::nullopt : std::optional(l_Written);
            if (l_Time != a_Entry->Time)
            {
                Release(a_Entry->Texture);
                if (l_Time)
                {
                    a_Entry->Texture = Load(file);
                    ++m_Loads;
                }
            }

            a_Entry->Time = l_Time;
            a_Entry->Checked = true;
        }

        const ImTextureData* l_Texture = a_Entry->Texture.get();

        return l_Texture != nullptr && l_Texture->Status == ImTextureStatus_OK ? l_Texture : nullptr;
    }

    void Thumbnails::Recheck()
    {
        for (Entry& it_Entry : m_Entries)
        {
            it_Entry.Checked = false;
        }
    }

    void Thumbnails::Clear()
    {
        for (Entry& it_Entry : m_Entries)
        {
            Release(it_Entry.Texture);
        }

        m_Entries.clear();
    }

    // A thumbnail never changes once uploaded, so its pixels are let go of
    void Thumbnails::Update()
    {
        m_Loads = 0;
        for (const Entry& it_Entry : m_Entries)
        {
            if (it_Entry.Texture && it_Entry.Texture->Status == ImTextureStatus_OK && it_Entry.Texture->Pixels != nullptr)
            {
                it_Entry.Texture->DestroyPixels();
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

    // ImGui's renderer destroys it next frame, and Update lets go of what is left once it has
    void Thumbnails::Release(std::unique_ptr<ImTextureData>& texture)
    {
        if (!texture)
        {
            return;
        }

        texture->WantDestroyNextFrame = true;
        m_Released.push_back(std::move(texture));
    }
}