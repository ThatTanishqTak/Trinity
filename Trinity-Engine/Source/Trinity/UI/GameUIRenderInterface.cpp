#include "Trinity/UI/GameUIRenderInterface.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <cstring>

namespace Trinity
{
    // RmlUi has released everything it made by the time it shuts down, so what is left over is reported rather than freed, as the GameUI tag would show it anyway
    GameUIRenderInterface::~GameUIRenderInterface()
    {
        BeginFrame();
        if (m_Statistics.Geometries != 0 || m_Statistics.Textures != 0)
        {
            TR_CORE_WARN("GameUI: {} geometry and {} texture(s) were never released by RmlUi", m_Statistics.Geometries, m_Statistics.Textures);
        }
    }

    void GameUIRenderInterface::BeginFrame()
    {
        for (Geometry* it_Geometry : m_ReleasedGeometry)
        {
            Free(it_Geometry);
        }

        for (Texture* it_Texture : m_ReleasedTextures)
        {
            Free(it_Texture);
        }

        m_ReleasedGeometry.clear();
        m_ReleasedTextures.clear();
    }

    void GameUIRenderInterface::Begin(GameUIDrawList& list)
    {
        TR_CORE_ASSERT(m_List == nullptr, "A GameUI draw list is already being recorded.");

        list.Clear();
        m_List = &list;
    }

    void GameUIRenderInterface::End()
    {
        m_List = nullptr;
    }

    Rml::CompiledGeometryHandle GameUIRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
    {
        Geometry* l_Geometry = Memory::New<Geometry>(MemoryTag::GameUI);
        l_Geometry->Vertices.assign(vertices.begin(), vertices.end());
        l_Geometry->Indices.assign(indices.begin(), indices.end());

        ++m_Statistics.Geometries;
        m_Statistics.GeometryBytes += vertices.size() * sizeof(Rml::Vertex) + indices.size() * sizeof(int);

        return reinterpret_cast<Rml::CompiledGeometryHandle>(l_Geometry);
    }

    void GameUIRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::Geometry;
        l_Command.Geometry = geometry;
        l_Command.Texture = texture;
        l_Command.Translation = { translation.x, translation.y };
        Record(l_Command);
    }

    void GameUIRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        if (geometry != 0)
        {
            m_ReleasedGeometry.push_back(reinterpret_cast<Geometry*>(geometry));
        }
    }

    // Images in documents are not loaded, so each source is 0, which RmlUi reports once and draws nothing for
    Rml::TextureHandle GameUIRenderInterface::LoadTexture([[maybe_unused]] Rml::Vector2i& dimensions, [[maybe_unused]] const Rml::String& source)
    {
        return 0;
    }

    // Font atlases and the like, which RmlUi makes in premultiplied RGBA8
    Rml::TextureHandle GameUIRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
    {
        if (dimensions.x <= 0 || dimensions.y <= 0 || source.size() != static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4)
        {
            TR_CORE_ERROR("GameUI: a {}x{} texture of {} bytes is no RGBA8 image, so it is not made", dimensions.x, dimensions.y, source.size());

            return 0;
        }

        Texture* l_Texture = Memory::New<Texture>(MemoryTag::GameUI);
        l_Texture->Width = static_cast<std::uint32_t>(dimensions.x);
        l_Texture->Height = static_cast<std::uint32_t>(dimensions.y);
        l_Texture->Pixels.resize(source.size());
        std::memcpy(l_Texture->Pixels.data(), source.data(), source.size());

        ++m_Statistics.Textures;
        m_Statistics.TextureBytes += source.size();

        return reinterpret_cast<Rml::TextureHandle>(l_Texture);
    }

    void GameUIRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
    {
        if (texture != 0)
        {
            m_ReleasedTextures.push_back(reinterpret_cast<Texture*>(texture));
        }
    }

    void GameUIRenderInterface::EnableScissorRegion(bool enable)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::EnableScissor;
        l_Command.Enable = enable;
        Record(l_Command);
    }

    void GameUIRenderInterface::SetScissorRegion(Rml::Rectanglei region)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::SetScissor;
        l_Command.Scissor = { region.Left(), region.Top(), region.Width(), region.Height() };
        Record(l_Command);
    }

    void GameUIRenderInterface::EnableClipMask(bool enable)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::EnableClipMask;
        l_Command.Enable = enable;
        Record(l_Command);
    }

    void GameUIRenderInterface::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::RenderToClipMask;
        l_Command.Operation = operation;
        l_Command.Geometry = geometry;
        l_Command.Translation = { translation.x, translation.y };
        Record(l_Command);
    }

    void GameUIRenderInterface::SetTransform(const Rml::Matrix4f* transform)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::SetTransform;
        l_Command.Transform = GameUIDrawList::c_Identity;
        if (transform != nullptr && m_List != nullptr)
        {
            l_Command.Transform = static_cast<std::uint32_t>(m_List->Transforms.size());
            std::array<float, 16>& l_Transform = m_List->Transforms.emplace_back();
            std::copy_n(transform->data(), l_Transform.size(), l_Transform.begin());
        }

        Record(l_Command);
    }

    // RmlUi only draws as a context renders, which GameUI always records
    void GameUIRenderInterface::Record(const GameUIDrawList::Command& command)
    {
        TR_CORE_ASSERT(m_List != nullptr, "RmlUi drew outside a context's render.");

        if (m_List != nullptr)
        {
            m_List->Commands.push_back(command);
        }
    }

    void GameUIRenderInterface::Free(Geometry* geometry)
    {
        --m_Statistics.Geometries;
        m_Statistics.GeometryBytes -= geometry->Vertices.size() * sizeof(Rml::Vertex) + geometry->Indices.size() * sizeof(int);
        Memory::Delete(geometry);
    }

    void GameUIRenderInterface::Free(Texture* texture)
    {
        --m_Statistics.Textures;
        m_Statistics.TextureBytes -= texture->Pixels.size();
        Memory::Delete(texture);
    }
}