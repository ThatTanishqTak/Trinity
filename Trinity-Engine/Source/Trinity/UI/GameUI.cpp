#include "Trinity/UI/GameUI.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/UI/GameUIFileInterface.hpp"
#include "Trinity/UI/GameUIRenderInterface.hpp"
#include "Trinity/UI/GameUISystemInterface.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include <algorithm>
#include <chrono>
#include <format>
#include <string>

// RmlUi's font engine makes and ends its FreeType library through these, in place of FT_Init_FreeType and FT_Done_FreeType, which Vendor/CMakeLists.txt renames in that one source of RmlUi's. They take FreeType's own steps, with its memory counted under GameUI
extern "C"
{
    FT_Error TrinityRmlUi_InitFreeType(FT_Library* library);
    FT_Error TrinityRmlUi_DoneFreeType(FT_Library library);
}

namespace
{
    void* AllocateFreeType([[maybe_unused]] FT_Memory memory, long size)
    {
        return Trinity::Memory::TryAllocate(static_cast<std::size_t>(size), Trinity::MemoryTag::GameUI);
    }

    void FreeFreeType([[maybe_unused]] FT_Memory memory, void* block)
    {
        Trinity::Memory::Free(block);
    }

    void* ReallocateFreeType([[maybe_unused]] FT_Memory memory, [[maybe_unused]] long currentSize, long newSize, void* block)
    {
        return Trinity::Memory::Reallocate(block, static_cast<std::size_t>(newSize), Trinity::MemoryTag::GameUI);
    }

    FT_MemoryRec_ s_FreeTypeMemory{ nullptr, &AllocateFreeType, &FreeFreeType, &ReallocateFreeType };
}

FT_Error TrinityRmlUi_InitFreeType(FT_Library* library)
{
    const FT_Error l_Error = FT_New_Library(&s_FreeTypeMemory, library);
    if (l_Error != 0)
    {
        return l_Error;
    }

    FT_Add_Default_Modules(*library);
    FT_Set_Default_Properties(*library);

    return 0;
}

FT_Error TrinityRmlUi_DoneFreeType(FT_Library library)
{
    return FT_Done_Library(library);
}

namespace Trinity
{
    namespace
    {
        bool s_Running = false;

        Rml::String GetContextName(ViewID view)
        {
            return std::format("View {}", view);
        }
    }

    // RmlUi keeps the interfaces until it shuts down, so they live as long as the UI does. Each context keeps the list it recorded until it records again
    struct GameUI::State
    {
        struct Context
        {
            ViewID View = Renderer::c_MainView;
            Rml::Context* Rml = nullptr;
            GameUIDrawList DrawList;
        };

        struct Document
        {
            UIDocumentID ID = c_NoDocument;
            ViewID View = Renderer::c_MainView;
            Rml::ElementDocument* Rml = nullptr;
            bool Visible = true;
        };

        GameUISystemInterface System;
        GameUIFileInterface Files;
        GameUIRenderInterface Rendering;
        GameUIVector<Context> Contexts;
        GameUIVector<Document> Documents;
        UIDocumentID NextDocument = c_NoDocument + 1;
        std::uint32_t Commands = 0;
        bool Started = false;

        [[nodiscard]] Document* FindDocument(UIDocumentID document)
        {
            const auto a_Document = std::ranges::find(Documents, document, &Document::ID);

            return a_Document != Documents.end() ? &*a_Document : nullptr;
        }
    };

    GameUI::GameUI(Renderer& renderer) : m_Renderer(renderer)
    {
        TR_CORE_ASSERT(!s_Running, "Only one GameUI may exist.");
        s_Running = true;

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        m_State = CreateScope<State>();
        Rml::SetSystemInterface(&m_State->System);
        Rml::SetFileInterface(&m_State->Files);
        Rml::SetRenderInterface(&m_State->Rendering);

        m_State->Started = Rml::Initialise();
        if (!m_State->Started)
        {
            TR_CORE_ERROR("GameUI: RmlUi could not start, so no game UI is shown");

            return;
        }

        TR_CORE_INFO("GameUI: RmlUi {} started", Rml::GetVersion());
    }

    // RmlUi ends every context with its documents, and releases what it made, before the interfaces go. What it still holds then is in its own statics, kept until the process ends, so it is counted as untagged, as every other static's memory is
    GameUI::~GameUI()
    {
        {
            const MemoryTagScope l_Scope(MemoryTag::GameUI);
            if (m_State->Started)
            {
                Rml::Shutdown();
            }

            m_State.reset();
        }

        const MemoryTagStats l_Statics = Memory::UntagScopedAllocations(MemoryTag::GameUI);
        if (l_Statics.LiveAllocations != 0)
        {
            TR_CORE_INFO("GameUI: {} in {} allocation(s) that RmlUi keeps in its statics until the process ends are counted as untagged", Memory::FormatBytes(l_Statics.CurrentBytes), l_Statics.LiveAllocations);
        }

        s_Running = false;
    }

