#include "Trinity/Scene/SceneSerializer.hpp"

#include "Trinity/Scene/LiveScenes.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Prefab.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace Trinity
{
    namespace
    {
        constexpr std::string_view c_EntityKey = "Entity";
        constexpr std::string_view c_ParentKey = "Parent";
        constexpr std::string_view c_InstanceKey = "Instance";
        constexpr std::string_view c_PrefabKey = "Prefab";
        constexpr std::string_view c_OverridesKey = "Overrides";
        constexpr std::string_view c_AddedComponentsKey = "AddedComponents";
        constexpr std::string_view c_RemovedComponentsKey = "RemovedComponents";
        constexpr std::string_view c_RemovedEntitiesKey = "RemovedEntities";
        constexpr std::string_view c_MissingPrefabName = "Missing Prefab";
        // Between the keys and indices of an override's path, such as Scripts/0/Fields/Health
        constexpr char c_PathSeparator = '/';

        using MigrationFunction = void (*)(YAML::Node& root);

        // Entry N - 1 turns a format N file into format N + 1. Format 2 added prefab instances, so a format 1 file reads as it is
        constexpr std::array<MigrationFunction, SceneSerializer::c_FormatVersion - 1> c_Migrations{ [](YAML::Node&) {} };

        struct State
        {
            std::vector<ComponentSerializer, TaggedAllocator<ComponentSerializer, MemoryTag::Engine>> Serializers;
            std::vector<Scene*, TaggedAllocator<Scene*, MemoryTag::Engine>> Scenes;
            SceneSerializer::PrefabResolver Resolver;
        };

        State* s_State = nullptr;

        template<typename T>
        std::string FormatFloat(T value)
        {
            std::array<char, 32> l_Buffer{};
            const std::to_chars_result l_Result = std::to_chars(l_Buffer.data(), l_Buffer.data() + l_Buffer.size(), value);

            return std::string(l_Buffer.data(), l_Result.ptr);
        }

        template<typename T>
        bool ParseNumber(std::string_view text, T& value)
        {
            T l_Value{};
            const std::from_chars_result l_Result = std::from_chars(text.data(), text.data() + text.size(), l_Value);
            if (l_Result.ec != std::errc() || l_Result.ptr != text.data() + text.size())
            {
                return false;
            }

            value = l_Value;

            return true;
        }

        std::string FormatFloats(std::span<const float> values)
        {
            std::string l_Text = "[";
            for (std::size_t it_Index = 0; it_Index < values.size(); ++it_Index)
            {
                l_Text += it_Index == 0 ? "" : ", ";
                l_Text += FormatFloat(values[it_Index]);
            }

            return l_Text + "]";
        }

        // Plain only when no YAML reader could take the text for a number, a boolean, a null or syntax. Everything else is double-quoted
        std::string FormatString(std::string_view text)
        {
            constexpr std::array<std::string_view, 9> c_Keywords{ "true", "false", "yes", "no", "on", "off", "null", "y", "n" };

            const auto a_IsLetter = [](char character) { return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_'; };
            const auto a_IsPlain = [&a_IsLetter](char character) { return a_IsLetter(character) || (character >= '0' && character <= '9') || character == ' ' || character == '.' || character == '-' || character == '/' || character == '(' || character == ')'; };

            std::string l_Lower(text);
            std::ranges::transform(l_Lower, l_Lower.begin(), [](char character) { return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character; });

            if (!text.empty() && a_IsLetter(text.front()) && text.back() != ' ' && std::ranges::all_of(text, a_IsPlain) && std::ranges::find(c_Keywords, l_Lower) == c_Keywords.end())
            {
                return std::string(text);
            }

            std::string l_Quoted = "\"";
            for (const char it_Character : text)
            {
                const unsigned char l_Byte = static_cast<unsigned char>(it_Character);
                if (it_Character == '"' || it_Character == '\\')
                {
                    l_Quoted += '\\';
                    l_Quoted += it_Character;
                }
                else if (it_Character == '\n')
                {
                    l_Quoted += "\\n";
                }
                else if (it_Character == '\t')
                {
                    l_Quoted += "\\t";
                }
                else if (l_Byte < 0x20 || l_Byte == 0x7F)
                {
                    l_Quoted += std::format("\\x{:02x}", l_Byte);
                }
                else
                {
                    l_Quoted += it_Character;
                }
            }

            return l_Quoted + "\"";
        }

        // Every line of the text, each indented, on lines of their own after the key
        void AppendIndented(std::string& output, std::string_view text, std::uint32_t indent)
        {
            std::size_t l_Start = 0;
            while (l_Start <= text.size())
            {
                const std::size_t l_End = std::min(text.find('\n', l_Start), text.size());
                output += '\n';
                output.append(indent, ' ');
                output.append(text.substr(l_Start, l_End - l_Start));
                l_Start = l_End + 1;
            }
        }

        // A preserved component as the scene file had it: a block map or sequence on indented lines, anything else after the key
        void AppendUnknown(std::string& output, std::string_view yaml, std::uint32_t indent)
        {
            const YAML::Node l_Node = YAML::Load(std::string(yaml));
            if (l_Node.IsNull())
            {
                return;
            }

            if ((l_Node.IsMap() || l_Node.IsSequence()) && l_Node.size() != 0 && l_Node.Style() != YAML::EmitterStyle::Flow)
            {
                AppendIndented(output, yaml, indent);

                return;
            }

            output += ' ';
            output += yaml;
        }

        bool ParseFloatSequence(const YAML::Node& node, std::span<float> values)
        {
            if (!node.IsSequence() || node.size() != values.size())
            {
                return false;
            }

            std::array<float, 4> l_Parsed{};
            for (std::size_t it_Index = 0; it_Index < values.size(); ++it_Index)
            {
                if (!node[it_Index].IsScalar() || !ParseNumber(node[it_Index].Scalar(), l_Parsed[it_Index]))
                {
                    return false;
                }
            }

            std::copy_n(l_Parsed.begin(), values.size(), values.begin());

            return true;
        }

        std::string ToHex(UUID uuid)
        {
            const std::array<char, 16> l_Hex = uuid.ToHex();

            return std::string(l_Hex.data(), l_Hex.size());
        }

        // A list's element as a value of its type is written on its own, so a list of vectors is a sequence of sequences
        std::string FormatElement(const ScriptFieldValue& element)
        {
            return std::visit([](const auto& data) -> std::string
            {
                using Type = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<Type, bool>)
                {
                    return data ? "true" : "false";
                }
                else if constexpr (std::is_integral_v<Type>)
                {
                    return std::to_string(data);
                }
                else if constexpr (std::is_floating_point_v<Type>)
                {
                    return FormatFloat(data);
                }
                else if constexpr (std::is_same_v<Type, glm::vec2>)
                {
                    return FormatFloats(std::array{ data.x, data.y });
                }
                else if constexpr (std::is_same_v<Type, glm::vec3>)
                {
                    return FormatFloats(std::array{ data.x, data.y, data.z });
                }
                else if constexpr (std::is_same_v<Type, glm::vec4> || std::is_same_v<Type, glm::quat>)
                {
                    return FormatFloats(std::array{ data.x, data.y, data.z, data.w });
                }
                else if constexpr (std::is_same_v<Type, ScriptString>)
                {
                    return FormatString(std::string_view(data));
                }
                else if constexpr (std::is_same_v<Type, UUID>)
                {
                    return ToHex(data);
                }
                else
                {
                    // An element is never a list
                    return "[]";
                }
            }, element.GetStorage());
        }

        entt::id_type GetPoolId(std::string_view name)
        {
            return entt::hashed_string::value(name.data(), name.size());
        }

        const ComponentSerializer* FindSerializer(std::string_view name)
        {
            const auto a_Found = std::ranges::find(s_State->Serializers, name, &ComponentSerializer::Name);

            return a_Found != s_State->Serializers.end() ? &*a_Found : nullptr;
        }

        void SaveTag(const TagComponent& component, ComponentWriter& writer)
        {
            writer.WriteValue(component.Tag);
        }

        void LoadTag(TagComponent& component, const ComponentReader& reader)
        {
            std::string l_Tag;
            if (reader.ReadValue(l_Tag))
            {
                component.Tag = l_Tag;
            }
        }

        void SaveTransform(const TransformComponent& component, ComponentWriter& writer)
        {
            writer.Write("Position", component.Position);
            writer.Write("Rotation", component.Rotation);
            writer.Write("Scale", component.Scale);
        }

        void LoadTransform(TransformComponent& component, const ComponentReader& reader)
        {
            reader.Read("Position", component.Position);
            reader.Read("Rotation", component.Rotation);
            reader.Read("Scale", component.Scale);
        }

        std::string_view GetProjectionName(CameraProjection projection)
        {
            return projection == CameraProjection::Perspective ? "Perspective" : "Orthographic";
        }

        constexpr std::array<std::pair<LightType, std::string_view>, 3> c_LightTypeNames{ { { LightType::Directional, "Directional" },{ LightType::Point, "Point" },{ LightType::Spot, "Spot" } } };

        std::string_view GetLightTypeName(LightType type)
        {
            const auto a_Found = std::ranges::find(c_LightTypeNames, type, &std::pair<LightType, std::string_view>::first);

            return a_Found != c_LightTypeNames.end() ? a_Found->second : "Directional";
        }

        void SaveCamera(const CameraComponent& component, ComponentWriter& writer)
        {
            writer.Write("Projection", GetProjectionName(component.Projection));
            writer.Write("OrthographicSize", component.OrthographicSize);
            writer.Write("Near", component.Near);
            writer.Write("Far", component.Far);
            writer.Write("FieldOfView", component.FieldOfView);
            writer.Write("PerspectiveNear", component.PerspectiveNear);
            writer.Write("Primary", component.Primary);
            writer.Write("ExposureEV100", component.ExposureEV100);
            writer.Write("Tonemapper", ToString(component.Tonemap));
        }

        // A scene saved before cameras had a projection loads them orthographic, as they were drawn then
        void LoadCamera(CameraComponent& component, const ComponentReader& reader)
        {
            std::string l_Projection;
            component.Projection = reader.Read("Projection", l_Projection) && l_Projection == GetProjectionName(CameraProjection::Perspective) ? CameraProjection::Perspective : CameraProjection::Orthographic;
            reader.Read("OrthographicSize", component.OrthographicSize);
            reader.Read("Near", component.Near);
            reader.Read("Far", component.Far);
            reader.Read("FieldOfView", component.FieldOfView);
            reader.Read("PerspectiveNear", component.PerspectiveNear);
            reader.Read("Primary", component.Primary);
            reader.Read("ExposureEV100", component.ExposureEV100);

            // A scene saved before cameras had a tone curve, or naming one this build lacks, shows none
            std::string l_Tonemapper;
            component.Tonemap = reader.Read("Tonemapper", l_Tonemapper) ? ParseTonemapper(l_Tonemapper).value_or(Tonemapper::None) : Tonemapper::None;
        }

        void SaveSpriteRenderer(const SpriteRendererComponent& component, ComponentWriter& writer)
        {
            writer.Write("Texture", component.Texture);
            writer.Write("Tint", component.Tint);
            writer.Write("FlipX", component.FlipX);
            writer.Write("FlipY", component.FlipY);
            writer.Write("UVRect", component.UVRect);
            writer.Write("SortingLayer", component.SortingLayer);
            writer.Write("OrderInLayer", component.OrderInLayer);
        }

        void LoadSpriteRenderer(SpriteRendererComponent& component, const ComponentReader& reader)
        {
            reader.Read("Texture", component.Texture);
            reader.Read("Tint", component.Tint);
            reader.Read("FlipX", component.FlipX);
            reader.Read("FlipY", component.FlipY);
            reader.Read("UVRect", component.UVRect);
            reader.Read("SortingLayer", component.SortingLayer);
            reader.Read("OrderInLayer", component.OrderInLayer);
        }

        void SaveMeshRenderer(const MeshRendererComponent& component, ComponentWriter& writer)
        {
            writer.Write("Mesh", component.Mesh);
            writer.Write("Materials", std::span<const UUID>(component.Materials));
            writer.Write("CastShadows", component.CastShadows);
        }

        void LoadMeshRenderer(MeshRendererComponent& component, const ComponentReader& reader)
        {
            reader.Read("Mesh", component.Mesh);
            reader.Read("Materials", component.Materials);
            reader.Read("CastShadows", component.CastShadows);
        }

        void SaveLight(const LightComponent& component, ComponentWriter& writer)
        {
            writer.Write("Type", GetLightTypeName(component.Type));
            writer.Write("Color", component.Color);
            writer.Write("Intensity", component.Intensity);
            writer.Write("Range", component.Range);
            writer.Write("InnerConeAngle", component.InnerConeAngle);
            writer.Write("OuterConeAngle", component.OuterConeAngle);
            writer.Write("CastShadows", component.CastShadows);
        }

        // A light saved before there were other types, or naming one this build lacks, loads as a directional light
        void LoadLight(LightComponent& component, const ComponentReader& reader)
        {
            std::string l_Type;
            const bool l_HasType = reader.Read("Type", l_Type);
            const auto a_Found = std::ranges::find(c_LightTypeNames, std::string_view(l_Type), &std::pair<LightType, std::string_view>::second);
            component.Type = l_HasType && a_Found != c_LightTypeNames.end() ? a_Found->first : LightType::Directional;
            reader.Read("Color", component.Color);
            reader.Read("Intensity", component.Intensity);
            reader.Read("Range", component.Range);
            reader.Read("InnerConeAngle", component.InnerConeAngle);
            reader.Read("OuterConeAngle", component.OuterConeAngle);
            reader.Read("CastShadows", component.CastShadows);
        }

        void SaveEnvironment(const EnvironmentComponent& component, ComponentWriter& writer)
        {
            writer.Write("Environment", component.Environment);
            writer.Write("Intensity", component.Intensity);
            writer.Write("Rotation", component.Rotation);
        }

        void LoadEnvironment(EnvironmentComponent& component, const ComponentReader& reader)
        {
            reader.Read("Environment", component.Environment);
            reader.Read("Intensity", component.Intensity);
            reader.Read("Rotation", component.Rotation);
        }

        constexpr std::array<std::pair<BodyMotion, std::string_view>, 3> c_BodyMotionNames{ { { BodyMotion::Static, "Static" },{ BodyMotion::Kinematic, "Kinematic" },{ BodyMotion::Dynamic, "Dynamic" } } };

        // Each locked axis is a line of its own, so a diff shows which one changed
        void WriteAxes(ComponentWriter& writer, std::string_view key, const glm::bvec3& axes)
        {
            writer.Write(std::format("{}X", key), axes.x);
            writer.Write(std::format("{}Y", key), axes.y);
            writer.Write(std::format("{}Z", key), axes.z);
        }

        void ReadAxes(const ComponentReader& reader, std::string_view key, glm::bvec3& axes)
        {
            reader.Read(std::format("{}X", key), axes.x);
            reader.Read(std::format("{}Y", key), axes.y);
            reader.Read(std::format("{}Z", key), axes.z);
        }

        void SaveRigidBody(const RigidBodyComponent& component, ComponentWriter& writer)
        {
            const auto a_Found = std::ranges::find(c_BodyMotionNames, component.Motion, &std::pair<BodyMotion, std::string_view>::first);
            writer.Write("Motion", a_Found != c_BodyMotionNames.end() ? a_Found->second : std::string_view("Dynamic"));
            writer.Write("Mass", component.Mass);
            writer.Write("LinearDamping", component.LinearDamping);
            writer.Write("AngularDamping", component.AngularDamping);
            writer.Write("GravityFactor", component.GravityFactor);
            writer.Write("ContinuousCollision", component.ContinuousCollision);
            WriteAxes(writer, "LockPosition", component.LockPosition);
            WriteAxes(writer, "LockRotation", component.LockRotation);
            writer.Write("InitialLinearVelocity", component.InitialLinearVelocity);
            writer.Write("InitialAngularVelocity", component.InitialAngularVelocity);
            writer.Write("StartAsleep", component.StartAsleep);
            writer.Write("CanSleep", component.CanSleep);
        }

        // A motion this build lacks loads as dynamic
        void LoadRigidBody(RigidBodyComponent& component, const ComponentReader& reader)
        {
            std::string l_Motion;
            reader.Read("Motion", l_Motion);
            const auto a_Found = std::ranges::find(c_BodyMotionNames, std::string_view(l_Motion), &std::pair<BodyMotion, std::string_view>::second);
            component.Motion = a_Found != c_BodyMotionNames.end() ? a_Found->first : BodyMotion::Dynamic;
            reader.Read("Mass", component.Mass);
            reader.Read("LinearDamping", component.LinearDamping);
            reader.Read("AngularDamping", component.AngularDamping);
            reader.Read("GravityFactor", component.GravityFactor);
            reader.Read("ContinuousCollision", component.ContinuousCollision);
            ReadAxes(reader, "LockPosition", component.LockPosition);
            ReadAxes(reader, "LockRotation", component.LockRotation);
            reader.Read("InitialLinearVelocity", component.InitialLinearVelocity);
            reader.Read("InitialAngularVelocity", component.InitialAngularVelocity);
            reader.Read("StartAsleep", component.StartAsleep);
            reader.Read("CanSleep", component.CanSleep);
        }

        // Written before each collider's own fields
        void SaveColliderSettings(const ColliderSettings& collider, ComponentWriter& writer)
        {
            writer.Write("Offset", collider.Offset);
            writer.Write("OffsetRotation", collider.OffsetRotation);
            writer.Write("Friction", collider.Friction);
            writer.Write("Restitution", collider.Restitution);
            writer.Write("Trigger", collider.Trigger);
            writer.Write("Layer", collider.Layer);
        }

        // A layer past the last there can be is taken as the first
        void LoadColliderSettings(ColliderSettings& collider, const ComponentReader& reader)
        {
            reader.Read("Offset", collider.Offset);
            reader.Read("OffsetRotation", collider.OffsetRotation);
            reader.Read("Friction", collider.Friction);
            reader.Read("Restitution", collider.Restitution);
            reader.Read("Trigger", collider.Trigger);
            reader.Read("Layer", collider.Layer);
            collider.Layer = collider.Layer < c_MaxCollisionLayers ? collider.Layer : 0;
        }

        void SaveBoxCollider(const BoxColliderComponent& component, ComponentWriter& writer)
        {
            SaveColliderSettings(component, writer);
            writer.Write("HalfExtents", component.HalfExtents);
        }

        void LoadBoxCollider(BoxColliderComponent& component, const ComponentReader& reader)
        {
            LoadColliderSettings(component, reader);
            reader.Read("HalfExtents", component.HalfExtents);
        }

        void SaveSphereCollider(const SphereColliderComponent& component, ComponentWriter& writer)
        {
            SaveColliderSettings(component, writer);
            writer.Write("Radius", component.Radius);
        }

        void LoadSphereCollider(SphereColliderComponent& component, const ComponentReader& reader)
        {
            LoadColliderSettings(component, reader);
            reader.Read("Radius", component.Radius);
        }

        void SaveCapsuleCollider(const CapsuleColliderComponent& component, ComponentWriter& writer)
        {
            SaveColliderSettings(component, writer);
            writer.Write("Radius", component.Radius);
            writer.Write("Height", component.Height);
        }

        void LoadCapsuleCollider(CapsuleColliderComponent& component, const ComponentReader& reader)
        {
            LoadColliderSettings(component, reader);
            reader.Read("Radius", component.Radius);
            reader.Read("Height", component.Height);
        }

        void SaveCylinderCollider(const CylinderColliderComponent& component, ComponentWriter& writer)
        {
            SaveColliderSettings(component, writer);
            writer.Write("Radius", component.Radius);
            writer.Write("Height", component.Height);
        }

        void LoadCylinderCollider(CylinderColliderComponent& component, const ComponentReader& reader)
        {
            LoadColliderSettings(component, reader);
            reader.Read("Radius", component.Radius);
            reader.Read("Height", component.Height);
        }

        void SaveConvexHullCollider(const ConvexHullColliderComponent& component, ComponentWriter& writer)
        {
            SaveColliderSettings(component, writer);
            writer.Write("Shape", component.Shape);
        }

        void LoadConvexHullCollider(ConvexHullColliderComponent& component, const ComponentReader& reader)
        {
            LoadColliderSettings(component, reader);
            reader.Read("Shape", component.Shape);
        }

        void SaveMeshCollider(const MeshColliderComponent& component, ComponentWriter& writer)
        {
            SaveColliderSettings(component, writer);
            writer.Write("Shape", component.Shape);
        }

        void LoadMeshCollider(MeshColliderComponent& component, const ComponentReader& reader)
        {
            LoadColliderSettings(component, reader);
            reader.Read("Shape", component.Shape);
        }

        constexpr std::array<std::pair<CharacterMode, std::string_view>, 2> c_CharacterModeNames{ { { CharacterMode::Kinematic, "Kinematic" },{ CharacterMode::Physical, "Physical" } } };

        void SaveCharacterController(const CharacterControllerComponent& component, ComponentWriter& writer)
        {
            const auto a_Found = std::ranges::find(c_CharacterModeNames, component.Mode, &std::pair<CharacterMode, std::string_view>::first);
            writer.Write("Mode", a_Found != c_CharacterModeNames.end() ? a_Found->second : std::string_view("Kinematic"));
            writer.Write("Radius", component.Radius);
            writer.Write("Height", component.Height);
            writer.Write("Offset", component.Offset);
            writer.Write("MaxSlope", component.MaxSlope);
            writer.Write("StepHeight", component.StepHeight);
            writer.Write("Layer", component.Layer);
            writer.Write("PushStrength", component.PushStrength);
            writer.Write("Mass", component.Mass);
        }

        // A layer past the last there can be is taken as the first, as a collider's is, and a mode this build lacks, or none, as kinematic
        void LoadCharacterController(CharacterControllerComponent& component, const ComponentReader& reader)
        {
            std::string l_Mode;
            reader.Read("Mode", l_Mode);
            const auto a_Found = std::ranges::find(c_CharacterModeNames, std::string_view(l_Mode), &std::pair<CharacterMode, std::string_view>::second);
            component.Mode = a_Found != c_CharacterModeNames.end() ? a_Found->first : CharacterMode::Kinematic;
            reader.Read("Radius", component.Radius);
            reader.Read("Height", component.Height);
            reader.Read("Offset", component.Offset);
            reader.Read("MaxSlope", component.MaxSlope);
            reader.Read("StepHeight", component.StepHeight);
            reader.Read("Layer", component.Layer);
            reader.Read("PushStrength", component.PushStrength);
            reader.Read("Mass", component.Mass);
            component.Layer = component.Layer < c_MaxCollisionLayers ? component.Layer : 0;
        }

        void SaveRigidBody2D(const RigidBody2DComponent& component, ComponentWriter& writer)
        {
            const auto a_Found = std::ranges::find(c_BodyMotionNames, component.Motion, &std::pair<BodyMotion, std::string_view>::first);
            writer.Write("Motion", a_Found != c_BodyMotionNames.end() ? a_Found->second : std::string_view("Dynamic"));
            writer.Write("Mass", component.Mass);
            writer.Write("LinearDamping", component.LinearDamping);
            writer.Write("AngularDamping", component.AngularDamping);
            writer.Write("GravityFactor", component.GravityFactor);
            writer.Write("ContinuousCollision", component.ContinuousCollision);
            writer.Write("LockRotation", component.LockRotation);
            writer.Write("InitialLinearVelocity", component.InitialLinearVelocity);
            writer.Write("InitialAngularVelocity", component.InitialAngularVelocity);
            writer.Write("StartAsleep", component.StartAsleep);
            writer.Write("CanSleep", component.CanSleep);
        }

        // A motion this build lacks loads as dynamic
        void LoadRigidBody2D(RigidBody2DComponent& component, const ComponentReader& reader)
        {
            std::string l_Motion;
            reader.Read("Motion", l_Motion);
            const auto a_Found = std::ranges::find(c_BodyMotionNames, std::string_view(l_Motion), &std::pair<BodyMotion, std::string_view>::second);
            component.Motion = a_Found != c_BodyMotionNames.end() ? a_Found->first : BodyMotion::Dynamic;
            reader.Read("Mass", component.Mass);
            reader.Read("LinearDamping", component.LinearDamping);
            reader.Read("AngularDamping", component.AngularDamping);
            reader.Read("GravityFactor", component.GravityFactor);
            reader.Read("ContinuousCollision", component.ContinuousCollision);
            reader.Read("LockRotation", component.LockRotation);
            reader.Read("InitialLinearVelocity", component.InitialLinearVelocity);
            reader.Read("InitialAngularVelocity", component.InitialAngularVelocity);
            reader.Read("StartAsleep", component.StartAsleep);
            reader.Read("CanSleep", component.CanSleep);
        }

        // Written before each 2D collider's own fields
        void SaveCollider2DSettings(const Collider2DSettings& collider, ComponentWriter& writer)
        {
            writer.Write("Offset", collider.Offset);
            writer.Write("OffsetAngle", collider.OffsetAngle);
            writer.Write("Friction", collider.Friction);
            writer.Write("Restitution", collider.Restitution);
            writer.Write("Trigger", collider.Trigger);
            writer.Write("OneWay", collider.OneWay);
            writer.Write("Layer", collider.Layer);
        }

        // A layer past the last there can be is taken as the first. A scene saved before one-way colliders has none
        void LoadCollider2DSettings(Collider2DSettings& collider, const ComponentReader& reader)
        {
            reader.Read("Offset", collider.Offset);
            reader.Read("OffsetAngle", collider.OffsetAngle);
            reader.Read("Friction", collider.Friction);
            reader.Read("Restitution", collider.Restitution);
            reader.Read("Trigger", collider.Trigger);
            reader.Read("OneWay", collider.OneWay);
            reader.Read("Layer", collider.Layer);
            collider.Layer = collider.Layer < c_MaxCollisionLayers ? collider.Layer : 0;
        }

        void SaveBoxCollider2D(const BoxCollider2DComponent& component, ComponentWriter& writer)
        {
            SaveCollider2DSettings(component, writer);
            writer.Write("HalfExtents", component.HalfExtents);
        }

        void LoadBoxCollider2D(BoxCollider2DComponent& component, const ComponentReader& reader)
        {
            LoadCollider2DSettings(component, reader);
            reader.Read("HalfExtents", component.HalfExtents);
        }

        void SaveCircleCollider2D(const CircleCollider2DComponent& component, ComponentWriter& writer)
        {
            SaveCollider2DSettings(component, writer);
            writer.Write("Radius", component.Radius);
        }

        void LoadCircleCollider2D(CircleCollider2DComponent& component, const ComponentReader& reader)
        {
            LoadCollider2DSettings(component, reader);
            reader.Read("Radius", component.Radius);
        }

        void SaveCapsuleCollider2D(const CapsuleCollider2DComponent& component, ComponentWriter& writer)
        {
            SaveCollider2DSettings(component, writer);
            writer.Write("Radius", component.Radius);
            writer.Write("Height", component.Height);
        }

        void LoadCapsuleCollider2D(CapsuleCollider2DComponent& component, const ComponentReader& reader)
        {
            LoadCollider2DSettings(component, reader);
            reader.Read("Radius", component.Radius);
            reader.Read("Height", component.Height);
        }

        void SavePolygonCollider2D(const PolygonCollider2DComponent& component, ComponentWriter& writer)
        {
            SaveCollider2DSettings(component, writer);
            writer.Write("Points", std::span<const glm::vec2>(component.Points));
            writer.Write("Radius", component.Radius);
        }

        // Points past Box2D's most are dropped
        void LoadPolygonCollider2D(PolygonCollider2DComponent& component, const ComponentReader& reader)
        {
            LoadCollider2DSettings(component, reader);
            reader.Read("Points", component.Points);
            reader.Read("Radius", component.Radius);
            if (component.Points.size() > PolygonCollider2DComponent::c_MaxPoints)
            {
                component.Points.resize(PolygonCollider2DComponent::c_MaxPoints);
            }
        }

        void SaveCharacterController2D(const CharacterController2DComponent& component, ComponentWriter& writer)
        {
            const auto a_Found = std::ranges::find(c_CharacterModeNames, component.Mode, &std::pair<CharacterMode, std::string_view>::first);
            writer.Write("Mode", a_Found != c_CharacterModeNames.end() ? a_Found->second : std::string_view("Kinematic"));
            writer.Write("Radius", component.Radius);
            writer.Write("Height", component.Height);
            writer.Write("Offset", component.Offset);
            writer.Write("MaxSlope", component.MaxSlope);
            writer.Write("StepHeight", component.StepHeight);
            writer.Write("Layer", component.Layer);
            writer.Write("PushStrength", component.PushStrength);
            writer.Write("Mass", component.Mass);
        }

        // A layer past the last there can be is taken as the first, as a collider's is, and a mode this build lacks, or none, as kinematic, as a 3D character's are
        void LoadCharacterController2D(CharacterController2DComponent& component, const ComponentReader& reader)
        {
            std::string l_Mode;
            reader.Read("Mode", l_Mode);
            const auto a_Found = std::ranges::find(c_CharacterModeNames, std::string_view(l_Mode), &std::pair<CharacterMode, std::string_view>::second);
            component.Mode = a_Found != c_CharacterModeNames.end() ? a_Found->first : CharacterMode::Kinematic;
            reader.Read("Radius", component.Radius);
            reader.Read("Height", component.Height);
            reader.Read("Offset", component.Offset);
            reader.Read("MaxSlope", component.MaxSlope);
            reader.Read("StepHeight", component.StepHeight);
            reader.Read("Layer", component.Layer);
            reader.Read("PushStrength", component.PushStrength);
            reader.Read("Mass", component.Mass);
            component.Layer = component.Layer < c_MaxCollisionLayers ? component.Layer : 0;
        }

        constexpr std::array<std::pair<JointType, std::string_view>, 5> c_JointTypeNames{ { { JointType::Fixed, "Fixed" },{ JointType::Hinge, "Hinge" },{ JointType::Slider, "Slider" },{ JointType::Distance, "Distance" },{ JointType::Point, "Point" } } };

        // Every field, whichever the type uses, so changing a joint's type and back keeps its settings
        void SaveJoint(const JointComponent& component, ComponentWriter& writer)
        {
            const auto a_Found = std::ranges::find(c_JointTypeNames, component.Type, &std::pair<JointType, std::string_view>::first);
            writer.Write("Type", a_Found != c_JointTypeNames.end() ? a_Found->second : std::string_view("Hinge"));
            writer.Write("ConnectedBody", component.ConnectedBody);
            writer.Write("Anchor", component.Anchor);
            writer.Write("Axis", component.Axis);
            writer.Write("ConnectedAnchor", component.ConnectedAnchor);
            writer.Write("CollideConnected", component.CollideConnected);
            writer.Write("BreakForce", component.BreakForce);
            writer.Write("BreakTorque", component.BreakTorque);
            writer.Write("UseLimits", component.UseLimits);
            writer.Write("LowerLimit", component.LowerLimit);
            writer.Write("UpperLimit", component.UpperLimit);
            writer.Write("UseMotor", component.UseMotor);
            writer.Write("MotorSpeed", component.MotorSpeed);
            writer.Write("MotorMaxForce", component.MotorMaxForce);
            writer.Write("MinDistance", component.MinDistance);
            writer.Write("MaxDistance", component.MaxDistance);
            writer.Write("SpringFrequency", component.SpringFrequency);
            writer.Write("SpringDamping", component.SpringDamping);
        }

        // A type this build lacks loads as a hinge
        void LoadJoint(JointComponent& component, const ComponentReader& reader)
        {
            std::string l_Type;
            reader.Read("Type", l_Type);
            const auto a_Found = std::ranges::find(c_JointTypeNames, std::string_view(l_Type), &std::pair<JointType, std::string_view>::second);
            component.Type = a_Found != c_JointTypeNames.end() ? a_Found->first : JointType::Hinge;
            reader.Read("ConnectedBody", component.ConnectedBody);
            reader.Read("Anchor", component.Anchor);
            reader.Read("Axis", component.Axis);
            reader.Read("ConnectedAnchor", component.ConnectedAnchor);
            reader.Read("CollideConnected", component.CollideConnected);
            reader.Read("BreakForce", component.BreakForce);
            reader.Read("BreakTorque", component.BreakTorque);
            reader.Read("UseLimits", component.UseLimits);
            reader.Read("LowerLimit", component.LowerLimit);
            reader.Read("UpperLimit", component.UpperLimit);
            reader.Read("UseMotor", component.UseMotor);
            reader.Read("MotorSpeed", component.MotorSpeed);
            reader.Read("MotorMaxForce", component.MotorMaxForce);
            reader.Read("MinDistance", component.MinDistance);
            reader.Read("MaxDistance", component.MaxDistance);
            reader.Read("SpringFrequency", component.SpringFrequency);
            reader.Read("SpringDamping", component.SpringDamping);
        }

        constexpr std::array<std::pair<Joint2DType, std::string_view>, 5> c_Joint2DTypeNames{ { { Joint2DType::Revolute, "Revolute" },{ Joint2DType::Prismatic, "Prismatic" },{ Joint2DType::Distance, "Distance" },{ Joint2DType::Weld, "Weld" },{ Joint2DType::Wheel, "Wheel" } } };

        // Every field, whichever the type uses, as a 3D joint's
        void SaveJoint2D(const Joint2DComponent& component, ComponentWriter& writer)
        {
            const auto a_Found = std::ranges::find(c_Joint2DTypeNames, component.Type, &std::pair<Joint2DType, std::string_view>::first);
            writer.Write("Type", a_Found != c_Joint2DTypeNames.end() ? a_Found->second : std::string_view("Revolute"));
            writer.Write("ConnectedBody", component.ConnectedBody);
            writer.Write("Anchor", component.Anchor);
            writer.Write("Axis", component.Axis);
            writer.Write("ConnectedAnchor", component.ConnectedAnchor);
            writer.Write("CollideConnected", component.CollideConnected);
            writer.Write("BreakForce", component.BreakForce);
            writer.Write("BreakTorque", component.BreakTorque);
            writer.Write("UseLimits", component.UseLimits);
            writer.Write("LowerLimit", component.LowerLimit);
            writer.Write("UpperLimit", component.UpperLimit);
            writer.Write("UseMotor", component.UseMotor);
            writer.Write("MotorSpeed", component.MotorSpeed);
            writer.Write("MotorMaxForce", component.MotorMaxForce);
            writer.Write("MinDistance", component.MinDistance);
            writer.Write("MaxDistance", component.MaxDistance);
            writer.Write("SpringFrequency", component.SpringFrequency);
            writer.Write("SpringDamping", component.SpringDamping);
        }

        // A type this build lacks loads as revolute
        void LoadJoint2D(Joint2DComponent& component, const ComponentReader& reader)
        {
            std::string l_Type;
            reader.Read("Type", l_Type);
            const auto a_Found = std::ranges::find(c_Joint2DTypeNames, std::string_view(l_Type), &std::pair<Joint2DType, std::string_view>::second);
            component.Type = a_Found != c_Joint2DTypeNames.end() ? a_Found->first : Joint2DType::Revolute;
            reader.Read("ConnectedBody", component.ConnectedBody);
            reader.Read("Anchor", component.Anchor);
            reader.Read("Axis", component.Axis);
            reader.Read("ConnectedAnchor", component.ConnectedAnchor);
            reader.Read("CollideConnected", component.CollideConnected);
            reader.Read("BreakForce", component.BreakForce);
            reader.Read("BreakTorque", component.BreakTorque);
            reader.Read("UseLimits", component.UseLimits);
            reader.Read("LowerLimit", component.LowerLimit);
            reader.Read("UpperLimit", component.UpperLimit);
            reader.Read("UseMotor", component.UseMotor);
            reader.Read("MotorSpeed", component.MotorSpeed);
            reader.Read("MotorMaxForce", component.MotorMaxForce);
            reader.Read("MinDistance", component.MinDistance);
            reader.Read("MaxDistance", component.MaxDistance);
            reader.Read("SpringFrequency", component.SpringFrequency);
            reader.Read("SpringDamping", component.SpringDamping);
        }

        // Each field on one line with its type, so a class no loaded code has still saves and loads every value exactly
        void SaveScript(const ScriptComponent& component, ComponentWriter& writer)
        {
            writer.BeginList("Scripts");
            for (const ScriptEntry& it_Entry : component.Scripts)
            {
                writer.BeginItem();
                writer.Write("Class", std::string_view(it_Entry.ClassName));
                writer.Write("Language", ToString(it_Entry.Language));
                writer.Write("Enabled", it_Entry.Enabled);
                writer.BeginMap("Fields");
                for (const ScriptEntry::Field& it_Field : it_Entry.Fields)
                {
                    writer.Write(it_Field.Name, it_Field.Value);
                }

                writer.End();
            }

            writer.End();
        }

        // An entry with no class or a language this build lacks is left out, and so is a field with a type this build lacks or a name already read, each with a warning
        void LoadScript(ScriptComponent& component, const ComponentReader& reader)
        {
            component.Scripts.clear();
            reader.ReadList("Scripts", [&component](const ComponentReader& item)
            {
                std::string l_Class;
                std::string l_Language;
                item.Read("Class", l_Class);
                item.Read("Language", l_Language);
                const std::optional<ScriptLanguage> l_ParsedLanguage = ParseScriptLanguage(l_Language);
                if (l_Class.empty() || !l_ParsedLanguage)
                {
                    TR_CORE_WARN("Scene: a script {} has no class, or a language this build lacks, and is left out", l_Class);

                    return;
                }

                ScriptEntry l_Entry;
                l_Entry.ClassName = l_Class;
                l_Entry.Language = *l_ParsedLanguage;
                item.Read("Enabled", l_Entry.Enabled);
                item.ReadMap("Fields", [&l_Entry](std::string_view name, const ComponentReader& field)
                {
                    ScriptFieldValue l_Value;
                    if (!field.ReadValue(l_Value) || l_Entry.FindField(name) != nullptr)
                    {
                        TR_CORE_WARN("Scene: field {} of script {} has a type this build lacks, or repeats, and is left out", name, std::string_view(l_Entry.ClassName));

                        return;
                    }

                    l_Entry.Fields.push_back({ TaggedString<MemoryTag::Scene>(name), std::move(l_Value) });
                });

                component.Scripts.push_back(std::move(l_Entry));
            });
        }

        std::optional<UUID> ReadUUID(const YAML::Node& node)
        {
            const std::optional<UUID> l_UUID = node && node.IsScalar() ? UUID::Parse(node.Scalar()) : std::nullopt;

            return l_UUID && l_UUID->IsValid() ? l_UUID : std::nullopt;
        }

        // Each line of the text, without its line break
        std::vector<std::string_view> SplitLines(std::string_view text)
        {
            std::vector<std::string_view> l_Lines;
            std::size_t l_Start = 0;
            while (l_Start <= text.size())
            {
                const std::size_t l_End = std::min(text.find('\n', l_Start), text.size());
                std::string_view l_Line = text.substr(l_Start, l_End - l_Start);
                if (l_Line.ends_with('\r'))
                {
                    l_Line.remove_suffix(1);
                }

                l_Lines.push_back(l_Line);
                l_Start = l_End + 1;
            }

            return l_Lines;
        }

        // A block map or sequence that holds something, which is compared and written within, where anything else is one value
        bool IsBranch(const YAML::Node& node)
        {
            return (node.IsMap() || node.IsSequence()) && node.size() != 0 && node.Style() != YAML::EmitterStyle::Flow;
        }

        // A value after its key, on the key's line, or on lines of their own below it for a block map or sequence
        void AppendNode(std::string& output, const YAML::Node& node, std::uint32_t indent)
        {
            if (!node.IsDefined() || node.IsNull())
            {
                return;
            }

            if (IsBranch(node))
            {
                AppendIndented(output, YAML::Dump(node), indent);

                return;
            }

            output += ' ';
            output += YAML::Dump(node);
        }

        // An entity as a file has it, or an instance of a prefab, whose ID is the instance's own, with the lines of the text the entry takes, from its first, which an instance whose prefab is missing keeps as they are
        struct FileEntity
        {
            UUID ID;
            UUID Parent;
            YAML::Node Node;
            UUID Prefab;
            std::size_t FirstLine = 0;
            std::size_t EndLine = 0;

            [[nodiscard]] bool IsInstance() const { return Prefab.IsValid(); }
        };

        // Instances only where a scene may hold them, and not yet in a prefab
        Expected<std::vector<FileEntity>, std::string> ReadEntities(const YAML::Node& root, bool allowInstances, std::size_t lineCount)
        {
            const YAML::Node l_Entities = root["Entities"];
            if (!l_Entities)
            {
                return std::vector<FileEntity>();
            }

            if (!l_Entities.IsSequence())
            {
                return Unexpected{ std::string("Entities is not a sequence") };
            }

            std::vector<FileEntity> l_Result;
            l_Result.reserve(l_Entities.size());
            std::unordered_set<UUID> l_IDs;
            for (std::size_t it_Index = 0; it_Index < l_Entities.size(); ++it_Index)
            {
                FileEntity l_Entity;
                l_Entity.Node = l_Entities[it_Index];
                if (const std::optional<UUID> l_UUID = l_Entity.Node.IsMap() ? ReadUUID(l_Entity.Node[std::string(c_EntityKey)]) : std::nullopt)
                {
                    l_Entity.ID = *l_UUID;
                }
                else if (const std::optional<UUID> l_Instance = allowInstances && l_Entity.Node.IsMap() ? ReadUUID(l_Entity.Node[std::string(c_InstanceKey)]) : std::nullopt)
                {
                    const std::optional<UUID> l_Prefab = ReadUUID(l_Entity.Node[std::string(c_PrefabKey)]);
                    if (!l_Prefab)
                    {
                        return Unexpected{ std::format("instance {} names no valid Prefab", *l_Instance) };
                    }

                    l_Entity.ID = *l_Instance;
                    l_Entity.Prefab = *l_Prefab;
                }
                else
                {
                    return Unexpected{ std::format("entity {} has no valid Entity UUID", it_Index) };
                }

                if (const YAML::Node l_ParentNode = l_Entity.Node[std::string(c_ParentKey)])
                {
                    const std::optional<UUID> l_ParentUUID = ReadUUID(l_ParentNode);
                    if (!l_ParentUUID)
                    {
                        return Unexpected{ std::format("entity {} has a malformed Parent", l_Entity.ID) };
                    }

                    l_Entity.Parent = *l_ParentUUID;
                }

                if (!l_IDs.insert(l_Entity.ID).second)
                {
                    return Unexpected{ std::format("entity {} appears more than once", l_Entity.ID) };
                }

                l_Entity.FirstLine = static_cast<std::size_t>(std::max(l_Entity.Node.Mark().line, 0));
                l_Result.push_back(std::move(l_Entity));
            }

            // Each entry's lines run to the next entry's, and the last's to the next of the document's own keys, or the end
            std::size_t l_End = lineCount;
            for (const auto& it_Pair : root)
            {
                const std::size_t l_Line = static_cast<std::size_t>(std::max(it_Pair.first.Mark().line, 0));
                if (!l_Result.empty() && l_Line > l_Result.back().FirstLine)
                {
                    l_End = std::min(l_End, l_Line);
                }
            }

            for (std::size_t it_Index = 0; it_Index < l_Result.size(); ++it_Index)
            {
                l_Result[it_Index].EndLine = it_Index + 1 < l_Result.size() ? l_Result[it_Index + 1].FirstLine : l_End;
            }

            return l_Result;
        }

        // The YAML, refused when it is not, or when its Format is not one from 1 to the newest this build reads
        Expected<YAML::Node, std::string> ParseFormat(std::string_view text, std::uint32_t newest, std::uint32_t& version)
        {
            YAML::Node l_Root;
            try
            {
                l_Root = YAML::Load(std::string(text));
            }
            catch (const YAML::Exception& exception)
            {
                return Unexpected{ std::format("not valid YAML: {}", exception.what()) };
            }

            const YAML::Node l_Format = l_Root.IsMap() ? l_Root["Format"] : YAML::Node();
            if (!l_Format || !l_Format.IsScalar() || !ParseNumber(l_Format.Scalar(), version) || version == 0)
            {
                return Unexpected{ std::string("no valid Format version") };
            }

            if (version > newest)
            {
                return Unexpected{ std::format("written in format {} by a newer Trinity, and this build reads format {} and older", version, newest) };
            }

            return l_Root;
        }

        // Format and migrations first, so entity text kept by the editor reads exactly like a scene file
        Expected<YAML::Node, std::string> ParseDocument(std::string_view text)
        {
            std::uint32_t l_Version = 0;
            Expected<YAML::Node, std::string> l_Root = ParseFormat(text, SceneSerializer::c_FormatVersion, l_Version);
            if (!l_Root)
            {
                return l_Root;
            }

            for (std::uint32_t it_Version = l_Version; it_Version < SceneSerializer::c_FormatVersion; ++it_Version)
            {
                c_Migrations[it_Version - 1](*l_Root);
            }

            return l_Root;
        }

        // The prefab's entities, root first in hierarchy order, each with the UUID the instance gives it, without those the instance removes and their subtrees
        std::vector<std::pair<entt::entity, UUID>> GetInstanceMembers(Scene& prefab, UUID instance, const std::unordered_set<UUID>& removed)
        {
            std::vector<std::pair<entt::entity, UUID>> l_Members;
            const Entity l_Root = prefab.GetFirstRoot();
            std::vector<Entity> l_Pending{ l_Root };
            while (!l_Pending.empty())
            {
                const Entity l_Entity = l_Pending.back();
                l_Pending.pop_back();
                l_Members.emplace_back(l_Entity.GetHandle(), Prefab::MakeEntityUUID(instance, l_Entity.GetUUID()));
                for (Entity it_Child = l_Entity.GetLastChild(); it_Child; it_Child = it_Child.GetPreviousSibling())
                {
                    if (!removed.contains(it_Child.GetUUID()))
                    {
                        l_Pending.push_back(it_Child);
                    }
                }
            }

            return l_Members;
        }

        // The local IDs an instance's entry removes, never its prefab's root
        std::unordered_set<UUID> ReadRemovedEntities(const YAML::Node& entry)
        {
            std::unordered_set<UUID> l_Removed;
            const YAML::Node l_List = entry[std::string(c_RemovedEntitiesKey)];
            if (l_List && l_List.IsSequence())
            {
                for (const YAML::Node it_Local : l_List)
                {
                    if (const std::optional<UUID> l_Local = ReadUUID(it_Local))
                    {
                        l_Removed.insert(*l_Local);
                    }
                }
            }

            return l_Removed;
        }

        // An entry as it is to be loaded: an instance with its prefab's entities, its members, or one whose prefab is not there standing in for itself with the text its entry and the entries of its subtree take, which it absorbs. An entity under one its prefab no longer has goes under the instance's root
        struct PlannedEntity
        {
            const FileEntity* File = nullptr;
            UUID Parent;
            bool Absorbed = false;
            Scene* Template = nullptr;
            std::vector<std::pair<entt::entity, UUID>> Members;
            std::string Kept;

            [[nodiscard]] UUID GetRoot() const { return Template != nullptr ? Members.front().second : File->ID; }

            // What it is to make, the prefab's entities for an instance, and the entity itself for an entity or one standing in for an instance
            [[nodiscard]] std::vector<UUID> GetEntities() const
            {
                if (Template == nullptr)
                {
                    return { File->ID };
                }

                std::vector<UUID> l_Entities;
                l_Entities.reserve(Members.size());
                for (const auto& it_Member : Members)
                {
                    l_Entities.push_back(it_Member.second);
                }

                return l_Entities;
            }
        };

        bool IsBlank(std::string_view line)
        {
            return line.find_first_not_of(" \t") == std::string_view::npos;
        }

        // The entry's lines as the file has them, each after a line break, every one of them for an entry taken whole, or for an instance's body those after its first and Parent's, which are written in their place. An entry not written in blocks, as Trinity writes them, is written as YAML would
        std::string GetEntryText(const FileEntity& entity, std::span<const std::string_view> lines, bool body)
        {
            std::size_t l_KeyLine = std::string_view::npos;
            std::size_t l_ParentLine = std::string_view::npos;
            bool l_OneLine = true;
            for (const auto& it_Pair : entity.Node)
            {
                const std::string& l_Key = it_Pair.first.Scalar();
                if (l_Key == c_InstanceKey || l_Key == c_EntityKey || l_Key == c_ParentKey)
                {
                    const std::size_t l_Line = static_cast<std::size_t>(std::max(it_Pair.first.Mark().line, 0));
                    l_OneLine = l_OneLine && it_Pair.second.IsScalar() && it_Pair.second.Mark().line == it_Pair.first.Mark().line;
                    (l_Key == c_ParentKey ? l_ParentLine : l_KeyLine) = l_Line;
                }
            }

            std::string l_Text;
            if (entity.Node.Style() != YAML::EmitterStyle::Flow && l_OneLine && l_KeyLine == entity.FirstLine && entity.FirstLine < entity.EndLine && entity.EndLine <= lines.size())
            {
                std::size_t l_End = entity.EndLine;
                while (l_End > entity.FirstLine + 1 && IsBlank(lines[l_End - 1]))
                {
                    --l_End;
                }

                for (std::size_t it_Line = entity.FirstLine + (body ? 1 : 0); it_Line < l_End; ++it_Line)
                {
                    if (!body || it_Line != l_ParentLine)
                    {
                        l_Text += '\n';
                        l_Text += lines[it_Line];
                    }
                }

                return l_Text;
            }

            if (!body)
            {
                return std::format("\n  - {}", YAML::Dump(entity.Node));
            }

            for (const auto& it_Pair : entity.Node)
            {
                if (it_Pair.first.Scalar() != c_InstanceKey && it_Pair.first.Scalar() != c_ParentKey)
                {
                    l_Text += std::format("\n    {}:", FormatString(it_Pair.first.Scalar()));
                    AppendNode(l_Text, it_Pair.second, 6);
                }
            }

            return l_Text;
        }

        // Every entity there is to be with its parent, each found once, and each parent among them, which an entry under an instance's entity finds once the instance is, so a scene is only replaced once it is known to load whole
        Expected<std::vector<PlannedEntity>, std::string> PlanEntities(const std::vector<FileEntity>& entities, std::span<const std::string_view> lines)
        {
            std::vector<PlannedEntity> l_Plan(entities.size());
            std::unordered_map<UUID, UUID> l_Parents;
            for (std::size_t it_Index = 0; it_Index < entities.size(); ++it_Index)
            {
                const FileEntity& l_File = entities[it_Index];
                PlannedEntity& l_Planned = l_Plan[it_Index];
                l_Planned.File = &l_File;
                l_Planned.Parent = l_File.Parent;
                Scene* l_Template = l_File.IsInstance() ? SceneSerializer::FindPrefabTemplate(l_File.Prefab) : nullptr;
                if (l_Template == nullptr || !l_Template->GetFirstRoot())
                {
                    if (!l_Parents.emplace(l_File.ID, l_File.Parent).second)
                    {
                        return Unexpected{ std::format("entity {} appears more than once", l_File.ID) };
                    }

                    continue;
                }

                l_Planned.Template = l_Template;
                l_Planned.Members = GetInstanceMembers(*l_Template, l_File.ID, ReadRemovedEntities(l_File.Node));
                const SceneRegistry& l_Registry = l_Template->GetRegistry();
                for (const auto& [it_Member, it_UUID] : l_Planned.Members)
                {
                    const entt::entity l_Parent = l_Registry.get<RelationshipComponent>(it_Member).Parent;
                    const UUID l_ParentUUID = l_Parent == entt::null ? l_File.Parent : Prefab::MakeEntityUUID(l_File.ID, l_Registry.get<IDComponent>(l_Parent).ID);
                    if (!it_UUID.IsValid() || !l_Parents.emplace(it_UUID, l_ParentUUID).second)
                    {
                        return Unexpected{ std::format("entity {} of instance {} appears more than once", it_UUID, l_File.ID) };
                    }
                }
            }

            // An entry is in the subtree of each instance before it whose entities, or entries in it, its parent is one of. While the latest such instance is one whose prefab is missing, the entry is absorbed into it, and an entry whose parent is no entity there is to be, under an instance's entity the prefab no longer has, goes under that instance's root
            struct Run
            {
                std::size_t Entry = 0;
                bool Kept = false;
                std::unordered_set<UUID> Members;
            };

            std::vector<Run> l_Runs;
            for (std::size_t it_Index = 0; it_Index < entities.size(); ++it_Index)
            {
                const FileEntity& l_File = entities[it_Index];
                PlannedEntity& l_Planned = l_Plan[it_Index];
                const UUID l_Parent = l_File.Parent;
                while (!l_Runs.empty() && (!l_Parent.IsValid() || (!l_Runs.back().Members.contains(l_Parent) && l_Parents.contains(l_Parent))))
                {
                    l_Runs.pop_back();
                }

                const std::vector<UUID> l_Brought = l_Planned.GetEntities();
                const auto a_Bring = [&l_Runs, &l_Brought]
                {
                    for (Run& it_Run : l_Runs)
                    {
                        it_Run.Members.insert(l_Brought.begin(), l_Brought.end());
                    }
                };

                if (!l_Runs.empty() && l_Runs.back().Kept)
                {
                    l_Planned.Absorbed = true;
                    l_Plan[l_Runs.back().Entry].Kept += GetEntryText(l_File, lines, false);
                    a_Bring();

                    continue;
                }

                if (l_Parent.IsValid() && !l_Parents.contains(l_Parent))
                {
                    if (l_Runs.empty())
                    {
                        return Unexpected{ std::format("entity {} names parent {}, which is not in the file", l_File.ID, l_Parent) };
                    }

                    const PlannedEntity& l_Owner = l_Plan[l_Runs.back().Entry];
                    TR_CORE_WARN("Scene: entity {} was under {}, which prefab instance {} no longer has, so it goes under the instance's root", l_File.ID, l_Parent, l_Owner.File->ID);
                    l_Planned.Parent = l_Owner.GetRoot();
                    l_Parents[l_Planned.GetRoot()] = l_Planned.Parent;
                }

                a_Bring();
                if (l_File.IsInstance())
                {
                    l_Runs.push_back({ it_Index, l_Planned.Template == nullptr, std::unordered_set<UUID>(l_Brought.begin(), l_Brought.end()) });
                    if (l_Planned.Template == nullptr)
                    {
                        l_Planned.Kept = GetEntryText(l_File, lines, true);
                    }
                }
            }

            // Each parent is an entity there is to be, and following parents from any entity reaches a root within as many steps as there are entities
            std::unordered_set<UUID> l_Existing;
            for (const PlannedEntity& it_Planned : l_Plan)
            {
                if (!it_Planned.Absorbed)
                {
                    const std::vector<UUID> l_Entities = it_Planned.GetEntities();
                    l_Existing.insert(l_Entities.begin(), l_Entities.end());
                }
            }

            for (const PlannedEntity& it_Planned : l_Plan)
            {
                if (it_Planned.Absorbed)
                {
                    continue;
                }

                if (it_Planned.Parent.IsValid() && !l_Existing.contains(it_Planned.Parent))
                {
                    return Unexpected{ std::format("entity {} names parent {}, which is not in the file", it_Planned.File->ID, it_Planned.Parent) };
                }

                UUID l_Current = it_Planned.Parent;
                for (std::size_t it_Step = 0; l_Current.IsValid(); ++it_Step)
                {
                    const auto a_Parent = l_Parents.find(l_Current);
                    if (a_Parent == l_Parents.end())
                    {
                        return Unexpected{ std::format("entity {} has an ancestor {} that is not in the file", it_Planned.File->ID, l_Current) };
                    }

                    if (it_Step > l_Parents.size())
                    {
                        return Unexpected{ std::format("entity {} is its own ancestor", it_Planned.File->ID) };
                    }

                    l_Current = a_Parent->second;
                }
            }

            return l_Plan;
        }

        // The entity, its own UUID and its parent's written, or none for one written as a root
        void AppendEntity(std::string& text, Entity entity, UUID id, UUID parent)
        {
            constexpr std::uint32_t c_FieldIndent = 6;

            SceneRegistry& l_Registry = entity.GetScene()->GetRegistry();

            text += std::format("\n  - {}: {}", c_EntityKey, ToHex(id));
            if (parent.IsValid())
            {
                text += std::format("\n    {}: {}", c_ParentKey, ToHex(parent));
            }

            for (const ComponentSerializer& it_Serializer : s_State->Serializers)
            {
                const SceneRegistry::common_type* l_Storage = l_Registry.storage(GetPoolId(it_Serializer.Name));
                if (l_Storage == nullptr || !l_Storage->contains(entity.GetHandle()))
                {
                    continue;
                }

                text += std::format("\n    {}:", FormatString(it_Serializer.Name));
                ComponentWriter l_Writer(text, c_FieldIndent);
                it_Serializer.Save(l_Storage->value(entity.GetHandle()), l_Writer);
            }

            if (entity.Has<UnknownComponentsComponent>())
            {
                for (const UnknownComponentsComponent::Entry& it_Unknown : entity.Get<UnknownComponentsComponent>().Entries)
                {
                    text += std::format("\n    {}:", FormatString(it_Unknown.Name));
                    AppendUnknown(text, it_Unknown.Yaml, c_FieldIndent);
                }
            }
        }

        void LoadComponent(Entity entity, const ComponentSerializer& serializer, SceneRegistry::common_type& storage, const YAML::Node& node)
        {
            if (!storage.contains(entity.GetHandle()))
            {
                storage.push(entity.GetHandle());
            }

            const ComponentReader l_Reader(node, serializer.Name);
            serializer.Load(storage.value(entity.GetHandle()), l_Reader);
        }

        // Only a component with a registered serializer, which code knows how to save and load
        SceneRegistry::common_type* FindStorage(Entity entity, std::string_view name)
        {
            return FindSerializer(name) != nullptr ? entity.GetScene()->GetRegistry().storage(GetPoolId(name)) : nullptr;
        }

        // The prefab's entities with the instance's UUIDs and every component the prefab gives them, linked as the prefab links them under the instance's root, which goes last among the roots. A prefab names no entity outside itself, so each it names is by local ID, and the instance names the entity of that local ID it has, or would have. Their UUIDs are free
        Entity ExpandInstance(Scene& scene, Scene& prefab, const std::vector<std::pair<entt::entity, UUID>>& members, UUID prefabID, UUID instance)
        {
            SceneRegistry& l_Target = scene.GetRegistry();
            SceneRegistry& l_Source = prefab.GetRegistry();
            for (const auto& [it_Member, it_UUID] : members)
            {
                const Entity l_Entity = scene.CreateEntityWithUUID(it_UUID);
                for (auto [it_Id, it_Storage] : l_Source.storage())
                {
                    if (it_Id == entt::type_hash<IDComponent>::value() || it_Id == entt::type_hash<RelationshipComponent>::value() || it_Id == entt::type_hash<PrefabInstanceComponent>::value() || !it_Storage.contains(it_Member))
                    {
                        continue;
                    }

                    SceneRegistry::common_type* l_Storage = l_Target.storage(it_Id);
                    if (l_Storage == nullptr)
                    {
                        TR_CORE_ERROR("Scene: prefab {} has a component the scene has no pool for, which instance {} leaves out", prefabID, instance);

                        continue;
                    }

                    if (l_Storage->contains(l_Entity.GetHandle()))
                    {
                        l_Storage->erase(l_Entity.GetHandle());
                    }

                    l_Storage->push(l_Entity.GetHandle(), it_Storage.value(it_Member));
                }

                const entt::entity l_Parent = l_Source.get<RelationshipComponent>(it_Member).Parent;
                if (l_Parent != entt::null)
                {
                    scene.SetParent(l_Entity, scene.FindEntityByUUID(Prefab::MakeEntityUUID(instance, l_Source.get<IDComponent>(l_Parent).ID)), false);
                }
            }

            Entity l_Root = scene.FindEntityByUUID(members.front().second);
            scene.RemapEntityReferences(l_Root, [instance](UUID& entity)
            {
                if (entity.IsValid())
                {
                    entity = Prefab::MakeEntityUUID(instance, entity);
                }
            });

            l_Root.Add<PrefabInstanceComponent>(PrefabInstanceComponent{ .Prefab = prefabID, .Instance = instance, .Kept = {} });

            return l_Root;
        }

        UnknownComponentsComponent::Entry* FindKept(Entity entity, std::string_view name)
        {
            if (!entity.Has<UnknownComponentsComponent>())
            {
                return nullptr;
            }

            auto& l_Entries = entity.Get<UnknownComponentsComponent>().Entries;
            const auto a_Kept = std::ranges::find_if(l_Entries, [name](const UnknownComponentsComponent::Entry& entry) { return std::string_view(entry.Name) == name; });

            return a_Kept != l_Entries.end() ? &*a_Kept : nullptr;
        }

        std::string SaveComponentYaml(const ComponentSerializer& serializer, const void* component)
        {
            std::string l_Text;
            ComponentWriter l_Writer(l_Text, 0);
            serializer.Save(component, l_Writer);

            return l_Text;
        }

        // False when the entity keeps no YAML for the component
        bool RemoveKept(Entity entity, std::string_view name)
        {
            if (!entity.Has<UnknownComponentsComponent>())
            {
                return false;
            }

            auto& l_Entries = entity.Get<UnknownComponentsComponent>().Entries;
            if (std::erase_if(l_Entries, [name](const UnknownComponentsComponent::Entry& entry) { return std::string_view(entry.Name) == name; }) == 0)
            {
                return false;
            }

            if (l_Entries.empty())
            {
                entity.Remove<UnknownComponentsComponent>();
            }

            return true;
        }

        // What the entity's component is as YAML, by its serializer or as it is kept, or nothing when the entity is without it
        std::optional<std::string> GetComponentYaml(Entity entity, std::string_view name)
        {
            if (SceneRegistry::common_type* l_Storage = FindStorage(entity, name); l_Storage != nullptr && l_Storage->contains(entity.GetHandle()))
            {
                return SaveComponentYaml(*FindSerializer(name), l_Storage->value(entity.GetHandle()));
            }

            if (const UnknownComponentsComponent::Entry* l_Kept = FindKept(entity, name))
            {
                return std::string(std::string_view(l_Kept->Yaml));
            }

            return std::nullopt;
        }

        // The component loaded from the YAML, by its serializer, added first when the entity is without it, or kept as it is when no loaded code knows it. Whole, so a component the entity had is replaced
        void SetComponentYaml(Entity entity, std::string_view name, const YAML::Node& node, std::unordered_map<std::string, std::size_t>& unknown)
        {
            if (SceneRegistry::common_type* l_Storage = FindStorage(entity, name))
            {
                if (l_Storage->contains(entity.GetHandle()))
                {
                    l_Storage->erase(entity.GetHandle());
                }

                LoadComponent(entity, *FindSerializer(name), *l_Storage, node);

                return;
            }

            if (UnknownComponentsComponent::Entry* l_Kept = FindKept(entity, name))
            {
                l_Kept->Yaml = TaggedString<MemoryTag::Scene>(YAML::Dump(node));

                return;
            }

            if (!entity.Has<UnknownComponentsComponent>())
            {
                entity.Add<UnknownComponentsComponent>();
            }

            entity.Get<UnknownComponentsComponent>().Entries.push_back({ TaggedString<MemoryTag::Scene>(name), TaggedString<MemoryTag::Scene>(YAML::Dump(node)) });
            ++unknown[std::string(name)];
        }

        // The value set at the path inside the node, by key in a map and by index in a sequence, false when the path leads nowhere the node has. A map's last key need not be there yet
        bool SetAtPath(YAML::Node& node, std::string_view path, const YAML::Node& value)
        {
            YAML::Node l_Current = node;
            std::size_t l_Start = 0;
            while (true)
            {
                const std::size_t l_End = path.find(c_PathSeparator, l_Start);
                const std::string l_Segment(path.substr(l_Start, l_End == std::string_view::npos ? std::string_view::npos : l_End - l_Start));
                const bool l_Last = l_End == std::string_view::npos;
                if (l_Current.IsSequence())
                {
                    std::size_t l_Index = 0;
                    if (!ParseNumber(std::string_view(l_Segment), l_Index) || l_Index >= l_Current.size())
                    {
                        return false;
                    }

                    if (l_Last)
                    {
                        l_Current[l_Index] = YAML::Clone(value);

                        return true;
                    }

                    l_Current.reset(l_Current[l_Index]);
                }
                else if (l_Current.IsMap())
                {
                    if (l_Last)
                    {
                        l_Current[l_Segment] = YAML::Clone(value);

                        return true;
                    }

                    if (!l_Current[l_Segment])
                    {
                        return false;
                    }

                    l_Current.reset(l_Current[l_Segment]);
                }
                else
                {
                    return false;
                }

                l_Start = l_End + 1;
            }
        }

        // What the node holds at the path, by key in a map and by index in a sequence, or nothing where the path leads nowhere the node has
        std::optional<YAML::Node> GetAtPath(const YAML::Node& node, std::string_view path)
        {
            YAML::Node l_Current = node;
            std::size_t l_Start = 0;
            while (true)
            {
                const std::size_t l_End = path.find(c_PathSeparator, l_Start);
                const std::string l_Segment(path.substr(l_Start, l_End == std::string_view::npos ? std::string_view::npos : l_End - l_Start));
                if (l_Current.IsSequence())
                {
                    std::size_t l_Index = 0;
                    if (!ParseNumber(std::string_view(l_Segment), l_Index) || l_Index >= l_Current.size())
                    {
                        return std::nullopt;
                    }

                    l_Current.reset(std::as_const(l_Current)[l_Index]);
                }
                else if (l_Current.IsMap() && std::as_const(l_Current)[l_Segment])
                {
                    l_Current.reset(std::as_const(l_Current)[l_Segment]);
                }
                else
                {
                    return std::nullopt;
                }

                if (l_End == std::string_view::npos)
                {
                    return l_Current;
                }

                l_Start = l_End + 1;
            }
        }

        // The map's key at the end of the path taken out, false when the path leads to no map's key
        bool RemoveAtPath(YAML::Node& node, std::string_view path)
        {
            const std::size_t l_Last = path.rfind(c_PathSeparator);
            const std::optional<YAML::Node> l_Parent = l_Last == std::string_view::npos ? std::optional<YAML::Node>(node) : GetAtPath(node, path.substr(0, l_Last));
            if (!l_Parent || !l_Parent->IsMap())
            {
                return false;
            }

            YAML::Node l_Map = *l_Parent;

            return l_Map.remove(std::string(l_Last == std::string_view::npos ? path : path.substr(l_Last + 1)));
        }

        // The overrides of one of the entity's components: each value at its path, or the whole of it for one value. How many lead nowhere the component has, all of them for a component the entity is without
        std::size_t OverrideComponent(Entity entity, std::string_view name, const YAML::Node& overrides, std::unordered_map<std::string, std::size_t>& unknown)
        {
            const bool l_ByPath = IsBranch(overrides) && overrides.IsMap();
            const std::size_t l_Count = l_ByPath ? overrides.size() : 1;
            const std::optional<std::string> l_Yaml = GetComponentYaml(entity, name);
            if (!l_Yaml)
            {
                return l_Count;
            }

            YAML::Node l_Node;
            try
            {
                l_Node = YAML::Load(*l_Yaml);
            }
            catch (const YAML::Exception&)
            {
                return l_Count;
            }

            std::size_t l_Dropped = 0;
            if (l_ByPath)
            {
                for (const auto& it_Pair : overrides)
                {
                    if (!it_Pair.first.IsScalar() || !SetAtPath(l_Node, it_Pair.first.Scalar(), it_Pair.second))
                    {
                        ++l_Dropped;
                    }
                }
            }
            else
            {
                l_Node.reset(YAML::Clone(overrides));
            }

            SetComponentYaml(entity, name, l_Node, unknown);

            return l_Dropped;
        }

        // Of each entity the section names by local ID, each component under it given to the function, with how many the section names of what the instance no longer has
        template<typename F>
        std::size_t ForEachChange(Scene& scene, const FileEntity& instance, std::string_view section, F&& change)
        {
            const YAML::Node l_Section = instance.Node[std::string(section)];
            if (!l_Section)
            {
                return 0;
            }

            if (!l_Section.IsMap())
            {
                return 1;
            }

            std::size_t l_Dropped = 0;
            for (const auto& it_Entity : l_Section)
            {
                const std::optional<UUID> l_Local = ReadUUID(it_Entity.first);
                const Entity l_Entity = l_Local ? scene.FindEntityByUUID(Prefab::MakeEntityUUID(instance.ID, *l_Local)) : Entity();
                if (!l_Entity)
                {
                    ++l_Dropped;

                    continue;
                }

                l_Dropped += change(l_Entity, it_Entity.second);
            }

            return l_Dropped;
        }

        // What the instance changes of its prefab: the components it removes, the values it overrides, then the components it adds. A change of what the prefab no longer has is dropped, and logged
        void ApplyInstanceChanges(Scene& scene, const PlannedEntity& planned, std::unordered_map<std::string, std::size_t>& unknown)
        {
            const FileEntity& l_File = *planned.File;
            std::size_t l_Dropped = ForEachChange(scene, l_File, c_RemovedComponentsKey, [](Entity entity, const YAML::Node& names) -> std::size_t
            {
                if (!names.IsSequence())
                {
                    return 1;
                }

                std::size_t l_Missing = 0;
                for (const YAML::Node it_Name : names)
                {
                    const std::string l_Name = it_Name.IsScalar() ? it_Name.Scalar() : std::string();
                    if (FindStorage(entity, l_Name) != nullptr ? !SceneSerializer::RemoveComponent(entity, l_Name) : !RemoveKept(entity, l_Name))
                    {
                        ++l_Missing;
                    }
                }

                return l_Missing;
            });

            l_Dropped += ForEachChange(scene, l_File, c_OverridesKey, [&unknown](Entity entity, const YAML::Node& components) -> std::size_t
            {
                if (!components.IsMap())
                {
                    return 1;
                }

                std::size_t l_Missing = 0;
                for (const auto& it_Component : components)
                {
                    l_Missing += OverrideComponent(entity, it_Component.first.Scalar(), it_Component.second, unknown);
                }

                return l_Missing;
            });

            l_Dropped += ForEachChange(scene, l_File, c_AddedComponentsKey, [&unknown](Entity entity, const YAML::Node& components) -> std::size_t
            {
                if (!components.IsMap())
                {
                    return 1;
                }

                for (const auto& it_Component : components)
                {
                    SetComponentYaml(entity, it_Component.first.Scalar(), it_Component.second, unknown);
                }

                return 0;
            });

            if (l_Dropped != 0)
            {
                TR_CORE_WARN("Scene: instance {} of prefab {} changes {} thing(s) its prefab no longer has, which are dropped", l_File.ID, l_File.Prefab, l_Dropped);
            }
        }

        // Every entity first, each instance as its prefab's entities with its UUIDs and one whose prefab is missing as the entity standing in for it, then parents in file order, which is hierarchy order, so siblings keep theirs, then components, and what each instance changes of its prefab. Components no loaded code knows are kept as YAML and counted by name
        void CreateEntities(Scene& scene, const std::vector<PlannedEntity>& plan, std::unordered_map<std::string, std::size_t>& unknown)
        {
            SceneRegistry& l_Registry = scene.GetRegistry();
            for (const PlannedEntity& it_Planned : plan)
            {
                const FileEntity& l_File = *it_Planned.File;
                if (it_Planned.Absorbed)
                {
                    continue;
                }

                if (it_Planned.Template != nullptr)
                {
                    static_cast<void>(ExpandInstance(scene, *it_Planned.Template, it_Planned.Members, l_File.Prefab, l_File.ID));
                }
                else if (l_File.IsInstance())
                {
                    Entity l_Missing = scene.CreateEntityWithUUID(l_File.ID);
                    l_Missing.Add<TagComponent>().Tag = c_MissingPrefabName;
                    l_Missing.Add<PrefabInstanceComponent>(PrefabInstanceComponent{ .Prefab = l_File.Prefab, .Instance = l_File.ID, .Kept = TaggedString<MemoryTag::Scene>(it_Planned.Kept) });
                    TR_CORE_WARN("Scene: prefab {} of instance {} is missing or cannot be read, so the instance is kept as the file has it until the prefab is there", l_File.Prefab, l_File.ID);
                }
                else
                {
                    static_cast<void>(scene.CreateEntityWithUUID(l_File.ID));
                }
            }

            for (const PlannedEntity& it_Planned : plan)
            {
                if (!it_Planned.Absorbed && it_Planned.Parent.IsValid())
                {
                    scene.SetParent(scene.FindEntityByUUID(it_Planned.GetRoot()), scene.FindEntityByUUID(it_Planned.Parent), false);
                }
            }

            for (const PlannedEntity& it_Planned : plan)
            {
                const FileEntity& l_File = *it_Planned.File;
                if (it_Planned.Absorbed || l_File.IsInstance())
                {
                    continue;
                }

                Entity l_Entity = scene.FindEntityByUUID(l_File.ID);
                for (const auto& it_Pair : l_File.Node)
                {
                    const std::string& l_Name = it_Pair.first.Scalar();
                    if (l_Name == c_EntityKey || l_Name == c_ParentKey)
                    {
                        continue;
                    }

                    const ComponentSerializer* l_Serializer = FindSerializer(l_Name);
                    SceneRegistry::common_type* l_Storage = l_Serializer != nullptr ? l_Registry.storage(GetPoolId(l_Name)) : nullptr;
                    if (l_Storage == nullptr)
                    {
                        if (!l_Entity.Has<UnknownComponentsComponent>())
                        {
                            l_Entity.Add<UnknownComponentsComponent>();
                        }

                        l_Entity.Get<UnknownComponentsComponent>().Entries.push_back({ TaggedString<MemoryTag::Scene>(l_Name), TaggedString<MemoryTag::Scene>(YAML::Dump(it_Pair.second)) });
                        ++unknown[l_Name];

                        continue;
                    }

                    LoadComponent(l_Entity, *l_Serializer, *l_Storage, it_Pair.second);
                }
            }

            for (const PlannedEntity& it_Planned : plan)
            {
                if (!it_Planned.Absorbed && it_Planned.Template != nullptr)
                {
                    ApplyInstanceChanges(scene, it_Planned, unknown);
                }
            }
        }

        // The paths, by key and by index, at which the node differs from the base, each with what the node holds there. A block map or sequence is compared within, unless the base has a key the map lacks, a key could not be told from a path, or the sequences' lengths differ, when the whole of it differs
        void DiffNode(const YAML::Node& node, const YAML::Node& base, const std::string& path, std::vector<std::pair<std::string, YAML::Node>>& changes)
        {
            const auto a_Join = [&path](std::string_view key) { return path.empty() ? std::string(key) : std::format("{}{}{}", path, c_PathSeparator, key); };
            if (IsBranch(node) && IsBranch(base) && node.Type() == base.Type())
            {
                if (node.IsMap())
                {
                    const auto a_IsPathKey = [](const YAML::Node& key) { return key.IsScalar() && !key.Scalar().empty() && key.Scalar().find(c_PathSeparator) == std::string::npos; };
                    const bool l_ByKey = std::ranges::all_of(node, [&a_IsPathKey](const auto& pair) { return a_IsPathKey(pair.first); }) && std::ranges::all_of(base, [&a_IsPathKey, &node](const auto& pair) { return a_IsPathKey(pair.first) && node[pair.first.Scalar()]; });
                    if (l_ByKey)
                    {
                        for (const auto& it_Pair : node)
                        {
                            const std::string& l_Key = it_Pair.first.Scalar();
                            if (const YAML::Node l_Base = base[l_Key])
                            {
                                DiffNode(it_Pair.second, l_Base, a_Join(l_Key), changes);
                            }
                            else
                            {
                                changes.emplace_back(a_Join(l_Key), it_Pair.second);
                            }
                        }

                        return;
                    }
                }
                else if (node.size() == base.size())
                {
                    for (std::size_t it_Index = 0; it_Index < node.size(); ++it_Index)
                    {
                        DiffNode(node[it_Index], base[it_Index], a_Join(std::to_string(it_Index)), changes);
                    }

                    return;
                }
            }

            if (YAML::Dump(node) != YAML::Dump(base))
            {
                changes.emplace_back(path, node);
            }
        }

        // What an instance's entity changes of the prefab's: the values of the components both have, the components only it has, which a component whose whole map changed is too, and those only the prefab's has
        struct EntityChanges
        {
            UUID Local;
            std::vector<std::pair<std::string, std::vector<std::pair<std::string, YAML::Node>>>> Overrides;
            std::vector<std::pair<std::string, YAML::Node>> Added;
            std::vector<std::string> Removed;

            [[nodiscard]] bool IsEmpty() const { return Overrides.empty() && Added.empty() && Removed.empty(); }
        };

        // Each component's YAML, in the order a scene file has them, by name
        std::vector<std::pair<std::string, std::string>> GetComponentYamls(Entity entity)
        {
            std::vector<std::pair<std::string, std::string>> l_Yamls;
            const SceneRegistry& l_Registry = entity.GetScene()->GetRegistry();
            for (const ComponentSerializer& it_Serializer : s_State->Serializers)
            {
                const SceneRegistry::common_type* l_Storage = l_Registry.storage(GetPoolId(it_Serializer.Name));
                if (l_Storage != nullptr && l_Storage->contains(entity.GetHandle()))
                {
                    l_Yamls.emplace_back(std::string(it_Serializer.Name), SaveComponentYaml(it_Serializer, l_Storage->value(entity.GetHandle())));
                }
            }

            if (entity.Has<UnknownComponentsComponent>())
            {
                for (const UnknownComponentsComponent::Entry& it_Kept : entity.Get<UnknownComponentsComponent>().Entries)
                {
                    l_Yamls.emplace_back(std::string(std::string_view(it_Kept.Name)), std::string(std::string_view(it_Kept.Yaml)));
                }
            }

            return l_Yamls;
        }

        YAML::Node LoadYaml(const std::string& yaml)
        {
            try
            {
                return YAML::Load(yaml);
            }
            catch (const YAML::Exception&)
            {
                return YAML::Node(yaml);
            }
        }

        // Compared as their text first, which is the same for every component the instance has as the prefab has it
        EntityChanges DiffEntity(Entity entity, Entity base)
        {
            EntityChanges l_Changes;
            const std::vector<std::pair<std::string, std::string>> l_Own = GetComponentYamls(entity);
            const std::vector<std::pair<std::string, std::string>> l_Base = GetComponentYamls(base);
            for (const auto& [it_Name, it_Yaml] : l_Own)
            {
                const auto a_Base = std::ranges::find(l_Base, it_Name, &std::pair<std::string, std::string>::first);
                if (a_Base == l_Base.end())
                {
                    l_Changes.Added.emplace_back(it_Name, LoadYaml(it_Yaml));

                    continue;
                }

                if (a_Base->second == it_Yaml)
                {
                    continue;
                }

                const YAML::Node l_Node = LoadYaml(it_Yaml);
                std::vector<std::pair<std::string, YAML::Node>> l_Values;
                DiffNode(l_Node, LoadYaml(a_Base->second), {}, l_Values);
                if (l_Values.size() == 1 && l_Values.front().first.empty() && IsBranch(l_Values.front().second))
                {
                    l_Changes.Added.emplace_back(it_Name, l_Node);
                }
                else if (!l_Values.empty())
                {
                    l_Changes.Overrides.emplace_back(it_Name, std::move(l_Values));
                }
            }

            for (const auto& [it_Name, it_Yaml] : l_Base)
            {
                if (std::ranges::find(l_Own, it_Name, &std::pair<std::string, std::string>::first) == l_Own.end())
                {
                    l_Changes.Removed.push_back(it_Name);
                }
            }

            return l_Changes;
        }

        bool IsInSubtree(Entity entity, Entity root)
        {
            for (Entity it_Entity = entity; it_Entity; it_Entity = it_Entity.GetParent())
            {
                if (it_Entity == root)
                {
                    return true;
                }
            }

            return false;
        }

        // An instance whose prefab is missing, as the file had it
        void AppendKept(std::string& text, const PrefabInstanceComponent& instance, UUID parent)
        {
            text += std::format("\n  - {}: {}", c_InstanceKey, ToHex(instance.Instance));
            if (parent.IsValid())
            {
                text += std::format("\n    {}: {}", c_ParentKey, ToHex(parent));
            }

            text += instance.Kept;
        }

        // What an instance changes of its prefab, against what it would be unchanged, which is made in the scratch scene: its prefab's entities with the UUIDs it gives them, what each it has changes, and the local ID of each it removes, once for the whole subtree
        struct InstanceDiff
        {
            std::vector<std::pair<entt::entity, UUID>> Members;
            std::vector<EntityChanges> Changes;
            std::vector<UUID> Removed;
        };

        // Its prefab's entities it has are covered by it. Nothing for a prefab the resolver does not find, or a root that is not the instance's
        std::optional<InstanceDiff> DiffInstance(Entity root, Scope<Scene>& scratch, std::unordered_set<UUID>& covered)
        {
            const PrefabInstanceComponent& l_Instance = root.Get<PrefabInstanceComponent>();
            Scene* l_Template = SceneSerializer::FindPrefabTemplate(l_Instance.Prefab);
            if (l_Template == nullptr || !l_Template->GetFirstRoot())
            {
                return std::nullopt;
            }

            InstanceDiff l_Diff{ .Members = GetInstanceMembers(*l_Template, l_Instance.Instance,{}), .Changes = {}, .Removed = {} };
            if (l_Diff.Members.front().second != root.GetUUID())
            {
                return std::nullopt;
            }

            if (!scratch)
            {
                scratch = CreateScope<Scene>();
            }

            scratch->Clear();
            static_cast<void>(ExpandInstance(*scratch, *l_Template, l_Diff.Members, l_Instance.Prefab, l_Instance.Instance));

            Scene& l_Scene = *root.GetScene();
            const SceneRegistry& l_Prefab = l_Template->GetRegistry();
            std::unordered_set<UUID> l_Gone;
            for (const auto& [it_Member, it_UUID] : l_Diff.Members)
            {
                const UUID l_Local = l_Prefab.get<IDComponent>(it_Member).ID;
                const Entity l_Entity = l_Scene.FindEntityByUUID(it_UUID);
                if (!l_Entity || !IsInSubtree(l_Entity, root))
                {
                    const entt::entity l_Parent = l_Prefab.get<RelationshipComponent>(it_Member).Parent;
                    if (l_Parent == entt::null || !l_Gone.contains(l_Prefab.get<IDComponent>(l_Parent).ID))
                    {
                        l_Diff.Removed.push_back(l_Local);
                    }

                    l_Gone.insert(l_Local);

                    continue;
                }

                covered.insert(it_UUID);
                EntityChanges l_EntityChanges = DiffEntity(l_Entity, scratch->FindEntityByUUID(it_UUID));
                if (!l_EntityChanges.IsEmpty())
                {
                    l_EntityChanges.Local = l_Local;
                    l_Diff.Changes.push_back(std::move(l_EntityChanges));
                }
            }

            return l_Diff;
        }

        // An instance as its prefab and what it changes of it: each value of the components it holds otherwise, by its key, each component it adds or removes, and each of the prefab's entities it removes. Its prefab's entities are covered by it, so only those added under them are written as entities of their own. False, writing nothing, for a prefab the resolver does not find, or a root that is not the instance's
        bool AppendInstance(std::string& text, Entity root, UUID parent, Scope<Scene>& scratch, std::unordered_set<UUID>& covered)
        {
            const std::optional<InstanceDiff> l_Diff = DiffInstance(root, scratch, covered);
            if (!l_Diff)
            {
                return false;
            }

            const PrefabInstanceComponent& l_Instance = root.Get<PrefabInstanceComponent>();
            const std::vector<EntityChanges>& l_Changes = l_Diff->Changes;
            const std::vector<UUID>& l_Removed = l_Diff->Removed;
            text += std::format("\n  - {}: {}", c_InstanceKey, ToHex(l_Instance.Instance));
            if (parent.IsValid())
            {
                text += std::format("\n    {}: {}", c_ParentKey, ToHex(parent));
            }

            text += std::format("\n    {}: {}", c_PrefabKey, ToHex(l_Instance.Prefab));

            const auto a_HasAny = [&l_Changes](auto member) { return std::ranges::any_of(l_Changes, [member](const EntityChanges& changes) { return !(changes.*member).empty(); }); };
            if (a_HasAny(&EntityChanges::Overrides))
            {
                text += std::format("\n    {}:", c_OverridesKey);
                for (const EntityChanges& it_Entity : l_Changes)
                {
                    if (it_Entity.Overrides.empty())
                    {
                        continue;
                    }

                    text += std::format("\n      {}:", ToHex(it_Entity.Local));
                    for (const auto& [it_Name, it_Values] : it_Entity.Overrides)
                    {
                        text += std::format("\n        {}:", FormatString(it_Name));
                        if (it_Values.size() == 1 && it_Values.front().first.empty())
                        {
                            AppendNode(text, it_Values.front().second, 10);

                            continue;
                        }

                        for (const auto& [it_Path, it_Value] : it_Values)
                        {
                            text += std::format("\n          {}:", FormatString(it_Path));
                            AppendNode(text, it_Value, 12);
                        }
                    }
                }
            }

            if (a_HasAny(&EntityChanges::Added))
            {
                text += std::format("\n    {}:", c_AddedComponentsKey);
                for (const EntityChanges& it_Entity : l_Changes)
                {
                    if (it_Entity.Added.empty())
                    {
                        continue;
                    }

                    text += std::format("\n      {}:", ToHex(it_Entity.Local));
                    for (const auto& [it_Name, it_Node] : it_Entity.Added)
                    {
                        text += std::format("\n        {}:", FormatString(it_Name));
                        AppendNode(text, it_Node, 10);
                    }
                }
            }

            if (a_HasAny(&EntityChanges::Removed))
            {
                text += std::format("\n    {}:", c_RemovedComponentsKey);
                for (const EntityChanges& it_Entity : l_Changes)
                {
                    if (it_Entity.Removed.empty())
                    {
                        continue;
                    }

                    text += std::format("\n      {}: [", ToHex(it_Entity.Local));
                    for (std::size_t it_Index = 0; it_Index < it_Entity.Removed.size(); ++it_Index)
                    {
                        text += std::format("{}{}", it_Index == 0 ? "" : ", ", FormatString(it_Entity.Removed[it_Index]));
                    }

                    text += ']';
                }
            }

            if (!l_Removed.empty())
            {
                text += std::format("\n    {}: [", c_RemovedEntitiesKey);
                for (std::size_t it_Index = 0; it_Index < l_Removed.size(); ++it_Index)
                {
                    text += std::format("{}{}", it_Index == 0 ? "" : ", ", ToHex(l_Removed[it_Index]));
                }

                text += ']';
            }

            return true;
        }

        // Each entity in hierarchy order from the root, or from the scene's first root for none, each instance as its prefab and what it changes, which covers its prefab's entities, and one whose prefab is missing as the file had it. The root's parent is not written
        void AppendHierarchy(std::string& text, Scene& scene, Entity root)
        {
            Scope<Scene> l_Scratch;
            std::unordered_set<UUID> l_Covered;
            const SceneRegistry& l_Registry = scene.GetRegistry();
            for (Entity it_Entity = root ? root : scene.GetFirstRoot(); it_Entity; it_Entity = root ? scene.GetNextInSubtree(it_Entity, root) : scene.GetNextInHierarchyOrder(it_Entity))
            {
                if (l_Covered.contains(it_Entity.GetUUID()))
                {
                    continue;
                }

                const Entity l_Parent = it_Entity.GetParent();
                const UUID l_ParentID = it_Entity != root && l_Parent ? l_Parent.GetUUID() : UUID();
                if (const PrefabInstanceComponent* l_Instance = l_Registry.try_get<PrefabInstanceComponent>(it_Entity.GetHandle()))
                {
                    if (l_Instance->IsMissing())
                    {
                        AppendKept(text, *l_Instance, l_ParentID);

                        continue;
                    }

                    if (AppendInstance(text, it_Entity, l_ParentID, l_Scratch, l_Covered))
                    {
                        continue;
                    }

                    TR_CORE_WARN("Scene: prefab {} of instance {} is not there, so the instance's entities are written as entities of their own", l_Instance->Prefab, l_Instance->Instance);
                }

                AppendEntity(text, it_Entity, it_Entity.GetUUID(), l_ParentID);
            }
        }

        // What the instance changes of its prefab, as GetPrefabOverrides gives it, with what it would be unchanged left in the scratch scene. Nothing for an entity that is no instance's root, or one whose prefab the resolver does not find
        std::optional<std::vector<SceneSerializer::PrefabOverride>> ReadOverrides(Entity root, Scope<Scene>& scratch)
        {
            using Kind = SceneSerializer::PrefabOverride::Kind;

            if (!root.IsValid() || !root.Has<PrefabInstanceComponent>() || root.Get<PrefabInstanceComponent>().IsMissing())
            {
                return std::nullopt;
            }

            std::unordered_set<UUID> l_Covered;
            const std::optional<InstanceDiff> l_Diff = DiffInstance(root, scratch, l_Covered);
            if (!l_Diff)
            {
                return std::nullopt;
            }

            const UUID l_Instance = root.Get<PrefabInstanceComponent>().Instance;
            std::unordered_map<UUID, const EntityChanges*> l_Changes;
            for (const EntityChanges& it_Changes : l_Diff->Changes)
            {
                l_Changes.emplace(it_Changes.Local, &it_Changes);
            }

            const std::unordered_set<UUID> l_Removed(l_Diff->Removed.begin(), l_Diff->Removed.end());
            Scene& l_Scene = *root.GetScene();
            std::vector<SceneSerializer::PrefabOverride> l_Overrides;
            const auto a_Add = [&l_Overrides](Kind type, UUID entity, std::string component, std::string path) { l_Overrides.push_back({ .Type = type, .Entity = entity, .Component = std::move(component), .Path = std::move(path) }); };
            for (const auto& [it_Member, it_UUID] : l_Diff->Members)
            {
                if (l_Removed.contains(Prefab::FindLocalID(l_Instance, it_UUID)))
                {
                    a_Add(Kind::RemovedEntity, it_UUID, {}, {});

                    continue;
                }

                if (!l_Covered.contains(it_UUID))
                {
                    continue;
                }

                if (const auto a_Changes = l_Changes.find(Prefab::FindLocalID(l_Instance, it_UUID)); a_Changes != l_Changes.end())
                {
                    const EntityChanges& l_Entity = *a_Changes->second;
                    for (const auto& [it_Name, it_Values] : l_Entity.Overrides)
                    {
                        for (const auto& [it_Path, it_Value] : it_Values)
                        {
                            a_Add(Kind::Value, it_UUID, it_Name, it_Path);
                        }
                    }

                    // A component replaced whole is kept with those added, and is a value of one the prefab has
                    const Entity l_Base = scratch->FindEntityByUUID(it_UUID);
                    for (const auto& [it_Name, it_Node] : l_Entity.Added)
                    {
                        a_Add(GetComponentYaml(l_Base, it_Name) ? Kind::Value : Kind::AddedComponent, it_UUID, it_Name, {});
                    }

                    for (const std::string& it_Name : l_Entity.Removed)
                    {
                        a_Add(Kind::RemovedComponent, it_UUID, it_Name, {});
                    }
                }

                for (Entity it_Child = l_Scene.FindEntityByUUID(it_UUID).GetFirstChild(); it_Child; it_Child = it_Child.GetNextSibling())
                {
                    if (!l_Covered.contains(it_Child.GetUUID()))
                    {
                        a_Add(Kind::AddedEntity, it_Child.GetUUID(), {}, {});
                    }
                }
            }

            return l_Overrides;
        }

        // The overrides checked against what the instance has, each taken once, with what it would be unchanged in the scratch scene
        Expected<void, std::string> CheckOverrides(Entity root, std::span<const SceneSerializer::PrefabOverride> overrides, Scope<Scene>& scratch)
        {
            std::optional<std::vector<SceneSerializer::PrefabOverride>> l_Has = ReadOverrides(root, scratch);
            if (!l_Has)
            {
                return Unexpected{ std::format("entity {} is no root of an instance whose prefab is there", root.IsValid() ? root.GetUUID() : UUID()) };
            }

            for (const SceneSerializer::PrefabOverride& it_Override : overrides)
            {
                const auto a_Found = std::ranges::find(*l_Has, it_Override);
                if (a_Found == l_Has->end())
                {
                    return Unexpected{ std::format("instance {} has no such change of entity {}{}{}, or it is given twice", root.GetUUID(), it_Override.Entity, it_Override.Component.empty() ? "" : " in ", it_Override.Component) };
                }

                l_Has->erase(a_Found);
            }

            return {};
        }

        // By its serializer, or as it is kept when no loaded code knows it
        bool RemoveNamedComponent(Entity entity, std::string_view name)
        {
            return FindStorage(entity, name) != nullptr ? SceneSerializer::RemoveComponent(entity, name) : RemoveKept(entity, name);
        }

        // The value at the path, or the whole component for none, made as the source's, and taken out where the source has nothing there
        void CopyComponentValue(Entity target, Entity source, std::string_view name, std::string_view path, std::unordered_map<std::string, std::size_t>& unknown)
        {
            const std::optional<std::string> l_Source = GetComponentYaml(source, name);
            const std::optional<std::string> l_Target = GetComponentYaml(target, name);
            if (!l_Source)
            {
                return;
            }

            if (path.empty() || !l_Target)
            {
                SetComponentYaml(target, name, LoadYaml(*l_Source), unknown);

                return;
            }

            YAML::Node l_Node = LoadYaml(*l_Target);
            if (const std::optional<YAML::Node> l_Value = GetAtPath(LoadYaml(*l_Source), path))
            {
                static_cast<void>(SetAtPath(l_Node, path, *l_Value));
            }
            else
            {
                static_cast<void>(RemoveAtPath(l_Node, path));
            }

            SetComponentYaml(target, name, l_Node, unknown);
        }

        // One of the prefab's entities the instance removed, made again with its subtree as the prefab has them, before the next of its prefab's siblings the instance has, or else before the first entity added there, so it is where a reload puts it
        Expected<void, std::string> RestoreRemovedEntity(Scene& scene, Scene& scratch, UUID entity)
        {
            const Entity l_Base = scratch.FindEntityByUUID(entity);
            const Entity l_Parent = scene.FindEntityByUUID(l_Base.GetParent().GetUUID());
            Entity l_Before;
            for (Entity it_Sibling = l_Base.GetNextSibling(); it_Sibling && !l_Before; it_Sibling = it_Sibling.GetNextSibling())
            {
                const Entity l_Own = scene.FindEntityByUUID(it_Sibling.GetUUID());
                if (l_Own && l_Own.GetParent() == l_Parent)
                {
                    l_Before = l_Own;
                }
            }

            for (Entity it_Child = l_Parent.GetFirstChild(); it_Child && !l_Before; it_Child = it_Child.GetNextSibling())
            {
                if (!scratch.FindEntityByUUID(it_Child.GetUUID()))
                {
                    l_Before = it_Child;
                }
            }

            const Expected<Entity, std::string> l_Made = SceneSerializer::LoadEntityFromText(scene, SceneSerializer::SaveEntityToText(scratch, l_Base), l_Parent, l_Before);
            if (!l_Made)
            {
                return Unexpected{ std::format("entity {} could not be made again: {}", entity, l_Made.GetError()) };
            }

            return {};
        }

        // The YAML a scene file read without the component's code keeps for it, so a component kept this way saves as one kept from a file does
        std::string ToKeptYaml(const ComponentSerializer& serializer, const void* component)
        {
            std::string l_Text;
            ComponentWriter l_Writer(l_Text, 0);
            serializer.Save(component, l_Writer);
            try
            {
                return YAML::Dump(YAML::Load(l_Text));
            }
            catch (const YAML::Exception& exception)
            {
                TR_CORE_ERROR("Scene: {} wrote text that is not valid YAML, kept as it is: {}", serializer.Name, exception.what());

                return l_Text;
            }
        }

        // Each of the component in the scene, as YAML in the entity's unknown components, and then the pool, whose code may be about to unload. It goes first among them, where a known component saves, so a module unloading in reverse of how it registered leaves the text as it was
        std::size_t KeepAsYaml(Scene& scene, const ComponentSerializer& serializer)
        {
            SceneRegistry& l_Registry = scene.GetRegistry();
            const entt::id_type l_PoolId = GetPoolId(serializer.Name);
            const SceneRegistry::common_type* l_Storage = l_Registry.storage(l_PoolId);
            if (l_Storage == nullptr)
            {
                return 0;
            }

            const std::vector<entt::entity> l_Entities(l_Storage->begin(), l_Storage->end());
            for (const entt::entity it_Handle : l_Entities)
            {
                Entity l_Entity(it_Handle, &scene);
                std::string l_Yaml = ToKeptYaml(serializer, l_Storage->value(it_Handle));
                if (!l_Entity.Has<UnknownComponentsComponent>())
                {
                    l_Entity.Add<UnknownComponentsComponent>();
                }

                auto& l_Entries = l_Entity.Get<UnknownComponentsComponent>().Entries;
                l_Entries.insert(l_Entries.begin(), { TaggedString<MemoryTag::Scene>(serializer.Name), TaggedString<MemoryTag::Scene>(l_Yaml) });
            }

            static_cast<void>(l_Registry.reset(l_PoolId));

            return l_Entities.size();
        }

        // The pool, then each of the component the scene keeps as YAML, loaded into it and no longer kept. YAML that cannot be read stays kept
        std::size_t LoadKept(Scene& scene, const ComponentSerializer& serializer)
        {
            SceneRegistry& l_Registry = scene.GetRegistry();
            serializer.CreateStorage(l_Registry);
            SceneRegistry::common_type* l_Storage = l_Registry.storage(GetPoolId(serializer.Name));
            if (l_Storage == nullptr)
            {
                TR_CORE_ERROR("Scene: {} made no pool under its own name", serializer.Name);

                return 0;
            }

            const auto l_View = l_Registry.view<UnknownComponentsComponent>();
            const std::vector<entt::entity> l_Entities(l_View.begin(), l_View.end());
            std::size_t l_Loaded = 0;
            for (const entt::entity it_Handle : l_Entities)
            {
                Entity l_Entity(it_Handle, &scene);
                auto& l_Entries = l_Entity.Get<UnknownComponentsComponent>().Entries;
                const auto a_Kept = std::ranges::find_if(l_Entries, [&serializer](const UnknownComponentsComponent::Entry& entry) { return std::string_view(entry.Name) == serializer.Name; });
                if (a_Kept == l_Entries.end())
                {
                    continue;
                }

                YAML::Node l_Node;
                try
                {
                    l_Node = YAML::Load(std::string(std::string_view(a_Kept->Yaml)));
                }
                catch (const YAML::Exception& exception)
                {
                    TR_CORE_ERROR("Scene: {} on entity {} is kept as YAML that cannot be read, and stays kept: {}", serializer.Name, l_Entity.GetUUID(), exception.what());

                    continue;
                }

                LoadComponent(l_Entity, serializer, *l_Storage, l_Node);
                l_Entries.erase(a_Kept);
                if (l_Entries.empty())
                {
                    l_Entity.Remove<UnknownComponentsComponent>();
                }

                ++l_Loaded;
            }

            return l_Loaded;
        }
    }

    ComponentWriter::ComponentWriter(std::string& output, std::uint32_t indent) : m_Output(&output), m_Indent(indent)
    {

    }

    void ComponentWriter::WriteKey(std::string_view key)
    {
        WriteKeyName(key);
        *m_Output += ' ';
    }

    // Inside an inline map after a comma, else on a line of its own, after the dash when it is an item's first key
    void ComponentWriter::WriteKeyName(std::string_view key)
    {
        Block* l_Block = m_Depth != 0 ? &m_Blocks[m_Depth - 1] : nullptr;
        if (l_Block != nullptr && l_Block->Kind == BlockKind::InlineMap)
        {
            *m_Output += l_Block->Empty ? "" : ", ";
        }
        else if (m_ItemPending)
        {
            *m_Output += '\n';
            m_Output->append(m_Indent - 2, ' ');
            *m_Output += "- ";
            m_ItemPending = false;
        }
        else
        {
            *m_Output += '\n';
            m_Output->append(m_Indent, ' ');
        }

        if (l_Block != nullptr)
        {
            l_Block->Empty = false;
        }

        *m_Output += FormatString(key);
        *m_Output += ':';
    }

    void ComponentWriter::Push(BlockKind kind)
    {
        TR_CORE_ASSERT(m_Depth < c_MaxDepth, "ComponentWriter: blocks are nested deeper than {}", c_MaxDepth);

        m_Blocks[m_Depth++] = { kind, m_Indent, true };
    }

    // Items' dashes one level in and their keys two, as Fields under Scripts are
    void ComponentWriter::BeginList(std::string_view key)
    {
        WriteKeyName(key);
        Push(BlockKind::List);
        m_Indent += 4;
    }

    // An item left without keys is written as an empty map, so the dash is never lost
    void ComponentWriter::BeginItem()
    {
        TR_CORE_ASSERT(m_Depth != 0 && m_Blocks[m_Depth - 1].Kind == BlockKind::List, "ComponentWriter: BeginItem outside a list");

        if (m_ItemPending)
        {
            *m_Output += '\n';
            m_Output->append(m_Indent - 2, ' ');
            *m_Output += "- {}";
        }

        m_Blocks[m_Depth - 1].Empty = false;
        m_ItemPending = true;
    }

    void ComponentWriter::BeginMap(std::string_view key)
    {
        WriteKeyName(key);
        Push(BlockKind::Map);
        m_Indent += 2;
    }

    void ComponentWriter::BeginInlineMap(std::string_view key)
    {
        WriteKey(key);
        *m_Output += '{';
        Push(BlockKind::InlineMap);
    }

    void ComponentWriter::End()
    {
        TR_CORE_ASSERT(m_Depth != 0, "ComponentWriter: End without a block to end");

        const Block l_Block = m_Blocks[--m_Depth];
        switch (l_Block.Kind)
        {
            case BlockKind::List:
            {
                if (m_ItemPending)
                {
                    *m_Output += '\n';
                    m_Output->append(m_Indent - 2, ' ');
                    *m_Output += "- {}";
                    m_ItemPending = false;
                }

                *m_Output += l_Block.Empty ? " []" : "";

                break;
            }
            case BlockKind::Map:
            {
                *m_Output += l_Block.Empty ? " {}" : "";

                break;
            }
            case BlockKind::InlineMap:
            {
                *m_Output += '}';

                break;
            }
        }

        m_Indent = l_Block.Indent;
    }

    void ComponentWriter::Write(std::string_view key, bool value)
    {
        WriteKey(key);
        *m_Output += value ? "true" : "false";
    }

    void ComponentWriter::Write(std::string_view key, std::int32_t value)
    {
        WriteKey(key);
        *m_Output += std::to_string(value);
    }

    void ComponentWriter::Write(std::string_view key, std::uint32_t value)
    {
        WriteKey(key);
        *m_Output += std::to_string(value);
    }

    void ComponentWriter::Write(std::string_view key, std::int64_t value)
    {
        WriteKey(key);
        *m_Output += std::to_string(value);
    }

    void ComponentWriter::Write(std::string_view key, std::uint64_t value)
    {
        WriteKey(key);
        *m_Output += std::to_string(value);
    }

    void ComponentWriter::Write(std::string_view key, float value)
    {
        WriteKey(key);
        *m_Output += FormatFloat(value);
    }

    void ComponentWriter::Write(std::string_view key, double value)
    {
        WriteKey(key);
        *m_Output += FormatFloat(value);
    }

    void ComponentWriter::Write(std::string_view key, const char* value)
    {
        Write(key, std::string_view(value));
    }

    void ComponentWriter::Write(std::string_view key, std::string_view value)
    {
        WriteKey(key);
        *m_Output += FormatString(value);
    }

    void ComponentWriter::Write(std::string_view key, UUID value)
    {
        WriteKey(key);
        *m_Output += ToHex(value);
    }

    // As a flow sequence, so an empty list is [] rather than nothing
    void ComponentWriter::Write(std::string_view key, std::span<const UUID> values)
    {
        WriteKey(key);
        *m_Output += '[';
        for (std::size_t it_Index = 0; it_Index < values.size(); ++it_Index)
        {
            *m_Output += it_Index == 0 ? "" : ", ";
            *m_Output += ToHex(values[it_Index]);
        }

        *m_Output += ']';
    }

    // One flow sequence of pairs, such as [[0, 0.5], [-0.5, -0.5]]
    void ComponentWriter::Write(std::string_view key, std::span<const glm::vec2> values)
    {
        WriteKey(key);
        *m_Output += '[';
        for (std::size_t it_Index = 0; it_Index < values.size(); ++it_Index)
        {
            *m_Output += it_Index == 0 ? "" : ", ";
            *m_Output += FormatFloats(std::array{ values[it_Index].x, values[it_Index].y });
        }

        *m_Output += ']';
    }

    void ComponentWriter::Write(std::string_view key, const glm::vec2& value)
    {
        WriteKey(key);
        *m_Output += FormatFloats(std::array{ value.x, value.y });
    }

    void ComponentWriter::Write(std::string_view key, const glm::vec3& value)
    {
        WriteKey(key);
        *m_Output += FormatFloats(std::array{ value.x, value.y, value.z });
    }

    void ComponentWriter::Write(std::string_view key, const glm::vec4& value)
    {
        WriteKey(key);
        *m_Output += FormatFloats(std::array{ value.x, value.y, value.z, value.w });
    }

    void ComponentWriter::Write(std::string_view key, const glm::quat& value)
    {
        WriteKey(key);
        *m_Output += FormatFloats(std::array{ value.x, value.y, value.z, value.w });
    }

    void ComponentWriter::Write(std::string_view key, const ScriptFieldValue& value)
    {
        BeginInlineMap(key);
        Write("Type", ToString(value.GetType()));
        std::visit([this](const auto& data)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(data)>, ScriptString>)
            {
                Write("Value", std::string_view(data));
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(data)>, ScriptList>)
            {
                Write("Element", ToString(data.ElementType));
                WriteKey("Value");
                *m_Output += '[';
                for (std::size_t it_Index = 0; it_Index < data.Elements.size(); ++it_Index)
                {
                    *m_Output += it_Index == 0 ? "" : ", ";
                    *m_Output += FormatElement(data.Elements[it_Index]);
                }

                *m_Output += ']';
            }
            else
            {
                Write("Value", data);
            }
        }, value.GetStorage());
        End();
    }

    void ComponentWriter::WriteValue(std::string_view value)
    {
        *m_Output += ' ';
        *m_Output += FormatString(value);
    }

    ComponentReader::ComponentReader(const YAML::Node& node, std::string_view componentName) : m_Node(&node), m_ComponentName(componentName)
    {

    }

    namespace
    {
        std::optional<YAML::Node> FindValue(const YAML::Node& component, std::string_view key)
        {
            if (!component.IsMap())
            {
                return std::nullopt;
            }

            const YAML::Node l_Value = component[std::string(key)];
            if (!l_Value)
            {
                return std::nullopt;
            }

            return l_Value;
        }

        bool Malformed(std::string_view componentName, std::string_view key)
        {
            TR_CORE_WARN("Scene: {}.{} is malformed and keeps its default", componentName, key);

            return false;
        }

        // A list's element as a value of its type is read on its own, or false with the element as it was
        bool ParseElement(const YAML::Node& node, ScriptFieldValue& element)
        {
            return std::visit([&node](auto& data)
            {
                using Type = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<Type, bool>)
                {
                    if (!node.IsScalar() || (node.Scalar() != "true" && node.Scalar() != "false"))
                    {
                        return false;
                    }

                    data = node.Scalar() == "true";

                    return true;
                }
                else if constexpr (std::is_arithmetic_v<Type>)
                {
                    return node.IsScalar() && ParseNumber(node.Scalar(), data);
                }
                else if constexpr (std::is_same_v<Type, glm::vec2> || std::is_same_v<Type, glm::vec3> || std::is_same_v<Type, glm::vec4>)
                {
                    return ParseFloatSequence(node, std::span<float>(&data.x, static_cast<std::size_t>(Type::length())));
                }
                else if constexpr (std::is_same_v<Type, glm::quat>)
                {
                    std::array<float, 4> l_Values{};
                    if (!ParseFloatSequence(node, l_Values))
                    {
                        return false;
                    }

                    data = glm::quat(l_Values[3], l_Values[0], l_Values[1], l_Values[2]);

                    return true;
                }
                else if constexpr (std::is_same_v<Type, ScriptString>)
                {
                    if (!node.IsScalar())
                    {
                        return false;
                    }

                    data = node.Scalar();

                    return true;
                }
                else if constexpr (std::is_same_v<Type, UUID>)
                {
                    const std::optional<UUID> l_UUID = node.IsScalar() ? UUID::Parse(node.Scalar()) : std::nullopt;
                    if (!l_UUID)
                    {
                        return false;
                    }

                    data = *l_UUID;

                    return true;
                }
                else
                {
                    // An element is never a list
                    return false;
                }
            }, element.GetStorage());
        }

        // Every element or none, so a list with one malformed element keeps its default, which is empty
        bool ParseList(const YAML::Node& node, ScriptList& list)
        {
            if (!node.IsSequence())
            {
                return false;
            }

            ScriptList l_List{ list.ElementType,{} };
            l_List.Elements.reserve(node.size());
            for (const YAML::Node it_Node : node)
            {
                ScriptFieldValue l_Element(list.ElementType);
                if (!ParseElement(it_Node, l_Element))
                {
                    return false;
                }

                l_List.Elements.push_back(std::move(l_Element));
            }

            list = std::move(l_List);

            return true;
        }

        template<typename T>
        bool ReadNumber(const YAML::Node& component, std::string_view componentName, std::string_view key, T& value)
        {
            const std::optional<YAML::Node> l_Value = FindValue(component, key);
            if (!l_Value)
            {
                return false;
            }

            return (l_Value->IsScalar() && ParseNumber(l_Value->Scalar(), value)) || Malformed(componentName, key);
        }

        template<std::size_t N>
        bool ReadFloats(const YAML::Node& component, std::string_view componentName, std::string_view key, std::span<float, N> values)
        {
            const std::optional<YAML::Node> l_Value = FindValue(component, key);
            if (!l_Value)
            {
                return false;
            }

            return ParseFloatSequence(*l_Value, values) || Malformed(componentName, key);
        }
    }

    bool ComponentReader::Read(std::string_view key, bool& value) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        if (l_Value->IsScalar() && (l_Value->Scalar() == "true" || l_Value->Scalar() == "false"))
        {
            value = l_Value->Scalar() == "true";

            return true;
        }

        return Malformed(m_ComponentName, key);
    }

    bool ComponentReader::Read(std::string_view key, std::int32_t& value) const
    {
        return ReadNumber(*m_Node, m_ComponentName, key, value);
    }

    bool ComponentReader::Read(std::string_view key, std::uint32_t& value) const
    {
        return ReadNumber(*m_Node, m_ComponentName, key, value);
    }

    bool ComponentReader::Read(std::string_view key, std::int64_t& value) const
    {
        return ReadNumber(*m_Node, m_ComponentName, key, value);
    }

    bool ComponentReader::Read(std::string_view key, std::uint64_t& value) const
    {
        return ReadNumber(*m_Node, m_ComponentName, key, value);
    }

    bool ComponentReader::Read(std::string_view key, float& value) const
    {
        return ReadNumber(*m_Node, m_ComponentName, key, value);
    }

    bool ComponentReader::Read(std::string_view key, double& value) const
    {
        return ReadNumber(*m_Node, m_ComponentName, key, value);
    }

    bool ComponentReader::Read(std::string_view key, std::string& value) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        if (!l_Value->IsScalar())
        {
            return Malformed(m_ComponentName, key);
        }

        value = l_Value->Scalar();

        return true;
    }

    bool ComponentReader::Read(std::string_view key, UUID& value) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        const std::optional<UUID> l_UUID = l_Value->IsScalar() ? UUID::Parse(l_Value->Scalar()) : std::nullopt;
        if (!l_UUID)
        {
            return Malformed(m_ComponentName, key);
        }

        value = *l_UUID;

        return true;
    }

    bool ComponentReader::Read(std::string_view key, std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scene>>& values) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        if (!l_Value->IsSequence())
        {
            return Malformed(m_ComponentName, key);
        }

        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scene>> l_Values;
        l_Values.reserve(l_Value->size());
        for (const YAML::Node it_Element : *l_Value)
        {
            const std::optional<UUID> l_UUID = it_Element.IsScalar() ? UUID::Parse(it_Element.Scalar()) : std::nullopt;
            if (!l_UUID)
            {
                return Malformed(m_ComponentName, key);
            }

            l_Values.push_back(*l_UUID);
        }

        values = std::move(l_Values);

        return true;
    }

    bool ComponentReader::Read(std::string_view key, std::vector<glm::vec2, TaggedAllocator<glm::vec2, MemoryTag::Scene>>& values) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        if (!l_Value->IsSequence())
        {
            return Malformed(m_ComponentName, key);
        }

        std::vector<glm::vec2, TaggedAllocator<glm::vec2, MemoryTag::Scene>> l_Values;
        l_Values.reserve(l_Value->size());
        for (const YAML::Node it_Element : *l_Value)
        {
            glm::vec2 l_Point{ 0.0f };
            if (!ParseFloatSequence(it_Element, std::span<float>(&l_Point.x, 2)))
            {
                return Malformed(m_ComponentName, key);
            }

            l_Values.push_back(l_Point);
        }

        values = std::move(l_Values);

        return true;
    }

    bool ComponentReader::Read(std::string_view key, glm::vec2& value) const
    {
        return ReadFloats(*m_Node, m_ComponentName, key, std::span<float, 2>(&value.x, 2));
    }

    bool ComponentReader::Read(std::string_view key, glm::vec3& value) const
    {
        return ReadFloats(*m_Node, m_ComponentName, key, std::span<float, 3>(&value.x, 3));
    }

    bool ComponentReader::Read(std::string_view key, glm::vec4& value) const
    {
        return ReadFloats(*m_Node, m_ComponentName, key, std::span<float, 4>(&value.x, 4));
    }

    bool ComponentReader::Read(std::string_view key, glm::quat& value) const
    {
        std::array<float, 4> l_Values{ value.x, value.y, value.z, value.w };
        if (!ReadFloats(*m_Node, m_ComponentName, key, std::span<float, 4>(l_Values)))
        {
            return false;
        }

        value = glm::quat(l_Values[3], l_Values[0], l_Values[1], l_Values[2]);

        return true;
    }

    bool ComponentReader::ReadValue(std::string& value) const
    {
        if (!m_Node->IsScalar())
        {
            TR_CORE_WARN("Scene: {} is not a single value and keeps its default", m_ComponentName);

            return false;
        }

        value = m_Node->Scalar();

        return true;
    }

    bool ComponentReader::Read(std::string_view key, ScriptFieldValue& value) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        return ComponentReader(*l_Value, m_ComponentName).ReadValue(value);
    }

    bool ComponentReader::ReadValue(ScriptFieldValue& value) const
    {
        std::string l_TypeName;
        const std::optional<ScriptFieldType> l_Type = m_Node->IsMap() && Read("Type", l_TypeName) ? ParseScriptFieldType(l_TypeName) : std::nullopt;
        if (!l_Type)
        {
            return false;
        }

        // A list's elements are of any type this build has but a list
        std::string l_ElementName;
        const std::optional<ScriptFieldType> l_Element = *l_Type == ScriptFieldType::List && Read("Element", l_ElementName) ? ParseScriptFieldType(l_ElementName) : std::nullopt;
        if (*l_Type == ScriptFieldType::List && (!l_Element || *l_Element == ScriptFieldType::List))
        {
            return false;
        }

        ScriptFieldValue l_Read = l_Element ? ScriptFieldValue::List(*l_Element) : ScriptFieldValue(*l_Type);
        std::visit([this](auto& data)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(data)>, ScriptString>)
            {
                std::string l_Text;
                if (Read("Value", l_Text))
                {
                    data = l_Text;
                }
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(data)>, ScriptList>)
            {
                const std::optional<YAML::Node> l_Value = FindValue(*m_Node, "Value");
                if (l_Value && !ParseList(*l_Value, data))
                {
                    static_cast<void>(Malformed(m_ComponentName, "Value"));
                }
            }
            else
            {
                Read("Value", data);
            }
        }, l_Read.GetStorage());

        value = std::move(l_Read);

        return true;
    }

    bool ComponentReader::ReadList(std::string_view key, const std::function<void(const ComponentReader& item)>& read) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        if (!l_Value->IsSequence())
        {
            return Malformed(m_ComponentName, key);
        }

        for (const YAML::Node it_Item : *l_Value)
        {
            read(ComponentReader(it_Item, m_ComponentName));
        }

        return true;
    }

    // A key that is not a single value is skipped as malformed
    bool ComponentReader::ReadMap(std::string_view key, const std::function<void(std::string_view name, const ComponentReader& value)>& read) const
    {
        const std::optional<YAML::Node> l_Value = FindValue(*m_Node, key);
        if (!l_Value)
        {
            return false;
        }

        if (!l_Value->IsMap())
        {
            return Malformed(m_ComponentName, key);
        }

        for (const auto& it_Pair : *l_Value)
        {
            if (!it_Pair.first.IsScalar())
            {
                static_cast<void>(Malformed(m_ComponentName, key));

                continue;
            }

            const YAML::Node l_Entry = it_Pair.second;
            read(it_Pair.first.Scalar(), ComponentReader(l_Entry, m_ComponentName));
        }

        return true;
    }

    namespace SceneSerializer
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "The scene serializer is already initialized.");

            s_State = Memory::New<State>(MemoryTag::Engine);

            RegisterComponent(MakeComponentSerializer<TagComponent, SaveTag, LoadTag>());
            RegisterComponent(MakeComponentSerializer<TransformComponent, SaveTransform, LoadTransform>());
            RegisterComponent(MakeComponentSerializer<CameraComponent, SaveCamera, LoadCamera>());
            RegisterComponent(MakeComponentSerializer<SpriteRendererComponent, SaveSpriteRenderer, LoadSpriteRenderer>());
            RegisterComponent(MakeComponentSerializer<MeshRendererComponent, SaveMeshRenderer, LoadMeshRenderer>());
            RegisterComponent(MakeComponentSerializer<LightComponent, SaveLight, LoadLight>());
            RegisterComponent(MakeComponentSerializer<EnvironmentComponent, SaveEnvironment, LoadEnvironment>());
            RegisterComponent(MakeComponentSerializer<RigidBodyComponent, SaveRigidBody, LoadRigidBody>());
            RegisterComponent(MakeComponentSerializer<BoxColliderComponent, SaveBoxCollider, LoadBoxCollider>());
            RegisterComponent(MakeComponentSerializer<SphereColliderComponent, SaveSphereCollider, LoadSphereCollider>());
            RegisterComponent(MakeComponentSerializer<CapsuleColliderComponent, SaveCapsuleCollider, LoadCapsuleCollider>());
            RegisterComponent(MakeComponentSerializer<CylinderColliderComponent, SaveCylinderCollider, LoadCylinderCollider>());
            RegisterComponent(MakeComponentSerializer<ConvexHullColliderComponent, SaveConvexHullCollider, LoadConvexHullCollider>());
            RegisterComponent(MakeComponentSerializer<MeshColliderComponent, SaveMeshCollider, LoadMeshCollider>());
            RegisterComponent(MakeComponentSerializer<CharacterControllerComponent, SaveCharacterController, LoadCharacterController>());
            RegisterComponent(MakeComponentSerializer<JointComponent, SaveJoint, LoadJoint>());
            RegisterComponent(MakeComponentSerializer<RigidBody2DComponent, SaveRigidBody2D, LoadRigidBody2D>());
            RegisterComponent(MakeComponentSerializer<BoxCollider2DComponent, SaveBoxCollider2D, LoadBoxCollider2D>());
            RegisterComponent(MakeComponentSerializer<CircleCollider2DComponent, SaveCircleCollider2D, LoadCircleCollider2D>());
            RegisterComponent(MakeComponentSerializer<CapsuleCollider2DComponent, SaveCapsuleCollider2D, LoadCapsuleCollider2D>());
            RegisterComponent(MakeComponentSerializer<PolygonCollider2DComponent, SavePolygonCollider2D, LoadPolygonCollider2D>());
            RegisterComponent(MakeComponentSerializer<CharacterController2DComponent, SaveCharacterController2D, LoadCharacterController2D>());
            RegisterComponent(MakeComponentSerializer<Joint2DComponent, SaveJoint2D, LoadJoint2D>());
            // It holds nothing, so its functions write and read nothing
            RegisterComponent({ KeepAcrossScenesComponent::c_TypeName, [](const void*, ComponentWriter&) {}, [](void*, const ComponentReader&) {}, [](SceneRegistry& registry) { static_cast<void>(registry.storage<KeepAcrossScenesComponent>()); } });
            RegisterComponent(MakeComponentSerializer<ScriptComponent, SaveScript, LoadScript>());
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            Memory::Delete(s_State);
            s_State = nullptr;
        }

        // A serializer registered before the scene was made gives it its pool here, as one registered after gives it at RegisterComponent
        void AttachScene(Scene& scene)
        {
            if (s_State == nullptr)
            {
                return;
            }

            s_State->Scenes.push_back(&scene);
            for (const ComponentSerializer& it_Serializer : s_State->Serializers)
            {
                if (it_Serializer.CreateStorage != nullptr)
                {
                    it_Serializer.CreateStorage(scene.GetRegistry());
                }
            }
        }

        void DetachScene(Scene& scene)
        {
            if (s_State != nullptr)
            {
                std::erase(s_State->Scenes, &scene);
            }
        }

        void SetPrefabResolver(PrefabResolver resolver)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            s_State->Resolver = std::move(resolver);
        }

        // A prefab the asset manager has ready, until a resolver is set
        Scene* FindPrefabTemplate(UUID prefab)
        {
            if (s_State != nullptr && s_State->Resolver)
            {
                return s_State->Resolver(prefab);
            }

            const Asset* l_Asset = prefab && AssetManager::GetState(prefab) == AssetState::Ready ? AssetManager::GetAsset(prefab) : nullptr;

            return l_Asset != nullptr && l_Asset->GetAssetType() == PrefabAsset::c_AssetType ? static_cast<const PrefabAsset*>(l_Asset)->GetTemplate() : nullptr;
        }

        bool RegisterComponent(const ComponentSerializer& serializer)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            if (serializer.Name.empty() || serializer.Name == c_EntityKey || serializer.Name == c_ParentKey || serializer.Save == nullptr || serializer.Load == nullptr)
            {
                TR_CORE_ERROR("Scene: a component serializer needs a name other than Entity and Parent, and both functions");

                return false;
            }

            if (FindSerializer(serializer.Name) != nullptr)
            {
                TR_CORE_ERROR("Scene: a serializer for {} is already registered", serializer.Name);

                return false;
            }

            s_State->Serializers.push_back(serializer);
            if (serializer.CreateStorage == nullptr)
            {
                return true;
            }

            std::size_t l_Loaded = 0;
            for (Scene* it_Scene : s_State->Scenes)
            {
                l_Loaded += LoadKept(*it_Scene, serializer);
            }

            if (l_Loaded != 0)
            {
                TR_CORE_INFO("Scene: loaded {} {} component(s) kept as YAML while no loaded code knew that component", l_Loaded, serializer.Name);
            }

            return true;
        }

        void UnregisterComponent(std::string_view name)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            const ComponentSerializer* l_Serializer = FindSerializer(name);
            if (l_Serializer == nullptr)
            {
                return;
            }

            std::size_t l_Kept = 0;
            for (Scene* it_Scene : s_State->Scenes)
            {
                l_Kept += KeepAsYaml(*it_Scene, *l_Serializer);
            }

            if (l_Kept != 0)
            {
                TR_CORE_INFO("Scene: kept {} {} component(s) as YAML, since the code that knows that component is unloading", l_Kept, name);
            }

            std::erase_if(s_State->Serializers, [name](const ComponentSerializer& serializer) { return serializer.Name == name; });
        }

        // In the order they were registered, the engine's first
        std::vector<std::string_view> GetComponentNames()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            std::vector<std::string_view> l_Names;
            l_Names.reserve(s_State->Serializers.size());
            for (const ComponentSerializer& it_Serializer : s_State->Serializers)
            {
                l_Names.push_back(it_Serializer.Name);
            }

            return l_Names;
        }

        std::string SaveToText(Scene& scene)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            std::string l_Text = std::format("Format: {}\nEntities:", c_FormatVersion);
            if (!scene.GetFirstRoot())
            {
                l_Text += " []";
            }

            AppendHierarchy(l_Text, scene, {});

            return l_Text + "\n";
        }

        Expected<void, std::string> LoadFromText(Scene& scene, std::string_view text)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            const Expected<YAML::Node, std::string> l_Root = ParseDocument(text);
            if (!l_Root)
            {
                return Unexpected{ l_Root.GetError() };
            }

            const std::vector<std::string_view> l_Lines = SplitLines(text);
            Expected<std::vector<FileEntity>, std::string> l_Entities = ReadEntities(*l_Root, true, l_Lines.size());
            if (!l_Entities)
            {
                return Unexpected{ l_Entities.GetError() };
            }

            Expected<std::vector<PlannedEntity>, std::string> l_Plan = PlanEntities(*l_Entities, l_Lines);
            if (!l_Plan)
            {
                return Unexpected{ l_Plan.GetError() };
            }

            // Nothing can fail from here, so the scene is only replaced once the file is known to be whole
            scene.Clear();

            std::unordered_map<std::string, std::size_t> l_Unknown;
            CreateEntities(scene, *l_Plan, l_Unknown);
            for (const auto& [it_Name, it_Count] : l_Unknown)
            {
                TR_CORE_INFO("Scene: kept {} {} component(s) as YAML, since no loaded code knows that component", it_Count, it_Name);
            }

            return {};
        }

        // The root has no Parent in the text, so it can be put back anywhere
        std::string SaveEntityToText(Scene& scene, Entity root)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");
            TR_CORE_ASSERT(root.GetScene() == &scene && root.IsValid(), "SaveEntityToText was given an entity of another scene, or one already destroyed");

            std::string l_Text = std::format("Format: {}\nEntities:", c_FormatVersion);
            AppendHierarchy(l_Text, scene, root);

            return l_Text + "\n";
        }

        // The entities keep the UUIDs in the text, so none may already be in the scene. The root goes before a sibling when one is given, which must be a child of parent, and otherwise last under parent or among the roots
        Expected<Entity, std::string> LoadEntityFromText(Scene& scene, std::string_view text, Entity parent, Entity before)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            const Expected<YAML::Node, std::string> l_Root = ParseDocument(text);
            if (!l_Root)
            {
                return Unexpected{ l_Root.GetError() };
            }

            const std::vector<std::string_view> l_Lines = SplitLines(text);
            Expected<std::vector<FileEntity>, std::string> l_Entities = ReadEntities(*l_Root, true, l_Lines.size());
            if (!l_Entities)
            {
                return Unexpected{ l_Entities.GetError() };
            }

            const auto a_IsRoot = [](const FileEntity& entity) { return !entity.Parent.IsValid(); };
            if (l_Entities->empty() || !a_IsRoot(l_Entities->front()) || std::any_of(l_Entities->begin() + 1, l_Entities->end(), a_IsRoot))
            {
                return Unexpected{ std::string("does not hold one entity and its subtree, with the entity first") };
            }

            Expected<std::vector<PlannedEntity>, std::string> l_Plan = PlanEntities(*l_Entities, l_Lines);
            if (!l_Plan)
            {
                return Unexpected{ l_Plan.GetError() };
            }

            for (const PlannedEntity& it_Planned : *l_Plan)
            {
                for (const UUID it_UUID : it_Planned.Absorbed ? std::vector<UUID>() : it_Planned.GetEntities())
                {
                    if (scene.FindEntityByUUID(it_UUID))
                    {
                        return Unexpected{ std::format("entity {} is already in the scene", it_UUID) };
                    }
                }
            }

            std::unordered_map<std::string, std::size_t> l_Unknown;
            CreateEntities(scene, *l_Plan, l_Unknown);

            const Entity l_Entity = scene.FindEntityByUUID(l_Plan->front().GetRoot());
            if (before)
            {
                scene.MoveBefore(l_Entity, before, false);
            }
            else if (parent)
            {
                scene.SetParent(l_Entity, parent, false);
            }

            return l_Entity;
        }

        bool HasComponent(Entity entity, std::string_view name)
        {
            const SceneRegistry::common_type* l_Storage = entity.GetScene()->GetRegistry().storage(GetPoolId(name));

            return l_Storage != nullptr && l_Storage->contains(entity.GetHandle());
        }

        // Default constructed. Only a component with a serializer, which the entity is without
        bool AddComponent(Entity entity, std::string_view name)
        {
            SceneRegistry::common_type* l_Storage = FindStorage(entity, name);
            if (l_Storage == nullptr || l_Storage->contains(entity.GetHandle()))
            {
                return false;
            }

            l_Storage->push(entity.GetHandle());

            return true;
        }

        // Transform stays, since every entity has one
        bool RemoveComponent(Entity entity, std::string_view name)
        {
            SceneRegistry::common_type* l_Storage = name != TransformComponent::c_TypeName ? FindStorage(entity, name) : nullptr;
            if (l_Storage == nullptr || !l_Storage->contains(entity.GetHandle()))
            {
                return false;
            }

            l_Storage->erase(entity.GetHandle());

            return true;
        }

        // The component's fields as a scene file holds them, under a Value key, or nothing when the entity is without it
        std::string SaveComponentToText(Entity entity, std::string_view name)
        {
            const SceneRegistry::common_type* l_Storage = FindStorage(entity, name);
            if (l_Storage == nullptr || !l_Storage->contains(entity.GetHandle()))
            {
                return {};
            }

            std::string l_Text = "Value:";
            ComponentWriter l_Writer(l_Text, 2);
            FindSerializer(name)->Save(l_Storage->value(entity.GetHandle()), l_Writer);

            return l_Text + "\n";
        }

        // Adds the component first when the entity is without it
        Expected<void, std::string> LoadComponentFromText(Entity entity, std::string_view name, std::string_view text)
        {
            SceneRegistry::common_type* l_Storage = FindStorage(entity, name);
            if (l_Storage == nullptr)
            {
                return Unexpected{ std::format("no loaded code knows {}", name) };
            }

            YAML::Node l_Root;
            try
            {
                l_Root = YAML::Load(std::string(text));
            }
            catch (const YAML::Exception& exception)
            {
                return Unexpected{ std::format("not valid YAML: {}", exception.what()) };
            }

            const YAML::Node l_Value = l_Root.IsMap() ? l_Root["Value"] : YAML::Node();
            if (!l_Value)
            {
                return Unexpected{ std::string("has no Value") };
            }

            LoadComponent(entity, *FindSerializer(name), *l_Storage, l_Value);

            return {};
        }

        Expected<std::string, std::string> SavePrefabToText(Scene& scene, Entity root, UUID instance)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");
            TR_CORE_ASSERT(root.GetScene() == &scene && root.IsValid(), "SavePrefabToText was given an entity of another scene, or one already destroyed");

            std::unordered_set<UUID> l_Members;
            for (Entity it_Entity = root; it_Entity; it_Entity = scene.GetNextInSubtree(it_Entity, root))
            {
                if (it_Entity.Has<PrefabInstanceComponent>())
                {
                    return Unexpected{ std::format("entity {} is an instance of a prefab, which a prefab cannot hold yet", it_Entity.GetUUID()) };
                }

                if (!Prefab::FindLocalID(instance, it_Entity.GetUUID()).IsValid())
                {
                    return Unexpected{ std::format("instance {} gives entity {} no local ID", instance, it_Entity.GetUUID()) };
                }

                l_Members.insert(it_Entity.GetUUID());
            }

            // A copy, with what it names inside it by local IDs and nothing outside it, so the scene's own entities are left as they are
            Scene l_Copy;
            const Expected<Entity, std::string> l_Copied = LoadEntityFromText(l_Copy, SaveEntityToText(scene, root), {}, {});
            if (!l_Copied)
            {
                return Unexpected{ l_Copied.GetError() };
            }

            l_Copy.RemapEntityReferences(*l_Copied, [&l_Members, instance](UUID& entity) { entity = l_Members.contains(entity) ? Prefab::FindLocalID(instance, entity) : UUID(); });

            std::string l_Text = std::format("Format: {}\nEntities:", Prefab::c_FormatVersion);
            for (Entity it_Entity = *l_Copied; it_Entity; it_Entity = l_Copy.GetNextInSubtree(it_Entity, *l_Copied))
            {
                AppendEntity(l_Text, it_Entity, Prefab::FindLocalID(instance, it_Entity.GetUUID()), it_Entity != *l_Copied ? Prefab::FindLocalID(instance, it_Entity.GetParent().GetUUID()) : UUID());
            }

            return l_Text + "\n";
        }

        Expected<void, std::string> LoadPrefabFromText(Scene& prefab, std::string_view text)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            std::uint32_t l_Version = 0;
            const Expected<YAML::Node, std::string> l_Root = ParseFormat(text, Prefab::c_FormatVersion, l_Version);
            if (!l_Root)
            {
                return Unexpected{ l_Root.GetError() };
            }

            const std::vector<std::string_view> l_Lines = SplitLines(text);
            Expected<std::vector<FileEntity>, std::string> l_Entities = ReadEntities(*l_Root, false, l_Lines.size());
            if (!l_Entities)
            {
                return Unexpected{ l_Entities.GetError() };
            }

            const auto a_IsRoot = [](const FileEntity& entity) { return !entity.Parent.IsValid(); };
            if (l_Entities->empty() || !a_IsRoot(l_Entities->front()) || std::any_of(l_Entities->begin() + 1, l_Entities->end(), a_IsRoot))
            {
                return Unexpected{ std::string("does not hold one entity and its subtree, with the entity first") };
            }

            Expected<std::vector<PlannedEntity>, std::string> l_Plan = PlanEntities(*l_Entities, l_Lines);
            if (!l_Plan)
            {
                return Unexpected{ l_Plan.GetError() };
            }

            prefab.Clear();

            std::unordered_map<std::string, std::size_t> l_Unknown;
            CreateEntities(prefab, *l_Plan, l_Unknown);
            for (const auto& [it_Name, it_Count] : l_Unknown)
            {
                TR_CORE_INFO("Scene: kept {} {} component(s) of a prefab as YAML, since no loaded code knows that component", it_Count, it_Name);
            }

            return {};
        }

        Expected<Entity, std::string> InstantiatePrefab(Scene& scene, UUID prefab, UUID instance, Entity parent, Entity before)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            Scene* l_Template = FindPrefabTemplate(prefab);
            if (l_Template == nullptr || !l_Template->GetFirstRoot())
            {
                return Unexpected{ std::format("prefab {} is not there, or cannot be read", prefab) };
            }

            const std::vector<std::pair<entt::entity, UUID>> l_Members = GetInstanceMembers(*l_Template, instance, {});
            for (const auto& [it_Member, it_UUID] : l_Members)
            {
                if (!it_UUID.IsValid() || scene.FindEntityByUUID(it_UUID))
                {
                    return Unexpected{ std::format("entity {} of instance {} is already in the scene", it_UUID, instance) };
                }
            }

            const Entity l_Root = ExpandInstance(scene, *l_Template, l_Members, prefab, instance);
            if (before)
            {
                scene.MoveBefore(l_Root, before, false);
            }
            else if (parent)
            {
                scene.SetParent(l_Root, parent, false);
            }

            return l_Root;
        }

        std::vector<UUID> GetPrefabReferences(std::string_view text)
        {
            YAML::Node l_Root;
            try
            {
                l_Root = YAML::Load(std::string(text));
            }
            catch (const YAML::Exception&)
            {
                return {};
            }

            std::vector<UUID> l_Prefabs;
            const YAML::Node l_Entities = l_Root.IsMap() ? l_Root["Entities"] : YAML::Node();
            if (!l_Entities || !l_Entities.IsSequence())
            {
                return l_Prefabs;
            }

            for (const YAML::Node it_Entry : l_Entities)
            {
                const std::optional<UUID> l_Prefab = it_Entry.IsMap() && it_Entry[std::string(c_InstanceKey)] ? ReadUUID(it_Entry[std::string(c_PrefabKey)]) : std::nullopt;
                if (l_Prefab && std::ranges::find(l_Prefabs, *l_Prefab) == l_Prefabs.end())
                {
                    l_Prefabs.push_back(*l_Prefab);
                }
            }

            return l_Prefabs;
        }

        std::vector<PrefabOverride> GetPrefabOverrides(Entity root)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            Scope<Scene> l_Scratch;

            return ReadOverrides(root, l_Scratch).value_or(std::vector<PrefabOverride>());
        }

        // Every override is checked first, so a refusal changes nothing
        Expected<void, std::string> RevertPrefabOverrides(Entity root, std::span<const PrefabOverride> overrides)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            Scope<Scene> l_Scratch;
            if (const Expected<void, std::string> l_Checked = CheckOverrides(root, overrides, l_Scratch); !l_Checked)
            {
                return l_Checked;
            }

            Scene& l_Scene = *root.GetScene();
            std::unordered_map<std::string, std::size_t> l_Unknown;
            for (const PrefabOverride& it_Override : overrides)
            {
                const Entity l_Entity = l_Scene.FindEntityByUUID(it_Override.Entity);
                switch (it_Override.Type)
                {
                    case PrefabOverride::Kind::Value:
                    case PrefabOverride::Kind::RemovedComponent:
                    {
                        CopyComponentValue(l_Entity, l_Scratch->FindEntityByUUID(it_Override.Entity), it_Override.Component, it_Override.Path, l_Unknown);

                        break;
                    }
                    case PrefabOverride::Kind::AddedComponent:
                    {
                        static_cast<void>(RemoveNamedComponent(l_Entity, it_Override.Component));

                        break;
                    }
                    case PrefabOverride::Kind::RemovedEntity:
                    {
                        if (const Expected<void, std::string> l_Restored = RestoreRemovedEntity(l_Scene, *l_Scratch, it_Override.Entity); !l_Restored)
                        {
                            return l_Restored;
                        }

                        break;
                    }
                    case PrefabOverride::Kind::AddedEntity:
                    {
                        l_Scene.DestroyEntity(l_Entity);

                        break;
                    }
                }
            }

            return {};
        }

        // Made on the instance's prefab as it would be unchanged, in a scene of its own, and written from there under the local IDs the instance tells back
        Expected<std::string, std::string> ApplyPrefabOverrides(Entity root, std::span<const PrefabOverride> overrides)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            Scope<Scene> l_Scratch;
            if (const Expected<void, std::string> l_Checked = CheckOverrides(root, overrides, l_Scratch); !l_Checked)
            {
                return Unexpected{ l_Checked.GetError() };
            }

            Scene& l_Scene = *root.GetScene();
            std::unordered_map<std::string, std::size_t> l_Unknown;
            for (const PrefabOverride& it_Override : overrides)
            {
                const Entity l_Entity = l_Scene.FindEntityByUUID(it_Override.Entity);
                const Entity l_Base = l_Scratch->FindEntityByUUID(it_Override.Entity);
                switch (it_Override.Type)
                {
                    case PrefabOverride::Kind::Value:
                    case PrefabOverride::Kind::AddedComponent:
                    {
                        CopyComponentValue(l_Base, l_Entity, it_Override.Component, it_Override.Path, l_Unknown);

                        break;
                    }
                    case PrefabOverride::Kind::RemovedComponent:
                    {
                        static_cast<void>(RemoveNamedComponent(l_Base, it_Override.Component));

                        break;
                    }
                    case PrefabOverride::Kind::RemovedEntity:
                    {
                        l_Scratch->DestroyEntity(l_Base);

                        break;
                    }
                    case PrefabOverride::Kind::AddedEntity:
                    {
                        for (Entity it_Entity = l_Entity; it_Entity; it_Entity = l_Scene.GetNextInSubtree(it_Entity, l_Entity))
                        {
                            if (it_Entity.Has<PrefabInstanceComponent>())
                            {
                                return Unexpected{ std::format("entity {} is an instance of a prefab, which a prefab cannot hold yet", it_Entity.GetUUID()) };
                            }
                        }

                        const Expected<Entity, std::string> l_Copied = LoadEntityFromText(*l_Scratch, SaveEntityToText(l_Scene, l_Entity), l_Scratch->FindEntityByUUID(l_Entity.GetParent().GetUUID()), {});
                        if (!l_Copied)
                        {
                            return Unexpected{ l_Copied.GetError() };
                        }

                        break;
                    }
                }
            }

            Entity l_Root = l_Scratch->FindEntityByUUID(root.GetUUID());
            l_Root.Remove<PrefabInstanceComponent>();

            return SavePrefabToText(*l_Scratch, l_Root, root.Get<PrefabInstanceComponent>().Instance);
        }

        // An instance is found from its root, so one whose prefab is missing, which keeps its text only, is left as it is
        std::vector<PrefabInstanceText> SavePrefabInstances(Scene& scene, UUID prefab)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            std::vector<PrefabInstanceText> l_Instances;
            if (FindPrefabTemplate(prefab) == nullptr)
            {
                return l_Instances;
            }

            const auto a_IsInstance = [prefab](Entity entity) { return entity.Has<PrefabInstanceComponent>() && entity.Get<PrefabInstanceComponent>().Prefab == prefab && !entity.Get<PrefabInstanceComponent>().IsMissing(); };
            for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
            {
                if (!a_IsInstance(it_Entity))
                {
                    continue;
                }

                bool l_Inside = false;
                for (Entity it_Parent = it_Entity.GetParent(); it_Parent && !l_Inside; it_Parent = it_Parent.GetParent())
                {
                    l_Inside = a_IsInstance(it_Parent);
                }

                if (!l_Inside)
                {
                    const Entity l_Parent = it_Entity.GetParent();
                    const Entity l_Next = it_Entity.GetNextSibling();
                    l_Instances.push_back({ .Root = it_Entity.GetUUID(), .Parent = l_Parent ? l_Parent.GetUUID() : UUID(), .Before = l_Next ? l_Next.GetUUID() : UUID(), .Text = SaveEntityToText(scene, it_Entity) });
                }
            }

            return l_Instances;
        }

        // In order, so a sibling after one is still there as it is made, and each as a whole, its old entities destroyed first, since it keeps their UUIDs
        Expected<void, std::string> RestorePrefabInstances(Scene& scene, std::span<const PrefabInstanceText> instances)
        {
            TR_PROFILE_FUNCTION();
            TR_CORE_ASSERT(s_State != nullptr, "The scene serializer is not initialized.");

            Expected<void, std::string> l_Result;
            for (const PrefabInstanceText& it_Instance : instances)
            {
                if (const Entity l_Old = scene.FindEntityByUUID(it_Instance.Root))
                {
                    scene.DestroyEntity(l_Old);
                }

                const Entity l_Parent = scene.FindEntityByUUID(it_Instance.Parent);
                const Entity l_Before = scene.FindEntityByUUID(it_Instance.Before);
                const Expected<Entity, std::string> l_Made = LoadEntityFromText(scene, it_Instance.Text, l_Parent, l_Before && l_Before.GetParent().GetHandle() == l_Parent.GetHandle() ? l_Before : Entity());
                if (!l_Made && l_Result)
                {
                    l_Result = Unexpected{ std::format("instance {} could not be made again: {}", it_Instance.Root, l_Made.GetError()) };
                }
            }

            return l_Result;
        }

        Expected<void, std::string> Save(Scene& scene, std::string_view path)
        {
            const Expected<void, FileError> l_Written = FileSystem::WriteText(path, SaveToText(scene));
            if (!l_Written)
            {
                return Unexpected{ std::format("{} could not be written: {}", path, ToString(l_Written.GetError())) };
            }

            return {};
        }

        Expected<void, std::string> Load(Scene& scene, std::string_view path)
        {
            const Expected<std::string, FileError> l_Text = FileSystem::ReadText(path);
            if (!l_Text)
            {
                return Unexpected{ std::format("{} could not be read: {}", path, ToString(l_Text.GetError())) };
            }

            Expected<void, std::string> l_Loaded = LoadFromText(scene, *l_Text);
            if (!l_Loaded)
            {
                return Unexpected{ std::format("{} {}", path, l_Loaded.GetError()) };
            }

            return {};
        }
    }
}