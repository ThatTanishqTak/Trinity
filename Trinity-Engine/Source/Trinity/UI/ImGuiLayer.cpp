#include "Trinity/UI/ImGuiLayer.hpp"

#include "Trinity/Core/Application.hpp"
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Core/Window.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Input/Input.hpp"
#include "Trinity/Renderer/Renderer.hpp"
#include "Trinity/UI/ImGuiRenderer.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace Trinity
{
    namespace
    {
        constexpr std::string_view c_SettingsPath = "/saves/imgui.ini";
        constexpr float c_FontSize = 15.0f;
        constexpr float c_MinimumUserScale = 0.5f;
        constexpr float c_MaximumUserScale = 4.0f;

        struct FontFile
        {
            std::string_view Path;
            std::string_view Name;
        };

        // In UIFont order
        constexpr std::array<FontFile, 2> c_FontFiles
        { {
            { "/engine/fonts/JetBrainsMonoNLNerdFontPropo-Regular.ttf", "JetBrains Mono Regular" },
            { "/engine/fonts/JetBrainsMonoNLNerdFontPropo-Bold.ttf", "JetBrains Mono Bold" }
        } };

        ConsoleVariable<float> s_UserScaleVariable("ui.scale", 1.0f, "UI size on top of the monitor's DPI scale, from 0.5 to 4");

        void* AllocateUI(std::size_t size, [[maybe_unused]] void* userData)
        {
            return Memory::TryAllocate(size, MemoryTag::UI);
        }

        void FreeUI(void* memory, [[maybe_unused]] void* userData)
        {
            Memory::Free(memory);
        }

        // The atlas keeps its own copy of the file for its whole life, and frees it through FreeUI
        bool LoadFont(const FontFile& file)
        {
            const Expected<FileBuffer, FileError> l_File = FileSystem::ReadFile(file.Path);
            if (!l_File)
            {
                TR_CORE_WARN("ImGui: cannot read {}: {}", file.Path, ToString(l_File.GetError()));

                return false;
            }

            // ImGui asserts on fewer than 100 bytes, which no font has
            if (l_File->size() <= 100 || l_File->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                TR_CORE_WARN("ImGui: {} is {} bytes, too small or too large for a font", file.Path, l_File->size());

                return false;
            }

            void* l_Data = ImGui::MemAlloc(l_File->size());
            if (l_Data == nullptr)
            {
                TR_CORE_WARN("ImGui: no memory for {}", file.Path);

                return false;
            }

            std::memcpy(l_Data, l_File->data(), l_File->size());

            ImFontConfig l_Config;
            std::format_to_n(l_Config.Name, sizeof(l_Config.Name) - 1, "{}", file.Name);
            if (ImGui::GetIO().Fonts->AddFontFromMemoryTTF(l_Data, static_cast<int>(l_File->size()), c_FontSize, &l_Config) == nullptr)
            {
                TR_CORE_WARN("ImGui: FreeType cannot read {}", file.Path);

                return false;
            }

            return true;
        }

        // Bold is skipped when Regular fails, so GetFont gives the default font for both. A missing Bold leaves Regular for both
        void LoadFonts()
        {
            if (!LoadFont(c_FontFiles[std::to_underlying(UIFont::Regular)]))
            {
                ImGui::GetIO().Fonts->AddFontDefault();
                TR_CORE_INFO("ImGui: drawing with its default font");

                return;
            }

            const bool l_Bold = LoadFont(c_FontFiles[std::to_underlying(UIFont::Bold)]);
            TR_CORE_INFO("ImGui: {} loaded at {} px", l_Bold ? "JetBrains Mono Regular and Bold" : "JetBrains Mono Regular", c_FontSize);
        }

        // ImGui keeps the returned text until the next call, in the layer's own string
        const char* ReadClipboard([[maybe_unused]] ImGuiContext* context)
        {
            std::string& l_Text = *static_cast<std::string*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
            l_Text = Platform::GetClipboardText();

            return l_Text.c_str();
        }

        void WriteClipboard([[maybe_unused]] ImGuiContext* context, const char* text)
        {
            Platform::SetClipboardText(text);
        }

        CursorShape ToCursorShape(ImGuiMouseCursor cursor)
        {
            switch (cursor)
            {
                case ImGuiMouseCursor_None:
                {
                    return CursorShape::Hidden;
                }
                case ImGuiMouseCursor_TextInput:
                {
                    return CursorShape::TextInput;
                }
                case ImGuiMouseCursor_ResizeAll:
                {
                    return CursorShape::ResizeAll;
                }
                case ImGuiMouseCursor_ResizeNS:
                {
                    return CursorShape::ResizeNS;
                }
                case ImGuiMouseCursor_ResizeEW:
                {
                    return CursorShape::ResizeEW;
                }
                case ImGuiMouseCursor_ResizeNESW:
                {
                    return CursorShape::ResizeNESW;
                }
                case ImGuiMouseCursor_ResizeNWSE:
                {
                    return CursorShape::ResizeNWSE;
                }
                case ImGuiMouseCursor_Hand:
                {
                    return CursorShape::Hand;
                }
                case ImGuiMouseCursor_Wait:
                {
                    return CursorShape::Wait;
                }
                case ImGuiMouseCursor_Progress:
                {
                    return CursorShape::Progress;
                }
                case ImGuiMouseCursor_NotAllowed:
                {
                    return CursorShape::NotAllowed;
                }
                default:
                {
                    return CursorShape::Arrow;
                }
            }
        }

        struct KeyMapping
        {
            KeyCode Key;
            ImGuiKey Mapped;
        };

        // The keys outside the letter, digit, function and keypad digit ranges
        constexpr std::array<KeyMapping, 49> c_KeyMappings
        { {
            { KeyCode::TR_SPACE, ImGuiKey_Space },
            { KeyCode::TR_APOSTROPHE, ImGuiKey_Apostrophe },
            { KeyCode::TR_COMMA, ImGuiKey_Comma },
            { KeyCode::TR_MINUS, ImGuiKey_Minus },
            { KeyCode::TR_PERIOD, ImGuiKey_Period },
            { KeyCode::TR_SLASH, ImGuiKey_Slash },
            { KeyCode::TR_SEMICOLON, ImGuiKey_Semicolon },
            { KeyCode::TR_EQUAL, ImGuiKey_Equal },
            { KeyCode::TR_LEFT_BRACKET, ImGuiKey_LeftBracket },
            { KeyCode::TR_BACKSLASH, ImGuiKey_Backslash },
            { KeyCode::TR_RIGHT_BRACKET, ImGuiKey_RightBracket },
            { KeyCode::TR_GRAVE_ACCENT, ImGuiKey_GraveAccent },
            { KeyCode::TR_WORLD_2, ImGuiKey_Oem102 },
            { KeyCode::TR_ESCAPE, ImGuiKey_Escape },
            { KeyCode::TR_ENTER, ImGuiKey_Enter },
            { KeyCode::TR_TAB, ImGuiKey_Tab },
            { KeyCode::TR_BACKSPACE, ImGuiKey_Backspace },
            { KeyCode::TR_INSERT, ImGuiKey_Insert },
            { KeyCode::TR_DELETE, ImGuiKey_Delete },
            { KeyCode::TR_RIGHT, ImGuiKey_RightArrow },
            { KeyCode::TR_LEFT, ImGuiKey_LeftArrow },
            { KeyCode::TR_DOWN, ImGuiKey_DownArrow },
            { KeyCode::TR_UP, ImGuiKey_UpArrow },
            { KeyCode::TR_PAGE_UP, ImGuiKey_PageUp },
            { KeyCode::TR_PAGE_DOWN, ImGuiKey_PageDown },
            { KeyCode::TR_HOME, ImGuiKey_Home },
            { KeyCode::TR_END, ImGuiKey_End },
            { KeyCode::TR_CAPS_LOCK, ImGuiKey_CapsLock },
            { KeyCode::TR_SCROLL_LOCK, ImGuiKey_ScrollLock },
            { KeyCode::TR_NUM_LOCK, ImGuiKey_NumLock },
            { KeyCode::TR_PRINT_SCREEN, ImGuiKey_PrintScreen },
            { KeyCode::TR_PAUSE, ImGuiKey_Pause },
            { KeyCode::TR_KP_DECIMAL, ImGuiKey_KeypadDecimal },
            { KeyCode::TR_KP_DIVIDE, ImGuiKey_KeypadDivide },
            { KeyCode::TR_KP_MULTIPLY, ImGuiKey_KeypadMultiply },
            { KeyCode::TR_KP_SUBTRACT, ImGuiKey_KeypadSubtract },
            { KeyCode::TR_KP_ADD, ImGuiKey_KeypadAdd },
            { KeyCode::TR_KP_ENTER, ImGuiKey_KeypadEnter },
            { KeyCode::TR_LEFT_SHIFT, ImGuiKey_LeftShift },
            { KeyCode::TR_LEFT_CONTROL, ImGuiKey_LeftCtrl },
            { KeyCode::TR_LEFT_ALT, ImGuiKey_LeftAlt },
            { KeyCode::TR_LEFT_SUPER, ImGuiKey_LeftSuper },
            { KeyCode::TR_RIGHT_SHIFT, ImGuiKey_RightShift },
            { KeyCode::TR_RIGHT_CONTROL, ImGuiKey_RightCtrl },
            { KeyCode::TR_RIGHT_ALT, ImGuiKey_RightAlt },
            { KeyCode::TR_RIGHT_SUPER, ImGuiKey_RightSuper },
            { KeyCode::TR_MENU, ImGuiKey_Menu },
            { KeyCode::TR_APP_BACK, ImGuiKey_AppBack },
            { KeyCode::TR_APP_FORWARD, ImGuiKey_AppForward }
        } };

        // Every key Trinity has. ImGui's KeypadEqual has no Trinity key, since no Windows keyboard has one
        ImGuiKey ToImGuiKey(KeyCode key)
        {
            const auto a_Offset = [key](KeyCode first) { return std::to_underlying(key) - std::to_underlying(first); };

            if (key >= KeyCode::TR_A && key <= KeyCode::TR_Z)
            {
                return static_cast<ImGuiKey>(ImGuiKey_A + a_Offset(KeyCode::TR_A));
            }

            if (key >= KeyCode::TR_D0 && key <= KeyCode::TR_D9)
            {
                return static_cast<ImGuiKey>(ImGuiKey_0 + a_Offset(KeyCode::TR_D0));
            }

            if (key >= KeyCode::TR_F1 && key <= KeyCode::TR_F24)
            {
                return static_cast<ImGuiKey>(ImGuiKey_F1 + a_Offset(KeyCode::TR_F1));
            }

            if (key >= KeyCode::TR_KP0 && key <= KeyCode::TR_KP9)
            {
                return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + a_Offset(KeyCode::TR_KP0));
            }

            const auto a_Mapping = std::ranges::find(c_KeyMappings, key, &KeyMapping::Key);

            return a_Mapping != c_KeyMappings.end() ? a_Mapping->Mapped : ImGuiKey_None;
        }

        // The polling table already holds this event, since Application updates Input before any layer sees it
        void AddKeyEvent(ImGuiIO& io, KeyCode key, bool pressed)
        {
            io.AddKeyEvent(ImGuiMod_Ctrl, Input::IsKeyPressed(KeyCode::TR_LEFT_CONTROL) || Input::IsKeyPressed(KeyCode::TR_RIGHT_CONTROL));
            io.AddKeyEvent(ImGuiMod_Shift, Input::IsKeyPressed(KeyCode::TR_LEFT_SHIFT) || Input::IsKeyPressed(KeyCode::TR_RIGHT_SHIFT));
            io.AddKeyEvent(ImGuiMod_Alt, Input::IsKeyPressed(KeyCode::TR_LEFT_ALT) || Input::IsKeyPressed(KeyCode::TR_RIGHT_ALT));
            io.AddKeyEvent(ImGuiMod_Super, Input::IsKeyPressed(KeyCode::TR_LEFT_SUPER) || Input::IsKeyPressed(KeyCode::TR_RIGHT_SUPER));

            const ImGuiKey l_Key = ToImGuiKey(key);
            if (l_Key != ImGuiKey_None)
            {
                io.AddKeyEvent(l_Key, pressed);
            }
        }

        void AddMouseButtonEvent(ImGuiIO& io, MouseCode button, bool pressed)
        {
            const int l_Button = std::to_underlying(button);
            if (l_Button < ImGuiMouseButton_COUNT)
            {
                io.AddMouseButtonEvent(l_Button, pressed);
            }
        }
    }

    ImGuiLayer::ImGuiLayer() : Layer("ImGui")
    {

    }

    ImGuiLayer::~ImGuiLayer() = default;

    // ImGui writes no files itself: its settings go through /saves, and are saved when ImGui asks and at shutdown
    void ImGuiLayer::OnAttach()
    {
        ImGui::SetAllocatorFunctions(&AllocateUI, &FreeUI, nullptr);
        IMGUI_CHECKVERSION();
        m_Context = ImGui::CreateContext();

        ImGuiIO& l_IO = ImGui::GetIO();
        l_IO.IniFilename = nullptr;
        l_IO.LogFilename = nullptr;
        l_IO.BackendPlatformName = "Trinity";
        l_IO.BackendRendererName = "Trinity RHI";
        l_IO.BackendFlags |= ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        l_IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Navigation alone does not take the keyboard, so layer shortcuts keep working while an ImGui window has focus
        l_IO.ConfigNavCaptureKeyboard = false;

        ImGuiPlatformIO& l_PlatformIO = ImGui::GetPlatformIO();
        l_PlatformIO.Platform_GetClipboardTextFn = &ReadClipboard;
        l_PlatformIO.Platform_SetClipboardTextFn = &WriteClipboard;
        l_PlatformIO.Platform_ClipboardUserData = &m_ClipboardText;

        LoadFonts();

        const Expected<std::string, FileError> l_Settings = FileSystem::ReadText(c_SettingsPath);
        if (l_Settings)
        {
            ImGui::LoadIniSettingsFromMemory(l_Settings->data(), l_Settings->size());
        }

        Application& l_Application = Application::Get();
        m_DpiScale = l_Application.GetWindow().GetDpiScale();
        m_Renderer = CreateScope<ImGuiRenderer>(l_Application.GetDevice(), l_Application.GetRenderer().GetOutputFormat());

        TR_CORE_INFO("ImGui: {} context created, with settings {} {}", IMGUI_VERSION, l_Settings ? "loaded from" : "to be saved in", c_SettingsPath);
    }

    void ImGuiLayer::OnDetach()
    {
        SaveSettings();
        m_Renderer->DestroyTextures();
        m_Renderer.reset();

        ImGui::DestroyContext(m_Context);
        m_Context = nullptr;
        Application::Get().GetWindow().SetCursorShape(CursorShape::Arrow);

        const MemoryTagStats l_Stats = Memory::GetStats(MemoryTag::UI);
        TR_CORE_INFO("ImGui: context destroyed after {} frame(s), and UI holds {} in {} live allocation(s)", m_FrameCount, Memory::FormatBytes(l_Stats.CurrentBytes), l_Stats.LiveAllocations);
    }

    // Overlays update after every layer, so the UI is built from what this frame's updates left behind
    void ImGuiLayer::OnUpdate(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        Application& l_Application = Application::Get();
        Window& l_Window = l_Application.GetWindow();
        UpdateCursor(l_Window);
        UpdateScale();

        ImGuiIO& l_IO = ImGui::GetIO();
        l_IO.DisplaySize = ImVec2(static_cast<float>(l_Window.GetWidth()), static_cast<float>(l_Window.GetHeight()));
        l_IO.DeltaTime = timestep.GetSeconds() > 0.0f ? timestep.GetSeconds() : 1.0f / 60.0f;

        ImGui::NewFrame();
        for (const Scope<Layer>& it_Layer : l_Application.GetLayerStack())
        {
            it_Layer->OnImGuiRender();
        }

        ImGui::Render();

        if (l_IO.WantSaveIniSettings)
        {
            SaveSettings();
        }

        ++m_FrameCount;
    }

    // Texture requests are recorded before any pass begins, since copies cannot happen inside one
    void ImGuiLayer::OnPrepareRender(RHI::CommandList& commands)
    {
        m_Renderer->UpdateTextures(commands);
    }

    void ImGuiLayer::OnRenderUI(RHI::CommandList& commands)
    {
        if (const ImDrawData* l_DrawData = ImGui::GetDrawData())
        {
            m_Renderer->Render(commands, *l_DrawData);
        }
    }

    // ImGui sees every event, and the ones it wants are hidden from the layers below. Releases always pass, so no layer below is left holding a key or button
    void ImGuiLayer::OnEvent(Event& event)
    {
        ImGuiIO& l_IO = ImGui::GetIO();

        EventDispatcher l_Dispatcher(event);
        l_Dispatcher.Dispatch<MouseMovedEvent>([&l_IO](MouseMovedEvent& moved) { l_IO.AddMousePosEvent(moved.GetX(), moved.GetY()); return l_IO.WantCaptureMouse; });
        l_Dispatcher.Dispatch<MouseLeftEvent>([&l_IO](MouseLeftEvent&) { l_IO.AddMousePosEvent(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()); return false; });
        l_Dispatcher.Dispatch<MouseButtonPressedEvent>([&l_IO](MouseButtonPressedEvent& pressed) { AddMouseButtonEvent(l_IO, pressed.GetMouseButton(), true); return l_IO.WantCaptureMouse; });
        l_Dispatcher.Dispatch<MouseButtonReleasedEvent>([&l_IO](MouseButtonReleasedEvent& released) { AddMouseButtonEvent(l_IO, released.GetMouseButton(), false); return false; });

        // Trinity scrolls right with a positive X, ImGui with a negative one
        l_Dispatcher.Dispatch<MouseScrolledEvent>([&l_IO](MouseScrolledEvent& scrolled) { l_IO.AddMouseWheelEvent(-scrolled.GetXOffset(), scrolled.GetYOffset()); return l_IO.WantCaptureMouse; });

        l_Dispatcher.Dispatch<KeyPressedEvent>([&l_IO](KeyPressedEvent& pressed) { AddKeyEvent(l_IO, pressed.GetKeyCode(), true); return l_IO.WantCaptureKeyboard; });
        l_Dispatcher.Dispatch<KeyReleasedEvent>([&l_IO](KeyReleasedEvent& released) { AddKeyEvent(l_IO, released.GetKeyCode(), false); return false; });
        l_Dispatcher.Dispatch<KeyTypedEvent>([&l_IO](KeyTypedEvent& typed) { l_IO.AddInputCharacter(static_cast<unsigned int>(typed.GetCodepoint())); return l_IO.WantCaptureKeyboard; });
        l_Dispatcher.Dispatch<WindowFocusEvent>([&l_IO](WindowFocusEvent&) { l_IO.AddFocusEvent(true); return false; });
        l_Dispatcher.Dispatch<WindowLostFocusEvent>([&l_IO](WindowLostFocusEvent&) { l_IO.AddFocusEvent(false); return false; });
        l_Dispatcher.Dispatch<WindowDpiChangedEvent>([this](WindowDpiChangedEvent& changed) { m_DpiScale = changed.GetScale(); return false; });
    }

    // ImGui picks the cursor during a frame and the window shows it from the next one, as ImGui's own platform backends do
    void ImGuiLayer::UpdateCursor(Window& window)
    {
        const ImGuiIO& l_IO = ImGui::GetIO();
        if ((l_IO.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0)
        {
            return;
        }

        const ImGuiMouseCursor l_Cursor = l_IO.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
        if (l_Cursor != m_Cursor)
        {
            m_Cursor = l_Cursor;
            window.SetCursorShape(ToCursorShape(l_Cursor));
        }
    }

    ImFont* ImGuiLayer::GetFont(UIFont font)
    {
        const ImVector<ImFont*>& l_Fonts = ImGui::GetIO().Fonts->Fonts;

        return l_Fonts[std::min(static_cast<int>(std::to_underlying(font)), l_Fonts.Size - 1)];
    }

    // Fonts are drawn at the scaled size, so text stays sharp. The style is rebuilt from ImGui's defaults because ScaleAllSizes rounds down and cannot be undone, which also drops style editor changes
    void ImGuiLayer::UpdateScale()
    {
        const float l_UserScale = std::clamp(s_UserScaleVariable.Get(), c_MinimumUserScale, c_MaximumUserScale);
        if (m_DpiScale == m_AppliedDpiScale && l_UserScale == m_AppliedUserScale)
        {
            return;
        }

        // Windows already open grow with their text, as ImGui scales them itself when a viewport changes DPI
        if (m_AppliedDpiScale > 0.0f)
        {
            ImGui::ScaleWindowsInViewport(static_cast<ImGuiViewportP*>(ImGui::GetMainViewport()), (m_DpiScale * l_UserScale) / (m_AppliedDpiScale * m_AppliedUserScale));
        }

        m_AppliedDpiScale = m_DpiScale;
        m_AppliedUserScale = l_UserScale;

        ImGuiStyle l_Style;
        l_Style.FontSizeBase = c_FontSize;
        l_Style.FontScaleMain = l_UserScale;
        l_Style.FontScaleDpi = m_DpiScale;
        l_Style.ScaleAllSizes(m_DpiScale * l_UserScale);
        ImGui::GetStyle() = l_Style;

        TR_CORE_INFO("ImGui: scaled by {} for DPI and {} from ui.scale, so text is {} px", m_DpiScale, l_UserScale, std::round(c_FontSize * m_DpiScale * l_UserScale));
    }

    void ImGuiLayer::SaveSettings()
    {
        std::size_t l_Size = 0;
        const char* l_Settings = ImGui::SaveIniSettingsToMemory(&l_Size);

        const Expected<void, FileError> l_Result = FileSystem::WriteText(c_SettingsPath, std::string_view(l_Settings, l_Size));
        if (!l_Result)
        {
            TR_CORE_WARN("ImGui: could not save {}: {}", c_SettingsPath, ToString(l_Result.GetError()));
        }

        ImGui::GetIO().WantSaveIniSettings = false;
    }
}