    UIDocumentID GameUI::LoadDocument(std::string_view path, ViewID view)
    {
        if (!m_State->Started)
        {
            return c_NoDocument;
        }

        if (!m_Renderer.HasView(view))
        {
            TR_CORE_ERROR("GameUI: {} is not loaded, since view {} does not exist", path, view);

            return c_NoDocument;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        auto a_Context = std::ranges::find(m_State->Contexts, view, &State::Context::View);
        if (a_Context == m_State->Contexts.end())
        {
            const Rml::Vector2i l_Size(static_cast<int>(std::max(m_Renderer.GetSceneWidth(view), 1u)), static_cast<int>(std::max(m_Renderer.GetSceneHeight(view), 1u)));
            Rml::Context* l_Context = Rml::CreateContext(GetContextName(view), l_Size);
            if (l_Context == nullptr)
            {
                TR_CORE_ERROR("GameUI: view {} could not have a context, so {} is not loaded", view, path);

                return c_NoDocument;
            }

            State::Context& l_Entry = m_State->Contexts.emplace_back();
            l_Entry.View = view;
            l_Entry.Rml = l_Context;
            a_Context = m_State->Contexts.end() - 1;
        }

        Rml::ElementDocument* l_Document = a_Context->Rml->LoadDocument(Rml::String(path));
        if (l_Document == nullptr)
        {
            TR_CORE_ERROR("GameUI: {} could not be loaded", path);

            return c_NoDocument;
        }

        l_Document->Show();

        State::Document& l_Entry = m_State->Documents.emplace_back();
        l_Entry.ID = m_State->NextDocument++;
        l_Entry.View = view;
        l_Entry.Rml = l_Document;

        return l_Entry.ID;
    }

    // RmlUi frees a closed document as its context next updates
    void GameUI::UnloadDocument(UIDocumentID document)
    {
        State::Document* l_Document = m_State->FindDocument(document);
        if (l_Document == nullptr)
        {
            return;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        l_Document->Rml->Close();
        std::erase_if(m_State->Documents, [document](const State::Document& entry) { return entry.ID == document; });
    }

    bool GameUI::HasDocument(UIDocumentID document) const
    {
        return m_State->FindDocument(document) != nullptr;
    }

    void GameUI::SetVisible(UIDocumentID document, bool visible)
    {
        State::Document* l_Document = m_State->FindDocument(document);
        if (l_Document == nullptr || l_Document->Visible == visible)
        {
            return;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        if (visible)
        {
            l_Document->Rml->Show();
        }
        else
        {
            l_Document->Rml->Hide();
        }

        l_Document->Visible = visible;
    }

    bool GameUI::IsVisible(UIDocumentID document) const
    {
        const State::Document* l_Document = m_State->FindDocument(document);

        return l_Document != nullptr && l_Document->Visible;
    }

    bool GameUI::LoadFontFace(std::string_view path, bool fallback)
    {
        if (!m_State->Started)
        {
            return false;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);

        return Rml::LoadFontFace(Rml::String(path), fallback);
    }

    // A view with no targets yet, or none at all, draws nothing, and its list is left empty
    void GameUI::Update(Timestep timestep)
    {
        if (!m_State->Started)
        {
            return;
        }

        TR_PROFILE_FUNCTION();

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        m_State->System.Advance(std::chrono::duration<double>(timestep.GetDuration()).count());
        m_State->Rendering.BeginFrame();

        std::erase_if(m_State->Contexts, [this](const State::Context& context)
            {
                if (m_Renderer.HasView(context.View))
                {
                    return false;
                }

                const std::size_t l_Documents = std::erase_if(m_State->Documents, [&context](const State::Document& document) { return document.View == context.View; });
                TR_CORE_WARN("GameUI: view {} was destroyed, so its {} document(s) are unloaded", context.View, l_Documents);
                Rml::RemoveContext(GetContextName(context.View));

                return true;
            });

        m_State->Commands = 0;
        for (State::Context& it_Context : m_State->Contexts)
        {
            const std::uint32_t l_Width = m_Renderer.GetSceneWidth(it_Context.View);
            const std::uint32_t l_Height = m_Renderer.GetSceneHeight(it_Context.View);
            if (l_Width == 0 || l_Height == 0)
            {
                it_Context.DrawList.Clear();

                continue;
            }

            const Rml::Vector2i l_Size(static_cast<int>(l_Width), static_cast<int>(l_Height));
            if (it_Context.Rml->GetDimensions() != l_Size)
            {
                it_Context.Rml->SetDimensions(l_Size);
            }

            it_Context.Rml->Update();
            m_State->Rendering.Begin(it_Context.DrawList);
            it_Context.Rml->Render();
            m_State->Rendering.End();
            m_State->Commands += static_cast<std::uint32_t>(it_Context.DrawList.Commands.size());
        }
    }

    GameUIStatistics GameUI::GetStatistics() const
    {
        const GameUIRenderInterface::Statistics l_Rendering = m_State->Rendering.GetStatistics();

        GameUIStatistics l_Statistics;
        l_Statistics.Contexts = static_cast<std::uint32_t>(m_State->Contexts.size());
        l_Statistics.Documents = static_cast<std::uint32_t>(m_State->Documents.size());
        l_Statistics.Geometries = l_Rendering.Geometries;
        l_Statistics.Textures = l_Rendering.Textures;
        l_Statistics.GeometryBytes = l_Rendering.GeometryBytes;
        l_Statistics.TextureBytes = l_Rendering.TextureBytes;
        l_Statistics.Commands = m_State->Commands;

        return l_Statistics;
    }
}