#include "Trinity/Scripting/Interop.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Input/Input.hpp"
#include "Trinity/Renderer/DebugDraw.hpp"
#include "Trinity/Scene/SceneManager.hpp"

#include <limits>

namespace Trinity
{
    namespace Interop
    {
        namespace
        {
            // A number C# gives that is no key or button reads as one never pressed
            [[nodiscard]] KeyCode ToKey(Int32 key)
            {
                return key >= 0 && key <= std::numeric_limits<std::uint16_t>::max() ? static_cast<KeyCode>(key) : KeyCode::Count;
            }

            [[nodiscard]] MouseCode ToButton(Int32 button)
            {
                return button >= 0 && button <= std::numeric_limits<std::uint8_t>::max() ? static_cast<MouseCode>(button) : MouseCode::Count;
            }

            [[nodiscard]] DebugDepth ToDepth(Bool onTop)
            {
                return onTop ? DebugDepth::OnTop : DebugDepth::Test;
            }
        }

        // A level C# does not have is logged as an error, so nothing a script writes is lost
        Void Log_Write(LogLevel level, Utf8 text)
        {
            const LogLevel l_Level = level <= LogLevel::Critical ? level : LogLevel::Error;
            Log::Print(LogChannel::Client, l_Level, "{}", ToStringView(text));
        }

        Int32 Interop_Echo(Int32 value)
        {
            return value + 1;
        }

        Bool Input_IsKeyPressed(Int32 key)
        {
            return Input::IsKeyPressed(ToKey(key));
        }

        Bool Input_WasKeyPressed(Int32 key)
        {
            return Input::WasKeyPressed(ToKey(key));
        }

        Bool Input_WasKeyReleased(Int32 key)
        {
            return Input::WasKeyReleased(ToKey(key));
        }

        Bool Input_IsMouseButtonPressed(Int32 button)
        {
            return Input::IsMouseButtonPressed(ToButton(button));
        }

        Bool Input_WasMouseButtonPressed(Int32 button)
        {
            return Input::WasMouseButtonPressed(ToButton(button));
        }

        Bool Input_WasMouseButtonReleased(Int32 button)
        {
            return Input::WasMouseButtonReleased(ToButton(button));
        }

        Void Input_GetMousePosition(Vector2* position)
        {
            const MousePosition l_Position = Input::GetMousePosition();
            *position = { l_Position.X, l_Position.Y };
        }

        Void Input_GetMouseDelta(Vector2* delta)
        {
            *delta = Input::GetMouseDelta();
        }

        Void Input_GetMouseScroll(Vector2* scroll)
        {
            *scroll = Input::GetMouseScroll();
        }

        // Each draws nothing in Distribution, where DebugDraw is empty
        Void DebugDraw_Line(const Vector3* from, const Vector3* to, const Color* color, Bool onTop)
        {
            DebugDraw::Line(*from, *to, *color, ToDepth(onTop));
        }

        Void DebugDraw_Box(const Matrix4x4* transform, const Color* color, Bool onTop)
        {
            DebugDraw::Box(*transform, *color, ToDepth(onTop));
        }

        Void DebugDraw_AxisBox(const Vector3* center, const Vector3* halfExtents, const Color* color, Bool onTop)
        {
            DebugDraw::Box(*center, *halfExtents, *color, ToDepth(onTop));
        }

        Void DebugDraw_Circle(const Vector3* center, const Vector3* normal, Float radius, const Color* color, Bool onTop)
        {
            DebugDraw::Circle(*center, *normal, radius, *color, ToDepth(onTop));
        }

        Void DebugDraw_Sphere(const Vector3* center, Float radius, const Color* color, Bool onTop)
        {
            DebugDraw::Sphere(*center, radius, *color, ToDepth(onTop));
        }

        Void DebugDraw_Capsule(const Vector3* start, const Vector3* end, Float radius, const Color* color, Bool onTop)
        {
            DebugDraw::Capsule(*start, *end, radius, *color, ToDepth(onTop));
        }

        Void DebugDraw_Arrow(const Vector3* from, const Vector3* to, const Color* color, Bool onTop)
        {
            DebugDraw::Arrow(*from, *to, *color, ToDepth(onTop));
        }

        Void DebugDraw_Cross(const Vector3* center, Float size, const Color* color, Bool onTop)
        {
            DebugDraw::Cross(*center, size, *color, ToDepth(onTop));
        }

        Bool SceneManager_Load(UInt64 scene)
        {
            SceneManager* l_Manager = SceneManager::GetActive();
            if (l_Manager == nullptr)
            {
                TR_CORE_ERROR("Scripting: scene {} was not loaded, since no SceneManager runs this scene", UUID(scene));

                return false;
            }

            return l_Manager->Load(UUID(scene));
        }

        Bool SceneManager_LoadPath(Utf8 path)
        {
            SceneManager* l_Manager = SceneManager::GetActive();
            if (l_Manager == nullptr)
            {
                TR_CORE_ERROR("Scripting: scene {} was not loaded, since no SceneManager runs this scene", ToStringView(path));

                return false;
            }

            return l_Manager->Load(ToStringView(path));
        }

        Bool SceneManager_IsLoading()
        {
            const SceneManager* l_Manager = SceneManager::GetActive();

            return l_Manager != nullptr && l_Manager->IsLoading();
        }

        UInt64 SceneManager_GetScene()
        {
            const SceneManager* l_Manager = SceneManager::GetActive();

            return l_Manager != nullptr ? l_Manager->GetSceneAsset().GetValue() : 0;
        }
}
}