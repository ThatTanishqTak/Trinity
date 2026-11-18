#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Renderer/FrameGraph.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace Trinity
{
    class Scene;

    struct WorldUIDocument
    {
        std::uint32_t Document = 0;
        glm::uvec2 Size{ 0 };
    };

    // Each document draws into its own texture once a frame, before any view. Main thread only
    class TRINITY_API WorldUISource
    {
    public:
        virtual ~WorldUISource() = default;

        // Valid from the source's update until the frame's graph has run
        [[nodiscard]] virtual std::optional<WorldUIDocument> FindWorldDocument(const Scene& scene, UUID entity) const = 0;
        // A pass for each document that draws, into a texture from the graph's pool
        virtual void AddWorldPasses(FrameGraph& graph) = 0;
        // Every texture those passes draw into, which each pass that may show one reads
        [[nodiscard]] virtual std::span<const FrameGraphTexture> GetWorldTextures() const = 0;
        // RHI::c_NoBindlessIndex before its pass has run, or for a document with no pass
        [[nodiscard]] virtual std::uint32_t GetWorldTextureIndex(std::uint32_t document) const = 0;
    };
}