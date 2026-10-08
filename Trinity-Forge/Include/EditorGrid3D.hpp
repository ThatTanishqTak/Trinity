#pragma once

#include <Trinity.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// The ground grid the Viewport shows in 3D, drawn as the scene's overlay so the scene's depth hides it
class EditorGrid3D
{
public:
    EditorGrid3D() = default;
    ~EditorGrid3D();

    EditorGrid3D(const EditorGrid3D&) = delete;
    EditorGrid3D& operator=(const EditorGrid3D&) = delete;

    void Draw(Trinity::RHI::CommandList& commands, const Trinity::RenderView& view, Trinity::RHI::Format colorFormat, std::uint32_t sampleCount);

    [[nodiscard]] static float GetSpacing(const glm::vec3& eye);

private:
    struct PipelineEntry
    {
        Trinity::RHI::Format Format = Trinity::RHI::Format::Unknown;
        std::uint32_t SampleCount = 1;
        Trinity::RHI::PipelineHandle Pipeline;
    };

    [[nodiscard]] Trinity::RHI::PipelineHandle GetPipeline(Trinity::RHI::Format colorFormat, std::uint32_t sampleCount);

    std::vector<PipelineEntry> m_Pipelines;
    bool m_Missing = false;
};