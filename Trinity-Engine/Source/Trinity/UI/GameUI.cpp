#include "Trinity/UI/GameUI.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Application.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Input/Input.hpp"
#include "Trinity/Input/InputActionData.hpp"
#include "Trinity/Input/InputActions.hpp"
#include "Trinity/Input/InputActionsRuntime.hpp"
#include "Trinity/Renderer/WorldUI.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/UI/GameUIFileInterface.hpp"
#include "Trinity/UI/GameUIInput.hpp"
#include "Trinity/UI/GameUIRenderInterface.hpp"
#include "Trinity/UI/GameUISystemInterface.hpp"
#include "Trinity/UI/UIAssets.hpp"
#include "Trinity/UI/UIWorldSettings.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/StyleSheetContainer.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <span>
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

        // Under every document's own, so the controls a player moves between take the focus as Unity's do, while a document's own rule for them wins
        constexpr const char* c_BaseStyleSheet = "button, input, select, textarea { tab-index: auto; }";
        // As Unity's UI input module: how far Navigate has to be pushed to move the focus, how long it waits before moving again while held, and how often it moves after that
        constexpr float c_NavigatePush = 0.5f;
        constexpr float c_NavigateRepeatDelay = 0.5f;
        constexpr float c_NavigateRepeatRate = 0.1f;
        // How far a pad's stick or trigger has to be pushed for the UI to take it from the game
        constexpr float c_TakenPush = 0.5f;
        // RmlUi's left, right and middle buttons
        constexpr std::array<MouseCode, 3> c_PointerButtons{ MouseCode::TR_LEFT, MouseCode::TR_RIGHT, MouseCode::TR_MIDDLE };

        enum class NavigateDirection : std::uint8_t
        {
            None,
            Up,
            Down,
            Left,
            Right
        };

        Rml::String GetContextName(UIDocumentID document)
        {
            return std::format("Document {}", document);
        }

        // The UI map's action, found again once the scene it was found in has stopped
        const InputAction& GetUIAction(InputAction& action, std::string_view name)
        {
            if (!action.IsCurrent())
            {
                action = InputAction::Find(std::format("{}{}{}", UIInputMap::c_Name, InputActionsData::c_PathSeparator, name));
            }

            return action;
        }

        // Each of an action's sources the player's devices are pushing now, kept from the game: a key, a mouse button, a pad's button, axis or stick
        void TakeSources(const InputAction& action)
        {
            for (std::uint32_t it_Binding = 0; it_Binding < action.GetBindingCount(); ++it_Binding)
            {
                for (std::uint32_t it_Part = 0; it_Part < action.GetPartCount(it_Binding); ++it_Part)
                {
                    const InputSource l_Source = action.GetSource(it_Binding, it_Part, 0);
                    switch (l_Source.Kind)
                    {
                        case InputSourceKind::Key:
                        {
                            Input::TakeKey(static_cast<KeyCode>(l_Source.Code));

                            break;
                        }
                        case InputSourceKind::MouseButton:
                        {
                            Input::TakeMouseButton(static_cast<MouseCode>(l_Source.Code));

                            break;
                        }
                        case InputSourceKind::GamepadButton:
                        {
                            for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
                            {
                                Input::TakeGamepadButton(it_Gamepad, static_cast<GamepadButton>(l_Source.Code));
                            }

                            break;
                        }
                        case InputSourceKind::GamepadAxis:
                        case InputSourceKind::GamepadStick:
                        {
                            const bool l_Stick = l_Source.Kind == InputSourceKind::GamepadStick;
                            const GamepadAxis l_X = l_Stick ? (l_Source.Code == 0 ? GamepadAxis::TR_LEFT_STICK_X : GamepadAxis::TR_RIGHT_STICK_X) : static_cast<GamepadAxis>(l_Source.Code);
                            const GamepadAxis l_Y = l_Source.Code == 0 ? GamepadAxis::TR_LEFT_STICK_Y : GamepadAxis::TR_RIGHT_STICK_Y;
                            for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
                            {
                                const float l_Push = l_Stick ? glm::length(glm::vec2(Input::GetGamepadAxis(it_Gamepad, l_X), Input::GetGamepadAxis(it_Gamepad, l_Y))) : std::abs(Input::GetGamepadAxis(it_Gamepad, l_X));
                                if (l_Push >= c_TakenPush)
                                {
                                    Input::TakeGamepadAxis(it_Gamepad, l_X);
                                    if (l_Stick)
                                    {
                                        Input::TakeGamepadAxis(it_Gamepad, l_Y);
                                    }
                                }
                            }

                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                }
            }
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

    // RmlUi keeps the interfaces until it shuts down, so they live as long as the UI does. Each document has a context of its own and keeps the list it recorded until it records again, which its view's overlay pass plays back with the view's other documents', in their sort order, or a world document's own pass plays back into its texture
    struct GameUI::State final : ViewOverlay, WorldUISource
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
            // A world document has no view, and its World settings and the size its texture is at the last Update, 0 when it draws nothing
            bool World = false;
            UIWorldSettings WorldSettings;
            glm::uvec2 TextureSize{ 0 };
        };

        // A UIDocument's document, by its entity, kept while ShowScene or ShowWorld sees it: a screen document over a view, and a world document in a scene, which is known by its address alone and never read through. One whose asset could not be loaded keeps no document, and is not tried again until it names another
        struct SceneDocument
        {
            ViewID View = Renderer::c_MainView;
            const Scene* Owner = nullptr;
            bool World = false;
            UUID Entity;
            UUID Asset;
            UIDocumentID Document = c_NoDocument;
            bool Shown = false;
        };

        // A world document drawn in this frame's graph, into its texture, whose bindless index is known once its pass has run
        struct WorldPass
        {
            UIDocumentID Document = c_NoDocument;
            FrameGraphTexture Texture;
            std::uint32_t Index = RHI::c_NoBindlessIndex;
        };

        State(RHI::Device& device, RHI::Format displayFormat) : Rendering(device, displayFormat, Renderer::c_OverlayStencilFormat), DisplayFormat(displayFormat)
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
        // The view whose documents take the scene's input, the document each pointer button's press went to, which its release goes to too, and Navigate's direction and the time to its next move while it is held there
        std::optional<ViewID> InputView;
        std::array<UIDocumentID, c_PointerButtons.size()> Captures{};
        NavigateDirection Direction = NavigateDirection::None;
        float NavigateWait = 0.0f;
        InputAction Navigate;
        InputAction Submit;
        InputAction Cancel;
        GameUIVector<Document*> InputDocuments;
        Rml::SharedPtr<Rml::StyleSheetContainer> BaseStyle;
        RHI::Format DisplayFormat = RHI::Format::Unknown;
        GameUIVector<WorldPass> WorldPasses;
        GameUIVector<FrameGraphTexture> WorldTextures;
        // The scene each view was last shown, whose world documents go as the view forgets its scene
        GameUIVector<std::pair<ViewID, const Scene*>> ViewScenes;

        // The element with the focus, but neither the document's body nor the context's root, which stand for none
        [[nodiscard]] static Rml::Element* GetFocusedElement(const Document& document)
        {
            Rml::Element* l_Focus = document.Context->GetFocusElement();

            return l_Focus != nullptr && l_Focus != document.Context->GetRootElement() && l_Focus != document.Rml ? l_Focus : nullptr;
        }

        // An element under the pointer, other than the document's body, which lets the pointer through to the game
        [[nodiscard]] static bool TakesPointer(const Document& document)
        {
            Rml::Element* l_Hover = document.Context->GetHoverElement();

            return l_Hover != nullptr && l_Hover != document.Context->GetRootElement() && l_Hover != document.Rml;
        }

        // The focus back on the document's body
        static void Blur(Document& document)
        {
            if (GetFocusedElement(document) != nullptr)
            {
                document.Rml->Focus();
            }
        }

        // Under the document's own stylesheets, whose rules win where both set a property
        void ApplyBaseStyle(Rml::ElementDocument& document) const
        {
            if (!BaseStyle)
            {
                return;
            }

            const Rml::StyleSheetContainer* l_Own = document.GetStyleSheetContainer();
            document.SetStyleSheetContainer(l_Own != nullptr ? BaseStyle->CombineStyleSheetContainer(*l_Own) : BaseStyle->CombineStyleSheetContainer(Rml::StyleSheetContainer()));
        }

        // As the view stops taking input: no press goes on, and nothing is hovered
        void ReleaseInput()
        {
            Captures.fill(c_NoDocument);
            Direction = NavigateDirection::None;
            for (Document& it_Document : Documents)
            {
                if (InputView && !it_Document.World && it_Document.View == *InputView)
                {
                    it_Document.Context->ProcessMouseLeave();
                }
            }
        }

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

        // Into a context of its own of this size, shown, with the base style under its own. c_NoDocument when it cannot be, which is logged
        UIDocumentID Load(std::string_view path, ViewID view, Rml::Vector2i size)
        {
            const UIDocumentID l_ID = NextDocument++;
            Rml::Context* l_Context = Rml::CreateContext(GetContextName(l_ID), size);
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

            ApplyBaseStyle(*l_Document);
            l_Document->Show();

            Document& l_Entry = Documents.emplace_back();
            l_Entry.ID = l_ID;
            l_Entry.View = view;
            l_Entry.Context = l_Context;
            l_Entry.Rml = l_Document;
            l_Entry.Path = Rml::String(path);
            l_Entry.Panel.ScaleMode = UIScaleMode::ConstantPixelSize;
            l_Entry.Sequence = NextSequence++;
            Sorted = false;

            return l_ID;
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
                if (it_Document.World || it_Document.View != view || it_Document.DrawList.Commands.empty())
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
                if (!it_Document.World && it_Document.View == view && !it_Document.DrawList.Commands.empty())
                {
                    Rendering.Draw(commands, it_Document.DrawList, width, height, stencil);
                }
            }
        }

        // One that recorded nothing at its last Update, such as a hidden one, or has no size, draws nothing, and so has no quad
        [[nodiscard]] std::optional<WorldUIDocument> FindWorldDocument(const Scene & scene, UUID entity) const override
        {
            const auto a_Entry = std::ranges::find_if(SceneDocuments, [&](const SceneDocument& entry) { return entry.World && entry.Owner == &scene && entry.Entity == entity; });
            const Document* l_Document = a_Entry != SceneDocuments.end() ? FindDocument(a_Entry->Document) : nullptr;
            if (l_Document == nullptr || !Draws(*l_Document))
            {
                return std::nullopt;
            }

            return WorldUIDocument{ l_Document->ID, l_Document->TextureSize };
        }

        // Each into a texture of its size from the graph's pool, cleared to transparent, with a stencil target of its size for one that clips through a mask. The list it plays back is the document's own, which nothing changes until the next Update, after the graph has run
        void AddWorldPasses(FrameGraph & graph) override
        {
            WorldPasses.clear();
            WorldTextures.clear();
            for (const Document& it_Document : Documents)
            {
                if (!it_Document.World || !Draws(it_Document))
                {
                    continue;
                }

                const std::uint32_t l_Width = it_Document.TextureSize.x;
                const std::uint32_t l_Height = it_Document.TextureSize.y;
                RHI::TextureDescription l_Description;
                l_Description.Width = l_Width;
                l_Description.Height = l_Height;
                l_Description.TextureFormat = DisplayFormat;
                l_Description.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
                l_Description.DebugName = "World UI";
                const FrameGraphTexture l_Texture = graph.CreateTexture("World UI", l_Description);

                FrameGraphTexture l_Stencil;
                if (it_Document.DrawList.ClipMask && Rendering.CanClip())
                {
                    l_Description.TextureFormat = Renderer::c_OverlayStencilFormat;
                    l_Description.DebugName = "World UI stencil";
                    l_Stencil = graph.CreateTexture("World UI stencil", l_Description);
                }

                const std::size_t l_Slot = WorldPasses.size();
                WorldPasses.push_back({ it_Document.ID, l_Texture });
                WorldTextures.push_back(l_Texture);
                const GameUIDrawList* l_List = &it_Document.DrawList;
                graph.AddPass("World UI", FrameGraphPassType::Raster, [l_Texture, l_Stencil](FrameGraphPassBuilder& builder)
                {
                    builder.AddColorAttachment({ l_Texture, RHI::LoadOp::Clear,{ 0.0f, 0.0f, 0.0f, 0.0f } });
                    if (l_Stencil)
                    {
                        builder.SetDepthAttachment({ l_Stencil, RHI::LoadOp::Clear, 0.0f, 0 });
                    }
                }, [this, l_List, l_Slot, l_Texture, l_Stencil, l_Width, l_Height](const FrameGraphContext& context)
                {
                    Rendering.Draw(context.GetCommands(), *l_List, l_Width, l_Height, static_cast<bool>(l_Stencil));
                    WorldPasses[l_Slot].Index = context.GetDevice().GetShaderResourceIndex(context.GetTexture(l_Texture));
                });
            }
        }

        [[nodiscard]] std::span<const FrameGraphTexture> GetWorldTextures() const override
        {
            return WorldTextures;
        }

        [[nodiscard]] std::uint32_t GetWorldTextureIndex(std::uint32_t document) const override
        {
            const auto a_Pass = std::ranges::find(WorldPasses, document, &WorldPass::Document);

            return a_Pass != WorldPasses.end() ? a_Pass->Index : RHI::c_NoBindlessIndex;
        }

        // A fixed size is its own, and a dynamic one is laid out in the largest there is, then takes from the context's top left to the body's bottom right, so a margin on the body is kept around it, and its absolute positioning lets it shrink to its content where it sets no size
        void UpdateWorld(Document & document)
        {
            const UIWorldSettings& l_Settings = document.WorldSettings;
            const bool l_Dynamic = l_Settings.SizeMode == UIWorldSizeMode::Dynamic;
            const glm::uvec2 l_Fixed = glm::clamp(l_Settings.Size, glm::uvec2(1), glm::uvec2(UIWorldSettings::c_MaxSize));
            const Rml::Vector2i l_Size = l_Dynamic ? Rml::Vector2i(static_cast<int>(UIWorldSettings::c_MaxSize)) : Rml::Vector2i(static_cast<int>(l_Fixed.x), static_cast<int>(l_Fixed.y));
            if (document.Context->GetDimensions() != l_Size)
            {
                document.Context->SetDimensions(l_Size);
            }

            document.Scale = document.Panel.GetWorldScale();
            document.Context->SetDensityIndependentPixelRatio(document.Scale);
            document.Context->Update();
            document.TextureSize = l_Fixed;
            if (l_Dynamic)
            {
                const Rml::Vector2f l_Corner = document.Rml->GetAbsoluteOffset(Rml::BoxArea::Border) + document.Rml->GetBox().GetSize(Rml::BoxArea::Border);
                const auto a_Pixels = [](float extent) { return std::isfinite(extent) ? static_cast<std::uint32_t>(std::clamp(std::ceil(extent), 0.0f, static_cast<float>(UIWorldSettings::c_MaxSize))) : 0u; };
                document.TextureSize = glm::uvec2(a_Pixels(l_Corner.x), a_Pixels(l_Corner.y));
            }

            Rendering.Begin(document.DrawList);
            document.Context->Render();
            Rendering.End();
        }

        // A world document draws when its last Update recorded something into a texture of some size, and the shaders are there to draw it
        [[nodiscard]] bool Draws(const Document & document) const
        {
            return Rendering.CanDraw() && document.TextureSize.x != 0 && document.TextureSize.y != 0 && !document.DrawList.Commands.empty();
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
        m_State->BaseStyle = Rml::Factory::InstanceStyleSheetString(c_BaseStyleSheet);
        InputActionsRuntime::SetUIHandler([this](Timestep timestep) { ProcessInput(timestep); });

        m_Renderer.SetViewOverlay(m_State.get());
        m_Renderer.SetWorldUISource(m_State.get());
        TR_CORE_INFO("GameUI: RmlUi {} started", Rml::GetVersion());
    }

    // RmlUi ends every context with its documents, and releases what it made, before the interfaces go. What it still holds then is in its own statics, kept until the process ends, so it is counted as untagged, as every other static's memory is
    GameUI::~GameUI()
    {
        InputActionsRuntime::SetUIHandler(nullptr);
        m_Renderer.SetViewOverlay(nullptr);
        m_Renderer.SetWorldUISource(nullptr);
        {
            const MemoryTagScope l_Scope(MemoryTag::GameUI);
            m_State->InputDocuments = GameUIVector<State::Document*>();
            m_State->WorldPasses = GameUIVector<State::WorldPass>();
            m_State->WorldTextures = GameUIVector<FrameGraphTexture>();
            m_State->ViewScenes = GameUIVector<std::pair<ViewID, const Scene*>>();
            m_State->BaseStyle.reset();
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

        return m_State->Load(path, view, Rml::Vector2i(static_cast<int>(std::max(m_Renderer.GetSceneWidth(view), 1u)), static_cast<int>(std::max(m_Renderer.GetSceneHeight(view), 1u))));
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

    void GameUI::ShowScene(Scene& scene, ViewID view, bool interactive)
    {
        if (!m_State->Started)
        {
            return;
        }

        TR_PROFILE_FUNCTION();

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        if (interactive != (m_State->InputView == view))
        {
            m_State->ReleaseInput();
            m_State->InputView = interactive ? std::optional<ViewID>(view) : std::nullopt;
        }

        const auto a_Shown = std::ranges::find(m_State->ViewScenes, view, &std::pair<ViewID, const Scene*>::first);
        if (a_Shown != m_State->ViewScenes.end())
        {
            a_Shown->second = &scene;
        }
        else
        {
            m_State->ViewScenes.emplace_back(view, &scene);
        }

        ShowDocuments(scene, view);
    }

    void GameUI::ShowWorld(Scene& scene)
    {
        if (!m_State->Started)
        {
            return;
        }

        TR_PROFILE_FUNCTION();

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        ShowDocuments(scene, std::nullopt);
    }

    // The hierarchy is walked only when two screen documents share a sort order, which is all its order decides. A world document is the scene's, whichever view, or none, it is shown through, so a scene shown twice in a frame keeps one of each
    void GameUI::ShowDocuments(Scene& scene, std::optional<ViewID> view)
    {
        SceneRegistry& l_Registry = scene.GetRegistry();
        const auto l_Components = l_Registry.view<UIDocumentComponent, IDComponent>();
        GameUIVector<std::int32_t>& l_Orders = m_State->SortOrders;
        GameUIVector<std::pair<UUID, std::uint64_t>>& l_Ranks = m_State->HierarchyRanks;
        l_Orders.clear();
        l_Ranks.clear();
        for (const auto [it_Entity, it_Component, it_ID] : l_Components.each())
        {
            if (view && it_Component.RenderMode == UIRenderMode::Screen)
            {
                l_Orders.push_back(it_Component.SortOrder);
            }
        }

        std::ranges::sort(l_Orders);
        if (std::ranges::adjacent_find(l_Orders) != l_Orders.end())
        {
            std::uint64_t l_Rank = 0;
            for (Entity it_Walked = scene.GetFirstRoot(); it_Walked; it_Walked = scene.GetNextInHierarchyOrder(it_Walked))
            {
                if (const UIDocumentComponent* l_Walked = l_Registry.try_get<UIDocumentComponent>(it_Walked.GetHandle()); l_Walked != nullptr && l_Walked->RenderMode == UIRenderMode::Screen)
                {
                    l_Ranks.emplace_back(it_Walked.GetUUID(), l_Rank++);
                }
            }
        }

        const AssetRegistry* l_Assets = AssetManager::GetRegistry();
        for (const auto [it_Entity, it_Component, it_ID] : l_Components.each())
        {
            const bool l_World = it_Component.RenderMode == UIRenderMode::World;
            if (!l_World && !view)
            {
                continue;
            }

            const UUID l_EntityID = it_ID.ID;
            auto a_Entry = std::ranges::find_if(m_State->SceneDocuments, [&](const State::SceneDocument& entry) { return entry.Entity == l_EntityID && entry.World == l_World && (l_World ? entry.Owner == &scene : entry.View == *view); });
            if (a_Entry == m_State->SceneDocuments.end())
            {
                State::SceneDocument& l_New = m_State->SceneDocuments.emplace_back();
                l_New.View = l_World ? Renderer::c_MainView : *view;
                l_New.Owner = l_World ? &scene : nullptr;
                l_New.World = l_World;
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
                    else if (l_World)
                    {
                        a_Entry->Document = m_State->Load(l_Record->Path, Renderer::c_MainView, Rml::Vector2i(1, 1));
                    }
                    else
                    {
                        a_Entry->Document = LoadDocument(l_Record->Path, *view);
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
            l_Document->World = l_World;
            l_Document->WorldSettings = it_Component.World;
            if (l_World)
            {
                continue;
            }

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
        if (m_State->InputView == view)
        {
            m_State->ReleaseInput();
        }

        const auto a_Shown = std::ranges::find(m_State->ViewScenes, view, &std::pair<ViewID, const Scene*>::first);
        const Scene* l_Scene = a_Shown != m_State->ViewScenes.end() ? a_Shown->second : nullptr;
        if (a_Shown != m_State->ViewScenes.end())
        {
            m_State->ViewScenes.erase(a_Shown);
        }

        std::erase_if(m_State->SceneDocuments, [this, view, l_Scene](const State::SceneDocument& entry)
        {
            if (entry.World ? entry.Owner != l_Scene : entry.View != view)
            {
                return false;
            }

            m_State->Remove(entry.Document);

            return true;
        });
    }

    UIDocumentID GameUI::FindSceneDocument(UUID entity, ViewID view) const
    {
        const auto a_Entry = std::ranges::find_if(m_State->SceneDocuments, [&](const State::SceneDocument& entry) { return !entry.World && entry.View == view && entry.Entity == entity; });

        return a_Entry != m_State->SceneDocuments.end() ? a_Entry->Document : c_NoDocument;
    }

    UIDocumentID GameUI::FindWorldDocument(const Scene& scene, UUID entity) const
    {
        const auto a_Entry = std::ranges::find_if(m_State->SceneDocuments, [&](const State::SceneDocument& entry) { return entry.World && entry.Owner == &scene && entry.Entity == entity; });

        return a_Entry != m_State->SceneDocuments.end() ? a_Entry->Document : c_NoDocument;
    }

    glm::uvec2 GameUI::GetTextureSize(UIDocumentID document) const
    {
        const State::Document* l_Document = m_State->FindDocument(document);

        return l_Document != nullptr ? l_Document->TextureSize : glm::uvec2(0);
    }

    std::string GameUI::GetFocus(UIDocumentID document) const
    {
        const State::Document* l_Document = m_State->FindDocument(document);
        const Rml::Element* l_Focus = l_Document != nullptr ? State::GetFocusedElement(*l_Document) : nullptr;

        return l_Focus != nullptr ? std::string(l_Focus->GetId()) : std::string();
    }

    // The interactive view's documents, top first. The pointer goes to the document a button pressed in while it is held, and otherwise to the topmost with an element under it, the documents below losing it, and a press there is the UI's. A press on no element takes the focus away from every document. A text field with the focus takes every key and the text typed, and otherwise Navigate moves the focus, Submit presses what has it and Cancel sends it a cancel event, which takes the focus away unless the element or its document stops it. While an element has the focus, every source of the UI map is the UI's
    void GameUI::ProcessInput(Timestep timestep)
    {
        if (!m_State->Started || !m_State->InputView)
        {
            return;
        }

        TR_PROFILE_FUNCTION();

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        const ViewID l_View = *m_State->InputView;
        m_State->Sort();
        GameUIVector<State::Document*>& l_Documents = m_State->InputDocuments;
        l_Documents.clear();
        for (auto it_Document = m_State->Documents.rbegin(); it_Document != m_State->Documents.rend(); ++it_Document)
        {
            if (!it_Document->World && it_Document->View == l_View && it_Document->Visible)
            {
                l_Documents.push_back(&*it_Document);
            }
        }

        if (l_Documents.empty())
        {
            m_State->Captures.fill(c_NoDocument);
            m_State->Direction = NavigateDirection::None;

            return;
        }

        const int l_Modifiers = GameUIInput::GetKeyModifiers();
        const MousePosition l_Mouse = Input::GetMousePosition();
        const bool l_Inside = l_Mouse.X >= 0.0f && l_Mouse.Y >= 0.0f && l_Mouse.X < static_cast<float>(m_Renderer.GetSceneWidth(l_View)) && l_Mouse.Y < static_cast<float>(m_Renderer.GetSceneHeight(l_View));
        State::Document* l_Captured = nullptr;
        for (const UIDocumentID it_Capture : m_State->Captures)
        {
            if (State::Document* l_Document = m_State->FindDocument(it_Capture); l_Document != nullptr && l_Captured == nullptr)
            {
                l_Captured = l_Document;
            }
        }

        State::Document* l_Under = nullptr;
        for (State::Document* it_Document : l_Documents)
        {
            const bool l_Gets = l_Captured != nullptr ? it_Document == l_Captured : l_Inside && l_Under == nullptr;
            if (!l_Gets)
            {
                it_Document->Context->ProcessMouseLeave();

                continue;
            }

            it_Document->Context->ProcessMouseMove(static_cast<int>(std::floor(l_Mouse.X)), static_cast<int>(std::floor(l_Mouse.Y)), l_Modifiers);
            if (l_Captured != nullptr || State::TakesPointer(*it_Document))
            {
                l_Under = it_Document;
            }
        }

        for (std::size_t it_Button = 0; it_Button < c_PointerButtons.size(); ++it_Button)
        {
            const MouseCode l_Button = c_PointerButtons[it_Button];
            const int l_Index = static_cast<int>(it_Button);
            if (Input::WasMouseButtonPressed(l_Button))
            {
                for (State::Document* it_Document : l_Documents)
                {
                    if (it_Document != l_Under)
                    {
                        State::Blur(*it_Document);
                    }
                }

                if (l_Under != nullptr)
                {
                    l_Under->Context->ProcessMouseButtonDown(l_Index, l_Modifiers);
                    m_State->Captures[it_Button] = l_Under->ID;
                    Input::TakeMouseButton(l_Button);
                }
            }

            if (Input::WasMouseButtonReleased(l_Button))
            {
                if (State::Document* l_Pressed = m_State->FindDocument(std::exchange(m_State->Captures[it_Button], c_NoDocument)))
                {
                    l_Pressed->Context->ProcessMouseButtonUp(l_Index, l_Modifiers);
                }
            }
        }

        if (const glm::vec2 l_Scroll = Input::GetMouseScroll(); l_Under != nullptr && (l_Scroll.x != 0.0f || l_Scroll.y != 0.0f))
        {
            l_Under->Context->ProcessMouseWheel(Rml::Vector2f(l_Scroll.x, -l_Scroll.y), l_Modifiers);
            Input::TakeMouseScroll();
        }

        const auto a_FindFocus = [&l_Documents]() -> std::pair<State::Document*, Rml::Element*>
        {
            for (State::Document* it_Document : l_Documents)
            {
                if (Rml::Element* l_Element = State::GetFocusedElement(*it_Document))
                {
                    return { it_Document, l_Element };
                }
            }

            return { nullptr, nullptr };
        };

        auto [l_Focused, l_Element] = a_FindFocus();
        const bool l_TextField = l_Element != nullptr && GameUIInput::IsTextField(*l_Element);
        if (l_TextField)
        {
            for (const KeyStroke& it_Stroke : Input::GetKeyStrokes())
            {
                if (const Rml::Input::KeyIdentifier l_Key = GameUIInput::ToKeyIdentifier(it_Stroke.Key); l_Key != Rml::Input::KI_UNKNOWN)
                {
                    if (it_Stroke.Down)
                    {
                        l_Focused->Context->ProcessKeyDown(l_Key, l_Modifiers);
                    }
                    else
                    {
                        l_Focused->Context->ProcessKeyUp(l_Key, l_Modifiers);
                    }
                }

                if (it_Stroke.Down)
                {
                    Input::TakeKey(it_Stroke.Key);
                }
            }

            for (const char32_t it_Character : Input::GetTypedText())
            {
                if (it_Character >= U' ' && it_Character != U'\x7F')
                {
                    l_Focused->Context->ProcessTextInput(static_cast<Rml::Character>(it_Character));
                }
            }
        }

        const InputAction& l_Navigate = GetUIAction(m_State->Navigate, UIInputMap::c_Navigate);
        const InputAction& l_Submit = GetUIAction(m_State->Submit, UIInputMap::c_Submit);
        const InputAction& l_Cancel = GetUIAction(m_State->Cancel, UIInputMap::c_Cancel);
        NavigateDirection l_Direction = NavigateDirection::None;
        if (const glm::vec2 l_Push = l_Navigate.ReadVector2(); !l_TextField && glm::length(l_Push) >= c_NavigatePush)
        {
            l_Direction = std::abs(l_Push.x) > std::abs(l_Push.y) ? (l_Push.x > 0.0f ? NavigateDirection::Right : NavigateDirection::Left) : (l_Push.y > 0.0f ? NavigateDirection::Up : NavigateDirection::Down);
        }

        bool l_Move = false;
        if (l_Direction != m_State->Direction)
        {
            m_State->Direction = l_Direction;
            m_State->NavigateWait = c_NavigateRepeatDelay;
            l_Move = l_Direction != NavigateDirection::None;
        }
        else if (l_Direction != NavigateDirection::None)
        {
            m_State->NavigateWait -= timestep.GetSeconds();
            if (m_State->NavigateWait <= 0.0f)
            {
                m_State->NavigateWait += c_NavigateRepeatRate;
                l_Move = true;
            }
        }

        // With nothing focused, the topmost document with an element that takes the focus gives it to its first. An element's own nav property says where it goes, as RmlUi takes the arrows, and otherwise Down and Right go to the next element in document order and Up and Left to the one before, round from the last to the first
        if (l_Move && l_Element == nullptr)
        {
            for (State::Document* it_Document : l_Documents)
            {
                Rml::Element* l_First = it_Document->Rml->FindNextTabElement(it_Document->Rml, true);
                if (l_First != nullptr && l_First->Focus(true))
                {
                    l_First->ScrollIntoView(false);

                    break;
                }
            }
        }
        else if (l_Move)
        {
            constexpr std::array<std::pair<Rml::PropertyId, Rml::Input::KeyIdentifier>, 4> c_Directions{ { { Rml::PropertyId::NavUp, Rml::Input::KI_UP },{ Rml::PropertyId::NavDown, Rml::Input::KI_DOWN },{ Rml::PropertyId::NavLeft, Rml::Input::KI_LEFT },{ Rml::PropertyId::NavRight, Rml::Input::KI_RIGHT } } };
            const auto [l_Property, l_Key] = c_Directions[static_cast<std::size_t>(l_Direction) - 1];
            if (l_Element->GetLocalProperty(l_Property) != nullptr)
            {
                l_Focused->Context->ProcessKeyDown(l_Key, 0);
                l_Focused->Context->ProcessKeyUp(l_Key, 0);
            }
            else if (Rml::Element* l_Next = l_Focused->Rml->FindNextTabElement(l_Element, l_Direction == NavigateDirection::Down || l_Direction == NavigateDirection::Right); l_Next != nullptr && l_Next->Focus(true))
            {
                l_Next->ScrollIntoView(false);
            }
        }

        std::tie(l_Focused, l_Element) = a_FindFocus();
        if (l_Element != nullptr && !l_TextField && l_Submit.WasPressed())
        {
            l_Element->Click();
        }

        if (l_Element != nullptr && l_Cancel.WasPressed() && l_Element->DispatchEvent("cancel", Rml::Dictionary(), true, true))
        {
            State::Blur(*l_Focused);
        }

        if (a_FindFocus().second != nullptr)
        {
            TakeSources(l_Navigate);
            TakeSources(l_Submit);
            TakeSources(l_Cancel);
        }
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
                m_State->ApplyBaseStyle(*it_Document.Rml);
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

            m_State->ApplyBaseStyle(*l_Document);
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
            if (!l_Document.World && !m_Renderer.HasView(l_Document.View))
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
            if (it_Document.World)
            {
                m_State->UpdateWorld(it_Document);
                m_State->Commands += static_cast<std::uint32_t>(it_Document.DrawList.Commands.size());

                continue;
            }

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
        l_Statistics.WorldDocuments = static_cast<std::uint32_t>(std::ranges::count_if(m_State->Documents, &State::Document::World));
        l_Statistics.WorldPasses = static_cast<std::uint32_t>(m_State->WorldPasses.size());
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