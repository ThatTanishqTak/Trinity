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

    // A world-space UI document a quad shows: the document, by the number its source gives it, and the size of its texture in pixels
    struct WorldUIDocument
    {
        std::uint32_t Document = 0;
        glm::uvec2 Size{ 0 };
    };

    // What draws a scene's world-space UI documents, as a game's UI does: each into a texture of its own size, once a frame, before any view draws, so every view of the scene shows the same image on its quad. The Renderer adds its passes first in each frame's graph, and each pass that can show a quad, the views' scene passes and Renderer3D's blended pass, reads every texture they draw, which the graph's pool keeps from one frame to the next. Its textures hold sRGB-encoded colours already multiplied by their alpha, as a view's display target does. Asked about on the main thread alone
    class TRINITY_API WorldUISource
    {
    public:
        virtual ~WorldUISource() = default;

        // The entity's document in the scene, when it draws something this frame. From the source's update until the frame's graph has run
        [[nodiscard]] virtual std::optional<WorldUIDocument> FindWorldDocument(const Scene& scene, UUID entity) const = 0;
        // A pass for each document that draws, into a texture from the graph's pool
        virtual void AddWorldPasses(FrameGraph& graph) = 0;
        // Every texture those passes draw into, which each pass that may show one reads
        [[nodiscard]] virtual std::span<const FrameGraphTexture> GetWorldTextures() const = 0;
        // A document's texture's bindless index, once its pass has run as the graph executes, and RHI::c_NoBindlessIndex before then or for a document with no pass
        [[nodiscard]] virtual std::uint32_t GetWorldTextureIndex(std::uint32_t document) const = 0;
    };
}