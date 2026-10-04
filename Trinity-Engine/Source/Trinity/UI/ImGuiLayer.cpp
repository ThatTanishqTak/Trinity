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
#include <optional>
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
        ConsoleVariable<bool> s_ViewportsVariable("ui.viewports", true, "Lets ImGui windows leave the main window as windows of their own, where the platform has windows");

        constexpr std::array<float, 4> c_ViewportClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

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

        // Every viewport's PlatformHandle is its Window, the main one included, so these serve both
        Window& GetViewportWindow(ImGuiViewport* viewport)
        {
            return *static_cast<Window*>(viewport->PlatformHandle);
        }

        ImGuiViewport* FindViewport(void* nativeHandle)
        {
            if (nativeHandle == nullptr)
            {
                return nullptr;
            }

            for (ImGuiViewport* it_Viewport : ImGui::GetPlatformIO().Viewports)
            {
                if (it_Viewport->PlatformHandleRaw == nativeHandle)
                {
                    return it_Viewport;
                }
            }

            return nullptr;
        }

        void ShowViewport(ImGuiViewport* viewport)
        {
            GetViewportWindow(viewport).Show((viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing) == 0);
        }

        void SetViewportPosition(ImGuiViewport* viewport, ImVec2 position)
        {
            GetViewportWindow(viewport).SetPosition({ static_cast<std::int32_t>(position.x), static_cast<std::int32_t>(position.y) });
        }

        ImVec2 GetViewportPosition(ImGuiViewport* viewport)
        {
            const WindowPosition l_Position = GetViewportWindow(viewport).GetPosition();

            return ImVec2(static_cast<float>(l_Position.X), static_cast<float>(l_Position.Y));
        }

        void SetViewportSize(ImGuiViewport* viewport, ImVec2 size)
        {
            GetViewportWindow(viewport).SetSize(static_cast<std::uint32_t>(std::max(size.x, 1.0f)), static_cast<std::uint32_t>(std::max(size.y, 1.0f)));
        }

        ImVec2 GetViewportSize(ImGuiViewport* viewport)
        {
            const Window& l_Window = GetViewportWindow(viewport);

            return ImVec2(static_cast<float>(l_Window.GetWidth()), static_cast<float>(l_Window.GetHeight()));
        }

        void FocusViewport(ImGuiViewport* viewport)
        {
            GetViewportWindow(viewport).Focus();
        }

        bool IsViewportFocused(ImGuiViewport* viewport)
        {
            return GetViewportWindow(viewport).IsFocused();
        }

        bool IsViewportMinimized(ImGuiViewport* viewport)
        {
            return GetViewportWindow(viewport).IsMinimized();
        }

        void SetViewportTitle(ImGuiViewport* viewport, const char* title)
        {
            GetViewportWindow(viewport).SetTitle(title);
        }

        void SetViewportAlpha(ImGuiViewport* viewport, float alpha)
        {
            GetViewportWindow(viewport).SetOpacity(alpha);
        }

        // ImGui changes these flags from frame to frame, such as NoInputs while a window is dragged, so the window under it can be found
        void UpdateViewport(ImGuiViewport* viewport)
        {
            Window& l_Window = GetViewportWindow(viewport);
            l_Window.SetTopMost((viewport->Flags & ImGuiViewportFlags_TopMost) != 0);
            l_Window.SetFocusOnClick((viewport->Flags & ImGuiViewportFlags_NoFocusOnClick) == 0);
            l_Window.SetMousePassthrough((viewport->Flags & ImGuiViewportFlags_NoInputs) != 0);
        }

        float GetViewportDpiScale(ImGuiViewport* viewport)
        {
            return GetViewportWindow(viewport).GetDpiScale();
        }
    }

    struct ImGuiLayer::ViewportWindow
    {
        Scope<Window> Platform;
        std::uint32_t Output = 0;
    };

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
        l_IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

        // Navigation alone does not take the keyboard, so layer shortcuts keep working while an ImGui window has focus
        l_IO.ConfigNavCaptureKeyboard = false;

        ImGuiPlatformIO& l_PlatformIO = ImGui::GetPlatformIO();
        l_PlatformIO.Platform_GetClipboardTextFn = &ReadClipboard;
        l_PlatformIO.Platform_SetClipboardTextFn = &WriteClipboard;
        l_PlatformIO.Platform_ClipboardUserData = &m_ClipboardText;

        LoadFonts();

        Application& l_Application = Application::Get();
        Window& l_Window = l_Application.GetWindow();
        m_DpiScale = l_Window.GetDpiScale();
        m_Renderer = CreateScope<ImGuiRenderer>(l_Application.GetDevice(), l_Application.GetRenderer().GetOutputFormat());

        // Floating windows are bare popups owned by the main window, with ImGui drawing their title bars, and no taskbar entries
        ImGuiViewport* l_MainViewport = ImGui::GetMainViewport();
        l_MainViewport->PlatformHandle = &l_Window;
        l_MainViewport->PlatformHandleRaw = l_Window.GetNativeHandle();
        l_IO.BackendPlatformUserData = this;
        l_IO.ConfigViewportsNoDecoration = true;
        l_IO.ConfigViewportsNoTaskBarIcon = true;

        UpdateMonitors();
        m_ViewportsSupported = l_Window.GetNativeHandle() != nullptr && !l_PlatformIO.Monitors.empty();
        if (m_ViewportsSupported)
        {
            l_IO.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_HasMouseHoveredViewport | ImGuiBackendFlags_RendererHasViewports;
            l_PlatformIO.Platform_CreateWindow = &ImGuiLayer::CreateViewportWindow;
            l_PlatformIO.Platform_DestroyWindow = &ImGuiLayer::DestroyViewportWindow;
            l_PlatformIO.Platform_ShowWindow = &ShowViewport;
            l_PlatformIO.Platform_SetWindowPos = &SetViewportPosition;
            l_PlatformIO.Platform_GetWindowPos = &GetViewportPosition;
            l_PlatformIO.Platform_SetWindowSize = &SetViewportSize;
            l_PlatformIO.Platform_GetWindowSize = &GetViewportSize;
            l_PlatformIO.Platform_SetWindowFocus = &FocusViewport;
            l_PlatformIO.Platform_GetWindowFocus = &IsViewportFocused;
            l_PlatformIO.Platform_GetWindowMinimized = &IsViewportMinimized;
            l_PlatformIO.Platform_SetWindowTitle = &SetViewportTitle;
            l_PlatformIO.Platform_SetWindowAlpha = &SetViewportAlpha;
            l_PlatformIO.Platform_UpdateWindow = &UpdateViewport;
            l_PlatformIO.Platform_GetWindowDpiScale = &GetViewportDpiScale;
            l_PlatformIO.Renderer_CreateWindow = &ImGuiLayer::CreateViewportOutput;
            l_PlatformIO.Renderer_DestroyWindow = &ImGuiLayer::DestroyViewportOutput;

            // Set before the first frame, as ImGui asks, so imgui.ini keeps the positions of floating windows
            UpdateViewports();
        }

        TR_CORE_INFO("ImGui: {} context created, with multi-viewport {}", IMGUI_VERSION, m_ViewportsSupported ? "available through ui.viewports" : "off, since this platform has no windows");
    }

    // Settings never loaded are not saved either, so a run with no frames leaves imgui.ini as it was
    void ImGuiLayer::OnDetach()
    {
        if (m_SettingsLoaded)
        {
            SaveSettings();
        }

        ImGui::DestroyPlatformWindows();
        ImGui::GetIO().BackendPlatformUserData = nullptr;
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

        if (!m_SettingsLoaded)
        {
            LoadSettings();
        }

        Application& l_Application = Application::Get();
        Window& l_Window = l_Application.GetWindow();
        UpdateCursor(l_Window);
        UpdateViewports();
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

        // Creates, moves and destroys the windows of floating ImGui windows. The Renderer draws them in its frame, through the outputs those windows were given
        if (m_ViewportsSupported)
        {
            ImGui::UpdatePlatformWindows();
        }

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
        Window& l_MainWindow = Application::Get().GetWindow();

        // Positions are relative to the main window, and with multi-viewport ImGui wants them on the screen
        const auto a_OnMouseMoved = [this, &l_IO, &l_MainWindow](MouseMovedEvent& moved)
        {
            m_MouseWindow = m_EventSource != nullptr ? m_EventSource : &l_MainWindow;

            const WindowPosition l_Origin = (l_IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0 ? l_MainWindow.GetPosition() : WindowPosition{};
            l_IO.AddMousePosEvent(moved.GetX() + static_cast<float>(l_Origin.X), moved.GetY() + static_cast<float>(l_Origin.Y));

            return l_IO.WantCaptureMouse;
        };

        // The mouse may already be in a floating window, whose move arrived first
        const auto a_OnMouseLeft = [this, &l_IO, &l_MainWindow](MouseLeftEvent&)
        {
            if (m_MouseWindow == &l_MainWindow)
            {
                m_MouseWindow = nullptr;
                l_IO.AddMousePosEvent(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
            }

            return false;
        };

        EventDispatcher l_Dispatcher(event);
        l_Dispatcher.Dispatch<MouseMovedEvent>(a_OnMouseMoved);
        l_Dispatcher.Dispatch<MouseLeftEvent>(a_OnMouseLeft);
        l_Dispatcher.Dispatch<MouseButtonPressedEvent>([&l_IO](MouseButtonPressedEvent& pressed) { AddMouseButtonEvent(l_IO, pressed.GetMouseButton(), true); return l_IO.WantCaptureMouse; });
        l_Dispatcher.Dispatch<MouseButtonReleasedEvent>([&l_IO](MouseButtonReleasedEvent& released) { AddMouseButtonEvent(l_IO, released.GetMouseButton(), false); return false; });

        // Trinity scrolls right with a positive X, ImGui with a negative one
        l_Dispatcher.Dispatch<MouseScrolledEvent>([&l_IO](MouseScrolledEvent& scrolled) { l_IO.AddMouseWheelEvent(-scrolled.GetXOffset(), scrolled.GetYOffset()); return l_IO.WantCaptureMouse; });

        l_Dispatcher.Dispatch<KeyPressedEvent>([&l_IO](KeyPressedEvent& pressed) { AddKeyEvent(l_IO, pressed.GetKeyCode(), true); return l_IO.WantCaptureKeyboard; });
        l_Dispatcher.Dispatch<KeyReleasedEvent>([&l_IO](KeyReleasedEvent& released) { AddKeyEvent(l_IO, released.GetKeyCode(), false); return false; });
        l_Dispatcher.Dispatch<KeyTypedEvent>([&l_IO](KeyTypedEvent& typed) { l_IO.AddInputCharacter(static_cast<unsigned int>(typed.GetCodepoint())); return l_IO.WantCaptureKeyboard; });
        l_Dispatcher.Dispatch<WindowFocusEvent>([&l_IO](WindowFocusEvent&) { l_IO.AddFocusEvent(true); return false; });
        l_Dispatcher.Dispatch<WindowLostFocusEvent>([&l_IO](WindowLostFocusEvent&) { l_IO.AddFocusEvent(false); return false; });
        l_Dispatcher.Dispatch<WindowDpiChangedEvent>([this](WindowDpiChangedEvent& changed) { m_DpiScale = changed.GetScale(); m_MonitorsChanged = true; return false; });
        l_Dispatcher.Dispatch<MonitorsChangedEvent>([this](MonitorsChangedEvent&) { m_MonitorsChanged = true; return false; });
    }

    // Window events become ImGui's requests for that viewport. Input goes the way the main window's does, through Application, so Input and the layers see it too
    void ImGuiLayer::OnViewportEvent(ImGuiViewport& viewport, Event& event)
    {
        ImGuiIO& l_IO = ImGui::GetIO();
        Window& l_Window = GetViewportWindow(&viewport);

        EventDispatcher l_Dispatcher(event);
        l_Dispatcher.Dispatch<WindowCloseEvent>([&viewport](WindowCloseEvent&) { viewport.PlatformRequestClose = true; return true; });
        l_Dispatcher.Dispatch<WindowMovedEvent>([&viewport](WindowMovedEvent&) { viewport.PlatformRequestMove = true; return true; });
        l_Dispatcher.Dispatch<WindowResizeEvent>([&viewport](WindowResizeEvent&) { viewport.PlatformRequestResize = true; return true; });
        l_Dispatcher.Dispatch<WindowFocusEvent>([&l_IO](WindowFocusEvent&) { l_IO.AddFocusEvent(true); return true; });
        l_Dispatcher.Dispatch<WindowLostFocusEvent>([&l_IO](WindowLostFocusEvent&) { l_IO.AddFocusEvent(false); return true; });
        l_Dispatcher.Dispatch<WindowDpiChangedEvent>([this](WindowDpiChangedEvent&) { m_MonitorsChanged = true; return true; });
        l_Dispatcher.Dispatch<MonitorsChangedEvent>([this](MonitorsChangedEvent&) { m_MonitorsChanged = true; return true; });
        l_Dispatcher.Dispatch<MouseLeftEvent>([this, &l_IO, &l_Window](MouseLeftEvent&)
        {
            if (m_MouseWindow == &l_Window)
            {
                m_MouseWindow = nullptr;
                l_IO.AddMousePosEvent(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
            }

            return true;
        });

        if (event.Handled || !event.IsInCategory(EventCategoryInput))
        {
            return;
        }

        m_EventSource = &l_Window;
        if (event.GetEventType() == EventType::MouseMoved)
        {
            const MouseMovedEvent& l_Moved = static_cast<const MouseMovedEvent&>(event);
            const WindowPosition l_From = l_Window.GetPosition();
            const WindowPosition l_To = Application::Get().GetWindow().GetPosition();

            MouseMovedEvent l_Relative(l_Moved.GetX() + static_cast<float>(l_From.X - l_To.X), l_Moved.GetY() + static_cast<float>(l_From.Y - l_To.Y));
            Application::Get().OnEvent(l_Relative);
        }
        else
        {
            Application::Get().OnEvent(event);
        }

        m_EventSource = nullptr;
    }

    // Turns multi-viewport on or off as ui.viewports asks, refreshes the monitors, and gives ImGui the mouse as Windows sees it
    void ImGuiLayer::UpdateViewports()
    {
        if (!m_ViewportsSupported)
        {
            return;
        }

        ImGuiIO& l_IO = ImGui::GetIO();
        const bool l_Enable = s_ViewportsVariable.Get();
        if (l_Enable != ((l_IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0))
        {
            l_IO.ConfigFlags ^= ImGuiConfigFlags_ViewportsEnable;

            // ImGui follows each viewport's DPI itself while viewports are on, and the style is rebuilt to hand it over
            l_IO.ConfigDpiScaleFonts = l_Enable;
            l_IO.ConfigDpiScaleViewports = l_Enable;
            m_AppliedDpiScale = 0.0f;

            TR_CORE_INFO("ImGui: multi-viewport {}", l_Enable ? "on" : "off");
        }

        if (m_MonitorsChanged)
        {
            UpdateMonitors();
        }

        if (!l_Enable)
        {
            return;
        }

        // Fills in while the mouse is over none of these windows, such as when a floating window is dragged faster than it follows
        const std::optional<Platform::ScreenPoint> l_Cursor = Platform::GetCursorPosition();
        if (l_Cursor && m_MouseWindow == nullptr && FindViewport(Platform::GetFocusedWindow()) != nullptr)
        {
            l_IO.AddMousePosEvent(static_cast<float>(l_Cursor->X), static_cast<float>(l_Cursor->Y));
        }

        // A window being dragged lets the mouse pass through, so this finds the one under it to dock into
        const ImGuiViewport* l_Hovered = l_Cursor ? FindViewport(Platform::GetWindowAt(*l_Cursor)) : nullptr;
        l_IO.AddMouseViewportEvent(l_Hovered != nullptr ? l_Hovered->ID : 0);
    }

    // Primary first, as ImGui expects
    void ImGuiLayer::UpdateMonitors()
    {
        ImVector<ImGuiPlatformMonitor>& l_Monitors = ImGui::GetPlatformIO().Monitors;
        l_Monitors.resize(0);

        for (const Platform::MonitorInfo& it_Monitor : Platform::GetMonitors())
        {
            ImGuiPlatformMonitor l_Monitor;
            l_Monitor.MainPos = ImVec2(static_cast<float>(it_Monitor.Area.X), static_cast<float>(it_Monitor.Area.Y));
            l_Monitor.MainSize = ImVec2(static_cast<float>(it_Monitor.Area.Width), static_cast<float>(it_Monitor.Area.Height));
            l_Monitor.WorkPos = ImVec2(static_cast<float>(it_Monitor.WorkArea.X), static_cast<float>(it_Monitor.WorkArea.Y));
            l_Monitor.WorkSize = ImVec2(static_cast<float>(it_Monitor.WorkArea.Width), static_cast<float>(it_Monitor.WorkArea.Height));
            l_Monitor.DpiScale = it_Monitor.DpiScale;
            l_Monitors.push_back(l_Monitor);
        }

        m_MonitorsChanged = false;
    }

    // Hidden until ImGui shows it, with ImGui's cursor, and with its input routed through OnViewportEvent
    void ImGuiLayer::CreateViewportWindow(ImGuiViewport* viewport)
    {
        ImGuiLayer& l_Layer = *static_cast<ImGuiLayer*>(ImGui::GetIO().BackendPlatformUserData);
        Window& l_MainWindow = Application::Get().GetWindow();

        WindowSpecification l_Specification;
        l_Specification.Title = "ImGui";
        l_Specification.Width = static_cast<std::uint32_t>(std::max(viewport->Size.x, 1.0f));
        l_Specification.Height = static_cast<std::uint32_t>(std::max(viewport->Size.y, 1.0f));
        l_Specification.Position = WindowPosition{ static_cast<std::int32_t>(viewport->Pos.x), static_cast<std::int32_t>(viewport->Pos.y) };
        l_Specification.Owner = viewport->ParentViewport != nullptr ? static_cast<Window*>(viewport->ParentViewport->PlatformHandle) : nullptr;
        l_Specification.Decorated = (viewport->Flags & ImGuiViewportFlags_NoDecoration) == 0;
        l_Specification.TaskbarIcon = (viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) == 0;
        l_Specification.TopMost = (viewport->Flags & ImGuiViewportFlags_TopMost) != 0;
        l_Specification.Visible = false;
        l_Specification.Headless = l_MainWindow.GetNativeHandle() == nullptr;

        Scope<ViewportWindow> l_Viewport = CreateScope<ViewportWindow>();
        l_Viewport->Platform = Window::Create(l_Specification);
        l_Viewport->Platform->SetEventCallback([&l_Layer, viewport](Event& event) { l_Layer.OnViewportEvent(*viewport, event); });
        l_Viewport->Platform->SetCursorShape(ToCursorShape(l_Layer.m_Cursor));

        viewport->PlatformUserData = l_Viewport.get();
        viewport->PlatformHandle = l_Viewport->Platform.get();
        viewport->PlatformHandleRaw = l_Viewport->Platform->GetNativeHandle();
        viewport->PlatformRequestResize = false;

        UpdateViewport(viewport);
        l_Layer.m_ViewportWindows.push_back(std::move(l_Viewport));
    }

    // ImGui calls this for the main viewport too, whose window belongs to Application
    void ImGuiLayer::DestroyViewportWindow(ImGuiViewport* viewport)
    {
        ImGuiLayer& l_Layer = *static_cast<ImGuiLayer*>(ImGui::GetIO().BackendPlatformUserData);
        if (const ViewportWindow* l_Viewport = static_cast<const ViewportWindow*>(viewport->PlatformUserData))
        {
            if (l_Layer.m_MouseWindow == l_Viewport->Platform.get())
            {
                l_Layer.m_MouseWindow = nullptr;
            }

            std::erase_if(l_Layer.m_ViewportWindows, [l_Viewport](const Scope<ViewportWindow>& it_Viewport) { return it_Viewport.get() == l_Viewport; });
        }

        viewport->PlatformUserData = nullptr;
        viewport->PlatformHandle = nullptr;
        viewport->PlatformHandleRaw = nullptr;
    }

    void ImGuiLayer::CreateViewportOutput(ImGuiViewport* viewport)
    {
        ImGuiLayer& l_Layer = *static_cast<ImGuiLayer*>(ImGui::GetIO().BackendPlatformUserData);
        ViewportWindow& l_Viewport = *static_cast<ViewportWindow*>(viewport->PlatformUserData);

        l_Viewport.Output = Application::Get().GetRenderer().AddOutput(*l_Viewport.Platform, c_ViewportClearColor, [&l_Layer, viewport](RHI::CommandList& commands, std::uint32_t, std::uint32_t)
        {
            if (const ImDrawData* l_DrawData = viewport->DrawData)
            {
                l_Layer.m_Renderer->Render(commands, *l_DrawData);
            }
        });
    }

    void ImGuiLayer::DestroyViewportOutput(ImGuiViewport* viewport)
    {
        if (ViewportWindow* l_Viewport = static_cast<ViewportWindow*>(viewport->PlatformUserData))
        {
            Application::Get().GetRenderer().RemoveOutput(l_Viewport->Output);
            l_Viewport->Output = 0;
        }
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
            for (const Scope<ViewportWindow>& it_Viewport : m_ViewportWindows)
            {
                it_Viewport->Platform->SetCursorShape(ToCursorShape(l_Cursor));
            }
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

        // Windows already open grow with their text. With multi-viewport on, ImGui scales them itself for DPI, so only ui.scale is left here
        const bool l_ImGuiFollowsDpi = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
        const float l_Ratio = m_AppliedDpiScale > 0.0f ? (l_ImGuiFollowsDpi ? l_UserScale / m_AppliedUserScale : (m_DpiScale * l_UserScale) / (m_AppliedDpiScale * m_AppliedUserScale)) : 1.0f;
        if (l_Ratio != 1.0f)
        {
            for (ImGuiViewport* it_Viewport : ImGui::GetPlatformIO().Viewports)
            {
                ImGui::ScaleWindowsInViewport(static_cast<ImGuiViewportP*>(it_Viewport), l_Ratio);
            }
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

    // Read before the first frame rather than on attach, so layers attached after this one have added their own settings handlers by then
    void ImGuiLayer::LoadSettings()
    {
        const Expected<std::string, FileError> l_Settings = FileSystem::ReadText(c_SettingsPath);
        if (l_Settings)
        {
            ImGui::LoadIniSettingsFromMemory(l_Settings->data(), l_Settings->size());
        }

        m_SettingsLoaded = true;
        TR_CORE_INFO("ImGui: settings {} {}", l_Settings ? "loaded from" : "to be saved in", c_SettingsPath);
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