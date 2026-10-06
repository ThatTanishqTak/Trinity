#pragma once

#include "EditorCamera.hpp"

#include <Trinity.hpp>

#include <glm/glm.hpp>

class EditorGrid
{
public:
    EditorGrid();
    ~EditorGrid();

    EditorGrid(const EditorGrid&) = delete;
    EditorGrid& operator=(const EditorGrid&) = delete;

    void Draw(Trinity::RHI::CommandList& commands, const EditorCamera& camera, glm::vec2 viewportSize);

    [[nodiscard]] static float GetSpacing(const EditorCamera& camera, glm::vec2 viewportSize);

private:
    Trinity::RHI::PipelineHandle m_Pipeline;
};