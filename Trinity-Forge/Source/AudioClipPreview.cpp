#include "AudioClipPreview.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    Trinity::AudioEngine* GetAudioEngine()
    {
        return Trinity::Application::Get().GetAudioEngine();
    }
}

AudioClipPreview::~AudioClipPreview()
{
    Stop();
}

void AudioClipPreview::Play(Trinity::UUID clip)
{
    Stop();
    m_Clip = Trinity::AssetRef<Trinity::AudioClipAsset>(clip);
    StartWhenLoaded();
}

void AudioClipPreview::Stop()
{
    if (m_Clip.GetID().IsValid())
    {
        if (Trinity::AudioEngine* l_Engine = GetAudioEngine())
        {
            l_Engine->StopPreview();
        }
    }

    m_Clip = {};
    m_Started = false;
}

void AudioClipPreview::Update(std::uint64_t importCount)
{
    ++m_Frame;
    if (importCount != m_ImportCount)
    {
        m_ImportCount = importCount;
        m_Headers.clear();
    }
    else
    {
        std::erase_if(m_Headers, [this](const auto& header) { return header.second.LastUsed + 1 < m_Frame; });
    }

    if (!m_Clip.GetID().IsValid())
    {
        return;
    }

    if (!m_Started)
    {
        StartWhenLoaded();

        return;
    }

    // The engine holds what it plays, so the clip is let go once it has ended
    const Trinity::AudioEngine* l_Engine = GetAudioEngine();
    if (l_Engine == nullptr || !l_Engine->IsPreviewPlaying())
    {
        m_Clip = {};
        m_Started = false;
    }
}

void AudioClipPreview::Clear()
{
    Stop();
    decltype(m_Headers)().swap(m_Headers);
}

bool AudioClipPreview::IsPlaying(Trinity::UUID clip) const
{
    return clip.IsValid() && m_Clip.GetID() == clip;
}

std::optional<float> AudioClipPreview::GetPlayhead(Trinity::UUID clip) const
{
    const Trinity::AudioClipAsset* l_Clip = m_Started && IsPlaying(clip) ? m_Clip.Get() : nullptr;
    const Trinity::AudioEngine* l_Engine = GetAudioEngine();
    if (l_Clip == nullptr || l_Engine == nullptr || l_Clip->GetHeader().Frames == 0)
    {
        return std::nullopt;
    }

    return std::clamp(static_cast<float>(static_cast<double>(l_Engine->GetPreviewCursor()) / static_cast<double>(l_Clip->GetHeader().Frames)), 0.0f, 1.0f);
}

const Trinity::AudioClipHeader* AudioClipPreview::FindHeader(Trinity::UUID clip)
{
    const auto [a_Found, a_Inserted] = m_Headers.try_emplace(clip);
    if (a_Inserted)
    {
        if (const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_File = Trinity::FileSystem::ReadFile(Trinity::GetCookedAudioClipPath(clip)))
        {
            if (Trinity::Expected<Trinity::AudioClipHeader, std::string> l_Header = Trinity::ReadAudioClipHeader(*l_File))
            {
                a_Found->second.Header = std::move(*l_Header);
            }
        }
    }

    a_Found->second.LastUsed = m_Frame;

    return a_Found->second.Header ? &*a_Found->second.Header : nullptr;
}

// A clip that fails to load is dropped, and the asset manager has said why
void AudioClipPreview::StartWhenLoaded()
{
    const Trinity::AssetState l_State = m_Clip.GetState();
    if (l_State == Trinity::AssetState::Failed)
    {
        m_Clip = {};

        return;
    }

    Trinity::AudioEngine* l_Engine = GetAudioEngine();
    if (l_State != Trinity::AssetState::Ready || l_Engine == nullptr)
    {
        return;
    }

    l_Engine->PlayPreview(*m_Clip.Get());
    m_Started = true;
}

// A pixel's line spans the columns under it, so a narrow thumbnail still shows every peak
void DrawAudioWaveform(ImDrawList& drawList, const Trinity::AudioClipHeader& header, ImVec2 min, ImVec2 max, bool lanes, std::optional<float> playhead)
{
    const std::size_t l_Columns = header.WaveformColumns;
    const std::size_t l_Channels = header.Channels;
    const int l_Pixels = static_cast<int>(std::floor(max.x - min.x));
    if (l_Columns == 0 || l_Channels == 0 || l_Pixels <= 0 || header.Waveform.size() < l_Columns * l_Channels * 2)
    {
        return;
    }

    const std::size_t l_Lanes = lanes ? l_Channels : 1;
    const float l_LaneHeight = (max.y - min.y) / static_cast<float>(l_Lanes);
    const ImU32 l_Color = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    for (std::size_t it_Lane = 0; it_Lane < l_Lanes; ++it_Lane)
    {
        const float l_Middle = min.y + l_LaneHeight * (static_cast<float>(it_Lane) + 0.5f);
        const float l_Half = l_LaneHeight * 0.5f;
        const std::size_t l_FirstChannel = lanes ? it_Lane : 0;
        const std::size_t l_EndChannel = lanes ? it_Lane + 1 : l_Channels;
        for (int it_Pixel = 0; it_Pixel < l_Pixels; ++it_Pixel)
        {
            const std::size_t l_Pixel = static_cast<std::size_t>(it_Pixel);
            const std::size_t l_Count = static_cast<std::size_t>(l_Pixels);
            const std::size_t l_First = l_Pixel * l_Columns / l_Count;
            const std::size_t l_End = std::max(l_First + 1, (l_Pixel + 1) * l_Columns / l_Count);
            int l_Low = 0;
            int l_High = 0;
            for (std::size_t it_Column = l_First; it_Column < l_End; ++it_Column)
            {
                for (std::size_t it_Channel = l_FirstChannel; it_Channel < l_EndChannel; ++it_Channel)
                {
                    const std::size_t l_Cell = (it_Column * l_Channels + it_Channel) * 2;
                    l_Low = std::min<int>(l_Low, header.Waveform[l_Cell]);
                    l_High = std::max<int>(l_High, header.Waveform[l_Cell + 1]);
                }
            }

            const float l_X = min.x + static_cast<float>(it_Pixel);
            const float l_Top = l_Middle - static_cast<float>(l_High) / 127.0f * l_Half;
            const float l_Bottom = l_Middle - static_cast<float>(l_Low) / 127.0f * l_Half;
            drawList.AddRectFilled(ImVec2(l_X, l_Top), ImVec2(l_X + 1.0f, std::max(l_Bottom, l_Top + 1.0f)), l_Color);
        }
    }

    if (playhead)
    {
        const float l_X = std::floor(min.x + std::clamp(*playhead, 0.0f, 1.0f) * (max.x - min.x));
        drawList.AddLine(ImVec2(l_X, min.y), ImVec2(l_X, max.y), ImGui::GetColorU32(ImGuiCol_Text));
    }
}