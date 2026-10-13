#include "Trinity/Scene/SceneSerializer.hpp"

#include "Trinity/Scene/LiveScenes.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Trinity
{
    namespace
    {
        constexpr std::string_view c_EntityKey = "Entity";
        constexpr std::string_view c_ParentKey = "Parent";

        using MigrationFunction = void (*)(YAML::Node& root);

        // Entry N - 1 turns a format N file into format N + 1
        constexpr std::array<MigrationFunction, SceneSerializer::c_FormatVersion - 1> c_Migrations{};

        struct State
        {
            std::vector<ComponentSerializer, TaggedAllocator<ComponentSerializer, MemoryTag::Engine>> Serializers;
            std::vector<Scene*, TaggedAllocator<Scene*, MemoryTag::Engine>> Scenes;
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

        constexpr std::array<std::pair<LightType, std::string_view>, 3> c_LightTypeNames{ { { LightType::Directional, "Directional" }, { LightType::Point, "Point" }, { LightType::Spot, "Spot" } } };

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

        constexpr std::array<std::pair<BodyMotion, std::string_view>, 3> c_BodyMotionNames{ { { BodyMotion::Static, "Static" }, { BodyMotion::Kinematic, "Kinematic" }, { BodyMotion::Dynamic, "Dynamic" } } };

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
            writer.Write("Layer", collider.Layer);
        }

        // A layer past the last there can be is taken as the first
        void LoadCollider2DSettings(Collider2DSettings& collider, const ComponentReader& reader)
        {
            reader.Read("Offset", collider.Offset);
            reader.Read("OffsetAngle", collider.OffsetAngle);
            reader.Read("Friction", collider.Friction);
            reader.Read("Restitution", collider.Restitution);
            reader.Read("Trigger", collider.Trigger);
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

        constexpr std::array<std::pair<JointType, std::string_view>, 5> c_JointTypeNames{ { { JointType::Fixed, "Fixed" }, { JointType::Hinge, "Hinge" }, { JointType::Slider, "Slider" }, { JointType::Distance, "Distance" }, { JointType::Point, "Point" } } };

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

        constexpr std::array<std::pair<Joint2DType, std::string_view>, 5> c_Joint2DTypeNames{ { { Joint2DType::Revolute, "Revolute" }, { Joint2DType::Prismatic, "Prismatic" }, { Joint2DType::Distance, "Distance" }, { Joint2DType::Weld, "Weld" }, { Joint2DType::Wheel, "Wheel" } } };

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

        struct FileEntity
        {
            UUID ID;
            UUID Parent;
            YAML::Node Node;
        };

        Expected<std::vector<FileEntity>, std::string> ReadEntities(const YAML::Node& root)
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
            std::unordered_map<UUID, UUID> l_Parents;
            for (std::size_t it_Index = 0; it_Index < l_Entities.size(); ++it_Index)
            {
                const YAML::Node l_Entry = l_Entities[it_Index];
                const YAML::Node l_ID = l_Entry.IsMap() ? l_Entry[std::string(c_EntityKey)] : YAML::Node();
                const std::optional<UUID> l_UUID = l_ID && l_ID.IsScalar() ? UUID::Parse(l_ID.Scalar()) : std::nullopt;
                if (!l_UUID || !l_UUID->IsValid())
                {
                    return Unexpected{ std::format("entity {} has no valid Entity UUID", it_Index) };
                }

                UUID l_Parent;
                if (const YAML::Node l_ParentNode = l_Entry[std::string(c_ParentKey)])
                {
                    const std::optional<UUID> l_ParentUUID = l_ParentNode.IsScalar() ? UUID::Parse(l_ParentNode.Scalar()) : std::nullopt;
                    if (!l_ParentUUID || !l_ParentUUID->IsValid())
                    {
                        return Unexpected{ std::format("entity {} has a malformed Parent", *l_UUID) };
                    }

                    l_Parent = *l_ParentUUID;
                }

                if (!l_Parents.emplace(*l_UUID, l_Parent).second)
                {
                    return Unexpected{ std::format("entity {} appears more than once", *l_UUID) };
                }

                l_Result.push_back({ *l_UUID, l_Parent, l_Entry });
            }

            // Every parent exists, and following parents from any entity reaches a root within as many steps as there are entities
            for (const FileEntity& it_Entity : l_Result)
            {
                UUID l_Current = it_Entity.Parent;
                for (std::size_t it_Step = 0; l_Current.IsValid(); ++it_Step)
                {
                    const auto a_Found = l_Parents.find(l_Current);
                    if (a_Found == l_Parents.end())
                    {
                        return Unexpected{ std::format("entity {} names parent {}, which is not in the file", it_Entity.ID, l_Current) };
                    }

                    if (it_Step > l_Result.size())
                    {
                        return Unexpected{ std::format("entity {} is its own ancestor", it_Entity.ID) };
                    }

                    l_Current = a_Found->second;
                }
            }

            return l_Result;
        }

        // Format and migrations first, so entity text kept by the editor reads exactly like a scene file
        Expected<YAML::Node, std::string> ParseDocument(std::string_view text)
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
            std::uint32_t l_Version = 0;
            if (!l_Format || !l_Format.IsScalar() || !ParseNumber(l_Format.Scalar(), l_Version) || l_Version == 0)
            {
                return Unexpected{ std::string("no valid Format version") };
            }

            if (l_Version > SceneSerializer::c_FormatVersion)
            {
                return Unexpected{ std::format("written in format {} by a newer Trinity, and this build reads format {} and older", l_Version, SceneSerializer::c_FormatVersion) };
            }

            for (std::uint32_t it_Version = l_Version; it_Version < SceneSerializer::c_FormatVersion; ++it_Version)
            {
                c_Migrations[it_Version - 1](l_Root);
            }

            return l_Root;
        }

        void AppendEntity(std::string& text, Entity entity, bool writeParent)
        {
            constexpr std::uint32_t c_FieldIndent = 6;

            SceneRegistry& l_Registry = entity.GetScene()->GetRegistry();

            text += std::format("\n  - {}: {}", c_EntityKey, ToHex(entity.GetUUID()));
            if (const Entity l_Parent = entity.GetParent(); writeParent && l_Parent)
            {
                text += std::format("\n    {}: {}", c_ParentKey, ToHex(l_Parent.GetUUID()));
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

        // Every entity first, with its own UUID, then parents in file order, which is hierarchy order, so siblings keep theirs. Components no loaded code knows are kept as YAML and counted by name
        void CreateEntities(Scene& scene, const std::vector<FileEntity>& entities, std::unordered_map<std::string, std::size_t>& unknown)
        {
            SceneRegistry& l_Registry = scene.GetRegistry();

            for (const FileEntity& it_Entity : entities)
            {
                static_cast<void>(scene.CreateEntityWithUUID(it_Entity.ID));
            }

            for (const FileEntity& it_Entity : entities)
            {
                if (it_Entity.Parent.IsValid())
                {
                    scene.SetParent(scene.FindEntityByUUID(it_Entity.ID), scene.FindEntityByUUID(it_Entity.Parent), false);
                }
            }

            for (const FileEntity& it_Entity : entities)
            {
                Entity l_Entity = scene.FindEntityByUUID(it_Entity.ID);
                for (const auto& it_Pair : it_Entity.Node)
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
        }

        // Only a component with a registered serializer, which code knows how to save and load
        SceneRegistry::common_type* FindStorage(Entity entity, std::string_view name)
        {
            return FindSerializer(name) != nullptr ? entity.GetScene()->GetRegistry().storage(GetPoolId(name)) : nullptr;
        }

        // The YAML a scene file read without the component's code keeps for it, so a component kept this way saves as one kept from a file does
        std::string ToKeptYaml(const ComponentSerializer & serializer, const void* component)
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
        std::size_t KeepAsYaml(Scene & scene, const ComponentSerializer & serializer)
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
        std::size_t LoadKept(Scene & scene, const ComponentSerializer & serializer)
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
                if (m_ItemPending)
                {
                    *m_Output += '\n';
                    m_Output->append(m_Indent - 2, ' ');
                    *m_Output += "- {}";
                    m_ItemPending = false;
                }

                *m_Output += l_Block.Empty ? " []" : "";
                break;
            case BlockKind::Map:
                *m_Output += l_Block.Empty ? " {}" : "";
                break;
            case BlockKind::InlineMap:
                *m_Output += '}';
                break;
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

    void ComponentWriter::Write(std::string_view key, const ScriptFieldValue & value)
    {
        BeginInlineMap(key);
        Write("Type", ToString(value.GetType()));
        std::visit([this](const auto& data)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(data)>, ScriptString>)
            {
                Write("Value", std::string_view(data));
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

        ScriptFieldValue l_Read(*l_Type);
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
            RegisterComponent(MakeComponentSerializer<JointComponent, SaveJoint, LoadJoint>());
            RegisterComponent(MakeComponentSerializer<RigidBody2DComponent, SaveRigidBody2D, LoadRigidBody2D>());
            RegisterComponent(MakeComponentSerializer<BoxCollider2DComponent, SaveBoxCollider2D, LoadBoxCollider2D>());
            RegisterComponent(MakeComponentSerializer<CircleCollider2DComponent, SaveCircleCollider2D, LoadCircleCollider2D>());
            RegisterComponent(MakeComponentSerializer<CapsuleCollider2DComponent, SaveCapsuleCollider2D, LoadCapsuleCollider2D>());
            RegisterComponent(MakeComponentSerializer<PolygonCollider2DComponent, SavePolygonCollider2D, LoadPolygonCollider2D>());
            RegisterComponent(MakeComponentSerializer<Joint2DComponent, SaveJoint2D, LoadJoint2D>());
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

            for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
            {
                AppendEntity(l_Text, it_Entity, true);
            }

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

            Expected<std::vector<FileEntity>, std::string> l_Entities = ReadEntities(*l_Root);
            if (!l_Entities)
            {
                return Unexpected{ l_Entities.GetError() };
            }

            // Nothing can fail from here, so the scene is only replaced once the file is known to be whole
            scene.Clear();

            std::unordered_map<std::string, std::size_t> l_Unknown;
            CreateEntities(scene, *l_Entities, l_Unknown);
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
            for (Entity it_Entity = root; it_Entity; it_Entity = scene.GetNextInSubtree(it_Entity, root))
            {
                AppendEntity(l_Text, it_Entity, it_Entity != root);
            }

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

            Expected<std::vector<FileEntity>, std::string> l_Entities = ReadEntities(*l_Root);
            if (!l_Entities)
            {
                return Unexpected{ l_Entities.GetError() };
            }

            const auto a_IsRoot = [](const FileEntity& entity) { return !entity.Parent.IsValid(); };
            if (l_Entities->empty() || !a_IsRoot(l_Entities->front()) || std::any_of(l_Entities->begin() + 1, l_Entities->end(), a_IsRoot))
            {
                return Unexpected{ std::string("does not hold one entity and its subtree, with the entity first") };
            }

            for (const FileEntity& it_Entity : *l_Entities)
            {
                if (scene.FindEntityByUUID(it_Entity.ID))
                {
                    return Unexpected{ std::format("entity {} is already in the scene", it_Entity.ID) };
                }
            }

            std::unordered_map<std::string, std::size_t> l_Unknown;
            CreateEntities(scene, *l_Entities, l_Unknown);

            const Entity l_Entity = scene.FindEntityByUUID(l_Entities->front().ID);
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