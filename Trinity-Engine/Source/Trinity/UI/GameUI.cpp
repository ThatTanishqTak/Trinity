#include "Trinity/UI/GameUI.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Application.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/UI/GameUIFileInterface.hpp"
#include "Trinity/UI/GameUIRenderInterface.hpp"
#include "Trinity/UI/GameUISystemInterface.hpp"
#include "Trinity/UI/UIAssets.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FontEngineInterface.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <tuple>
#include <utility>

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

        Rml::String GetContextName(UIDocumentID document)
        {
            return std::format("Document {}", document);
        }

        bool HasExtension(std::string_view path, std::string_view extension)
        {
            return path.size() >= extension.size() && std::ranges::equal(path.substr(path.size() - extension.size()), extension, [](char left, char right) { return (left >= 'A' && left <= 'Z' ? left - 'A' + 'a' : left) == right; });
        }

        // RmlUi's own font engine, but for a face it has already loaded from the same file as the same family, style and weight, which it would load again, and add again, each time a stylesheet naming it in an @font-face is read, as a reload does. Set once RmlUi has made and started its own, which it ends and frees as it shuts down
        class GameUIFontEngine final : public Rml::FontEngineInterface
        {
        public:
            void Wrap(Rml::FontEngineInterface* engine)
            {
                m_Engine = engine;
            }

            void Initialize() override
            {
                m_Engine->Initialize();
            }

            void Shutdown() override
            {
                m_Engine->Shutdown();
                m_Loaded.clear();
            }

            bool LoadFontFace(const Rml::String& file_name, int face_index, bool fallback_face, Rml::Style::FontWeight weight) override
            {
                return Load(Key{ file_name, face_index,{}, Rml::Style::FontStyle::Normal, weight, fallback_face, false }, [&] { return m_Engine->LoadFontFace(file_name, face_index, fallback_face, weight); });
            }

            bool LoadFontFace(const Rml::String& file_name, int face_index, const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback_face) override
            {
                return Load(Key{ file_name, face_index, family, style, weight, fallback_face, true }, [&] { return m_Engine->LoadFontFace(file_name, face_index, family, style, weight, fallback_face); });
            }

            bool LoadFontFace(Rml::Span<const Rml::byte> data, int face_index, const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback_face) override
            {
                return m_Engine->LoadFontFace(data, face_index, family, style, weight, fallback_face);
            }

            Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, int size) override
            {
                return m_Engine->GetFontFaceHandle(family, style, weight, size);
            }

            Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle, const Rml::FontEffectList& font_effects) override
            {
                return m_Engine->PrepareFontEffects(handle, font_effects);
            }

            const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override
            {
                return m_Engine->GetFontMetrics(handle);
            }

            int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& text_shaping_context, Rml::Character prior_character) override
            {
                return m_Engine->GetStringWidth(handle, string, text_shaping_context, prior_character);
            }

            int GenerateString(Rml::RenderManager& render_manager, Rml::FontFaceHandle face_handle, Rml::FontEffectsHandle font_effects_handle, Rml::StringView string, Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity, const Rml::TextShapingContext& text_shaping_context, Rml::TexturedMeshList& mesh_list) override
            {
                return m_Engine->GenerateString(render_manager, face_handle, font_effects_handle, string, position, colour, opacity, text_shaping_context, mesh_list);
            }

            int GetVersion(Rml::FontFaceHandle handle) override
            {
                return m_Engine->GetVersion(handle);
            }

            void ReleaseFontResources() override
            {
                m_Engine->ReleaseFontResources();
            }

        private:
            struct Key
            {
                Rml::String File;
                int FaceIndex = 0;
                Rml::String Family;
                Rml::Style::FontStyle Style = Rml::Style::FontStyle::Normal;
                Rml::Style::FontWeight Weight = Rml::Style::FontWeight::Auto;
                bool Fallback = false;
                bool Named = false;

                [[nodiscard]] bool operator==(const Key&) const = default;
            };

            // A face that failed is tried again the next time, as the file may have been fixed
            template<typename LoadFunction>
            bool Load(Key key, LoadFunction&& load)
            {
                if (std::ranges::find(m_Loaded, key) != m_Loaded.end())
                {
                    return true;
                }

                const bool l_Loaded = load();
                if (l_Loaded)
                {
                    m_Loaded.push_back(std::move(key));
                }

                return l_Loaded;
            }

            Rml::FontEngineInterface* m_Engine = nullptr;
            GameUIVector<Key> m_Loaded;
        };
    }

    // RmlUi keeps the interfaces until it shuts down, so they live as long as the UI does. Each document has a context of its own and keeps the list it recorded until it records again, which its view's overlay pass plays back with the view's other documents', in their sort order
    struct GameUI::State final : ViewOverlay
    {
        struct Document
        {
            UIDocumentID ID = c_NoDocument;
            ViewID View = Renderer::c_MainView;
            Rml::Context* Context = nullptr;
            Rml::ElementDocument* Rml = nullptr;
            Rml::String Path;
            GameUIDrawList DrawList;
            UIPanelSettings Panel;
            float Scale = 1.0f;
            std::int32_t SortOrder = 0;
            // What breaks a tie of sort orders: the order loaded, or the hierarchy's order for a scene's
            std::uint64_t Sequence = 0;
            bool Visible = true;
        };

        // A UIDocument's document, by its entity, kept while ShowScene sees it. One whose asset could not be loaded keeps no document, and is not tried again until it names another
        struct SceneDocument
        {
            ViewID View = Renderer::c_MainView;
            UUID Entity;
            UUID Asset;
            UIDocumentID Document = c_NoDocument;
            bool Shown = false;
        };

        State(RHI::Device& device, RHI::Format displayFormat) : Rendering(device, displayFormat, Renderer::c_OverlayStencilFormat)
        {

        }

        GameUISystemInterface System;
        GameUIFileInterface Files;
        GameUIRenderInterface Rendering;
        GameUIFontEngine Fonts;
        // In drawing order once Sorted, the views' documents together
        GameUIVector<Document> Documents;
        GameUIVector<SceneDocument> SceneDocuments;
        GameUIVector<std::int32_t> SortOrders;
        GameUIVector<std::pair<UUID, std::uint64_t>> HierarchyRanks;
        UIDocumentID NextDocument = c_NoDocument + 1;
        std::uint64_t NextSequence = 0;
        std::uint32_t Commands = 0;
        std::uint32_t DocumentReloads = 0;
        std::uint32_t StyleSheetReloads = 0;
        bool Started = false;
        bool Sorted = true;

        [[nodiscard]] Document* FindDocument(UIDocumentID document)
        {
            const auto a_Document = std::ranges::find(Documents, document, &Document::ID);

            return a_Document != Documents.end() ? &*a_Document : nullptr;
        }

        [[nodiscard]] const Document* FindDocument(UIDocumentID document) const
        {
            const auto a_Document = std::ranges::find(Documents, document, &Document::ID);

            return a_Document != Documents.end() ? &*a_Document : nullptr;
        }

        // Removing the context frees it and its document at once. What they drew is released as the next frame begins
        void Remove(UIDocumentID document)
        {
            const auto a_Document = std::ranges::find(Documents, document, &Document::ID);
            if (a_Document == Documents.end())
            {
                return;
            }

            Rml::RemoveContext(GetContextName(document));
            Documents.erase(a_Document);
        }

        // The whole key, so the order never depends on how the sort goes, and sorting moves the documents without allocating
        void Sort()
        {
            if (!std::exchange(Sorted, true))
            {
                std::ranges::sort(Documents, [](const Document& left, const Document& right) { return std::tie(left.SortOrder, left.Sequence, left.ID) < std::tie(right.SortOrder, right.Sequence, right.ID); });
            }
        }

        void RecordUploads(RHI::CommandList& commands) override
        {
            Rendering.RecordUploads(commands);
        }

        [[nodiscard]] Needs GetNeeds(ViewID view) const override
        {
            if (!Rendering.CanDraw())
            {
                return Needs::Nothing;
            }

            Needs l_Needs = Needs::Nothing;
            for (const Document& it_Document : Documents)
            {
                if (it_Document.View != view || it_Document.DrawList.Commands.empty())
                {
                    continue;
                }

                if (it_Document.DrawList.ClipMask && Rendering.CanClip())
                {
                    return Needs::ColorAndStencil;
                }

                l_Needs = Needs::Color;
            }

            return l_Needs;
        }

        void Draw(RHI::CommandList& commands, ViewID view, std::uint32_t width, std::uint32_t height, bool stencil) override
        {
            for (const Document& it_Document : Documents)
            {
                if (it_Document.View == view && !it_Document.DrawList.Commands.empty())
                {
                    Rendering.Draw(commands, it_Document.DrawList, width, height, stencil);
                }
            }
        }
    };

    GameUI::GameUI(Renderer& renderer, RHI::Device& device) : m_Renderer(renderer)
    {
        TR_CORE_ASSERT(!s_Running, "Only one GameUI may exist.");
        s_Running = true;

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        m_State = CreateScope<State>(device, m_Renderer.GetDisplayFormat());
        Rml::SetSystemInterface(&m_State->System);
        Rml::SetFileInterface(&m_State->Files);
        Rml::SetRenderInterface(&m_State->Rendering);

        m_State->Started = Rml::Initialise();
        if (!m_State->Started)
        {
            TR_CORE_ERROR("GameUI: RmlUi could not start, so no game UI is shown");

            return;
        }

        // RmlUi made and started its font engine as it started, and nothing has asked it for a face yet
        m_State->Fonts.Wrap(Rml::GetFontEngineInterface());
        Rml::SetFontEngineInterface(&m_State->Fonts);

        m_Renderer.SetViewOverlay(m_State.get());
        TR_CORE_INFO("GameUI: RmlUi {} started", Rml::GetVersion());
    }

    // RmlUi ends every context with its documents, and releases what it made, before the interfaces go. What it still holds then is in its own statics, kept until the process ends, so it is counted as untagged, as every other static's memory is
    GameUI::~GameUI()
    {
        m_Renderer.SetViewOverlay(nullptr);
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

    // The context takes the view's size as it is now, and its scale at the next Update
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
        const UIDocumentID l_ID = m_State->NextDocument++;
        const Rml::Vector2i l_Size(static_cast<int>(std::max(m_Renderer.GetSceneWidth(view), 1u)), static_cast<int>(std::max(m_Renderer.GetSceneHeight(view), 1u)));
        Rml::Context* l_Context = Rml::CreateContext(GetContextName(l_ID), l_Size);
        if (l_Context == nullptr)
        {
            TR_CORE_ERROR("GameUI: {} could not have a context, so it is not loaded", path);

            return c_NoDocument;
        }

        Rml::ElementDocument* l_Document = l_Context->LoadDocument(Rml::String(path));
        if (l_Document == nullptr)
        {
            Rml::RemoveContext(GetContextName(l_ID));
            TR_CORE_ERROR("GameUI: {} could not be loaded", path);

            return c_NoDocument;
        }

        l_Document->Show();

        State::Document& l_Entry = m_State->Documents.emplace_back();
        l_Entry.ID = l_ID;
        l_Entry.View = view;
        l_Entry.Context = l_Context;
        l_Entry.Rml = l_Document;
        l_Entry.Path = Rml::String(path);
        l_Entry.Panel.ScaleMode = UIScaleMode::ConstantPixelSize;
        l_Entry.Sequence = m_State->NextSequence++;
        m_State->Sorted = false;

        return l_ID;
    }

    void GameUI::UnloadDocument(UIDocumentID document)
    {
        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        m_State->Remove(document);
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

    void GameUI::SetSortOrder(UIDocumentID document, std::int32_t order)
    {
        State::Document* l_Document = m_State->FindDocument(document);
        if (l_Document != nullptr && l_Document->SortOrder != order)
        {
            l_Document->SortOrder = order;
            m_State->Sorted = false;
        }
    }

    std::int32_t GameUI::GetSortOrder(UIDocumentID document) const
    {
        const State::Document* l_Document = m_State->FindDocument(document);

        return l_Document != nullptr ? l_Document->SortOrder : 0;
    }

    void GameUI::SetPanelSettings(UIDocumentID document, const UIPanelSettings& settings)
    {
        if (State::Document* l_Document = m_State->FindDocument(document))
        {
            l_Document->Panel = settings;
        }
    }

    float GameUI::GetScale(UIDocumentID document) const
    {
        const State::Document* l_Document = m_State->FindDocument(document);

        return l_Document != nullptr ? l_Document->Scale : 1.0f;
    }

    // The hierarchy is walked only when two documents share a sort order, which is all its order decides
    void GameUI::ShowScene(Scene& scene, ViewID view)
    {
        if (!m_State->Started)
        {
            return;
        }

        TR_PROFILE_FUNCTION();

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        SceneRegistry& l_Registry = scene.GetRegistry();
        const auto l_Components = l_Registry.view<UIDocumentComponent, IDComponent>();
        GameUIVector<std::int32_t>& l_Orders = m_State->SortOrders;
        GameUIVector<std::pair<UUID, std::uint64_t>>& l_Ranks = m_State->HierarchyRanks;
        l_Orders.clear();
        l_Ranks.clear();
        for (const auto [it_Entity, it_Component, it_ID] : l_Components.each())
        {
            l_Orders.push_back(it_Component.SortOrder);
        }

        std::ranges::sort(l_Orders);
        if (std::ranges::adjacent_find(l_Orders) != l_Orders.end())
        {
            std::uint64_t l_Rank = 0;
            for (Entity it_Walked = scene.GetFirstRoot(); it_Walked; it_Walked = scene.GetNextInHierarchyOrder(it_Walked))
            {
                if (it_Walked.Has<UIDocumentComponent>())
                {
                    l_Ranks.emplace_back(it_Walked.GetUUID(), l_Rank++);
                }
            }
        }

        const AssetRegistry* l_Assets = AssetManager::GetRegistry();
        for (const auto [it_Entity, it_Component, it_ID] : l_Components.each())
        {
            const UUID l_EntityID = it_ID.ID;
            auto a_Entry = std::ranges::find_if(m_State->SceneDocuments, [&](const State::SceneDocument& entry) { return entry.View == view && entry.Entity == l_EntityID; });
            if (a_Entry == m_State->SceneDocuments.end())
            {
                State::SceneDocument& l_New = m_State->SceneDocuments.emplace_back();
                l_New.View = view;
                l_New.Entity = l_EntityID;
                a_Entry = m_State->SceneDocuments.end() - 1;
            }

            a_Entry->Shown = true;
            if (a_Entry->Asset != it_Component.Document || (a_Entry->Document != c_NoDocument && !HasDocument(a_Entry->Document)))
            {
                m_State->Remove(std::exchange(a_Entry->Document, c_NoDocument));
                a_Entry->Asset = it_Component.Document;
                if (a_Entry->Asset)
                {
                    const AssetRecord* l_Record = l_Assets != nullptr ? l_Assets->Find(a_Entry->Asset) : nullptr;
                    if (l_Record == nullptr || l_Record->Importer != UIAssets::c_DocumentType)
                    {
                        TR_CORE_WARN("GameUI: entity {} names {}, which {}, so its UIDocument shows nothing", l_EntityID, a_Entry->Asset, l_Record == nullptr ? "is not in the project" : "is not a UI document");
                    }
                    else
                    {
                        a_Entry->Document = LoadDocument(l_Record->Path, view);
                    }
                }
            }

            State::Document* l_Document = m_State->FindDocument(a_Entry->Document);
            if (l_Document == nullptr)
            {
                continue;
            }

            SetVisible(l_Document->ID, it_Component.Visible);
            SetSortOrder(l_Document->ID, it_Component.SortOrder);
            l_Document->Panel = it_Component.Panel;
            const auto a_Rank = std::ranges::find(l_Ranks, l_EntityID, &std::pair<UUID, std::uint64_t>::first);
            const std::uint64_t l_Sequence = a_Rank != l_Ranks.end() ? a_Rank->second : 0;
            if (l_Document->Sequence != l_Sequence)
            {
                l_Document->Sequence = l_Sequence;
                m_State->Sorted = false;
            }
        }
    }

    void GameUI::ForgetScene(ViewID view)
    {
        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        std::erase_if(m_State->SceneDocuments, [this, view](const State::SceneDocument& entry)
        {
            if (entry.View != view)
            {
                return false;
            }

            m_State->Remove(entry.Document);

            return true;
        });
    }

    UIDocumentID GameUI::FindSceneDocument(UUID entity, ViewID view) const
    {
        const auto a_Entry = std::ranges::find_if(m_State->SceneDocuments, [&](const State::SceneDocument& entry) { return entry.View == view && entry.Entity == entity; });

        return a_Entry != m_State->SceneDocuments.end() ? a_Entry->Document : c_NoDocument;
    }

    void GameUI::ReloadFiles(std::span<const std::string> paths)
    {
        const bool l_Documents = std::ranges::any_of(paths, [](const std::string& path) { return HasExtension(path, ".rml"); });
        if (l_Documents || std::ranges::any_of(paths, [](const std::string& path) { return HasExtension(path, ".rcss"); }))
        {
            Reload(l_Documents);
        }
    }

    void GameUI::ReloadAll()
    {
        Reload(true);
    }

    // A document that cannot be read again keeps what it had, with RmlUi's reasons logged
    void GameUI::Reload(bool documents)
    {
        if (!m_State->Started || m_State->Documents.empty())
        {
            return;
        }

        TR_PROFILE_FUNCTION();

        // The caches go first, so each document reads its stylesheets and templates from their files
        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();
        std::uint32_t l_Reloaded = 0;
        for (State::Document& it_Document : m_State->Documents)
        {
            if (!documents)
            {
                it_Document.Rml->ReloadStyleSheet();
                ++m_State->StyleSheetReloads;
                ++l_Reloaded;

                continue;
            }

            Rml::ElementDocument* l_Document = it_Document.Context->LoadDocument(it_Document.Path);
            if (l_Document == nullptr)
            {
                TR_CORE_ERROR("GameUI: {} could not be read again, so it is shown as it was", it_Document.Path);

                continue;
            }

            it_Document.Rml->Close();
            it_Document.Rml = l_Document;
            if (it_Document.Visible)
            {
                l_Document->Show();
            }

            ++m_State->DocumentReloads;
            ++l_Reloaded;
        }

        TR_CORE_INFO("GameUI: {} document(s) read their {} again", l_Reloaded, documents ? "documents and stylesheets" : "stylesheets");
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

    // A view with no targets yet, or none at all, draws nothing, and its documents' lists are left empty. The scene documents ShowScene did not see this frame go first
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

        std::erase_if(m_State->SceneDocuments, [this](State::SceneDocument& entry)
        {
            if (std::exchange(entry.Shown, false))
            {
                return false;
            }

            m_State->Remove(entry.Document);

            return true;
        });

        std::uint32_t l_Lost = 0;
        for (std::size_t it_Index = m_State->Documents.size(); it_Index-- > 0;)
        {
            const State::Document& l_Document = m_State->Documents[it_Index];
            if (!m_Renderer.HasView(l_Document.View))
            {
                TR_CORE_WARN("GameUI: view {} was destroyed, so {} is unloaded", l_Document.View, l_Document.Path);
                m_State->Remove(l_Document.ID);
                ++l_Lost;
            }
        }

        if (l_Lost != 0)
        {
            std::erase_if(m_State->SceneDocuments, [this](const State::SceneDocument& entry) { return entry.Document != c_NoDocument && !HasDocument(entry.Document); });
        }

        m_State->Sort();
        const float l_DpiScale = Application::Get().GetWindow().GetDpiScale();
        m_State->Commands = 0;
        for (State::Document& it_Document : m_State->Documents)
        {
            const std::uint32_t l_Width = m_Renderer.GetSceneWidth(it_Document.View);
            const std::uint32_t l_Height = m_Renderer.GetSceneHeight(it_Document.View);
            if (l_Width == 0 || l_Height == 0)
            {
                it_Document.DrawList.Clear();

                continue;
            }

            const Rml::Vector2i l_Size(static_cast<int>(l_Width), static_cast<int>(l_Height));
            if (it_Document.Context->GetDimensions() != l_Size)
            {
                it_Document.Context->SetDimensions(l_Size);
            }

            it_Document.Scale = it_Document.Panel.GetScale(l_Width, l_Height, l_DpiScale);
            it_Document.Context->SetDensityIndependentPixelRatio(it_Document.Scale);
            it_Document.Context->Update();
            m_State->Rendering.Begin(it_Document.DrawList);
            it_Document.Context->Render();
            m_State->Rendering.End();
            m_State->Commands += static_cast<std::uint32_t>(it_Document.DrawList.Commands.size());
        }
    }

    GameUIStatistics GameUI::GetStatistics() const
    {
        const GameUIRenderInterface::Statistics l_Rendering = m_State->Rendering.GetStatistics();

        GameUIStatistics l_Statistics;
        l_Statistics.Contexts = static_cast<std::uint32_t>(Rml::GetNumContexts());
        l_Statistics.Documents = static_cast<std::uint32_t>(m_State->Documents.size());
        l_Statistics.SceneDocuments = static_cast<std::uint32_t>(std::ranges::count_if(m_State->SceneDocuments, [](const State::SceneDocument& entry) { return entry.Document != c_NoDocument; }));
        l_Statistics.Geometries = l_Rendering.Geometries;
        l_Statistics.Textures = l_Rendering.Textures;
        l_Statistics.GeometryBytes = l_Rendering.GeometryBytes;
        l_Statistics.TextureBytes = l_Rendering.TextureBytes;
        l_Statistics.Commands = m_State->Commands;
        l_Statistics.DocumentReloads = m_State->DocumentReloads;
        l_Statistics.StyleSheetReloads = m_State->StyleSheetReloads;

        return l_Statistics;
    }
}