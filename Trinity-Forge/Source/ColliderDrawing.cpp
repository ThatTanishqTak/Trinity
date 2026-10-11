#include "ColliderDrawing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <utility>

namespace
{
    constexpr glm::vec4 c_SolidColor{ 0.45f, 0.95f, 0.45f, 1.0f };
    constexpr glm::vec4 c_TriggerColor{ 0.4f, 0.75f, 1.0f, 1.0f };
    constexpr glm::vec4 c_JointColor{ 1.0f, 0.65f, 0.2f, 1.0f };
    constexpr glm::vec4 c_ContactColor{ 1.0f, 0.3f, 0.35f, 1.0f };
    // In metres, whatever the scene's scale
    constexpr float c_JointMarkSize = 0.1f;
    constexpr float c_JointAxisLength = 0.5f;
    constexpr float c_HingeCircleRadius = 0.2f;
    constexpr float c_ContactMarkSize = 0.06f;
    constexpr float c_ContactNormalLength = 0.25f;
    // The most a rounded corner of a 2D collider turns in one segment
    constexpr float c_ArcStep = std::numbers::pi_v<float> / 12.0f;

    // The collider's own frame: the entity's world transform, then the collider's offset and turn
    glm::mat4 GetColliderMatrix(const glm::mat4& world, const Trinity::ColliderSettings& collider)
    {
        return world * glm::translate(glm::mat4(1.0f), collider.Offset) * glm::mat4_cast(collider.OffsetRotation);
    }

    glm::vec4 GetColor(const Trinity::ColliderSettings& collider)
    {
        return collider.Trigger ? c_TriggerColor : c_SolidColor;
    }

    glm::vec4 GetColor(const Trinity::Collider2DSettings& collider)
    {
        return collider.Trigger ? c_TriggerColor : c_SolidColor;
    }

    // A 2D collider's own frame in the XY plane, as the 2D world takes it: the entity's world transform, then the collider's offset and turn about Z, with each axis's scale, at the collider's depth in Z
    struct Frame2D
    {
        glm::vec2 Origin{ 0.0f };
        glm::vec2 AxisX{ 1.0f, 0.0f };
        glm::vec2 AxisY{ 0.0f, 1.0f };
        float ScaleX = 1.0f;
        float ScaleY = 1.0f;
        float Depth = 0.0f;

        [[nodiscard]] glm::vec2 ToWorld(const glm::vec2& point) const { return Origin + AxisX * point.x + AxisY * point.y; }
    };

    Frame2D GetFrame2D(const glm::mat4& world, const Trinity::Collider2DSettings& collider)
    {
        const glm::mat4 l_Placed = world * glm::translate(glm::mat4(1.0f), glm::vec3(collider.Offset, 0.0f)) * glm::mat4_cast(glm::angleAxis(collider.OffsetAngle, glm::vec3(0.0f, 0.0f, 1.0f)));

        return { glm::vec2(l_Placed[3]), glm::vec2(l_Placed[0]), glm::vec2(l_Placed[1]), glm::length(glm::vec3(l_Placed[0])), glm::length(glm::vec3(l_Placed[1])), l_Placed[3].z };
    }

    float Cross2D(const glm::vec2& origin, const glm::vec2& first, const glm::vec2& second)
    {
        return (first.x - origin.x) * (second.y - origin.y) - (first.y - origin.y) * (second.x - origin.x);
    }

    // Room for a polygon's most points, and the hull's two chains over them
    using Points2D = std::array<glm::vec2, Trinity::PolygonCollider2DComponent::c_MaxPoints>;
    using HullPoints = std::array<glm::vec2, Trinity::PolygonCollider2DComponent::c_MaxPoints * 2>;

    // The convex hull of the first points, counter-clockwise, without points in line with their neighbours, in place of nothing, so drawing every collider each frame allocates nothing
    std::span<const glm::vec2> GetHull(Points2D& points, std::size_t count, HullPoints& hull)
    {
        const std::span<glm::vec2> l_Points(points.data(), count);
        std::ranges::sort(l_Points, [](const glm::vec2& left, const glm::vec2& right) { return left.x < right.x || (left.x == right.x && left.y < right.y); });
        const std::size_t l_Unique = static_cast<std::size_t>(std::ranges::unique(l_Points).begin() - l_Points.begin());
        if (l_Unique < 3)
        {
            std::ranges::copy(l_Points.first(l_Unique), hull.begin());

            return std::span<const glm::vec2>(hull.data(), l_Unique);
        }

        std::size_t l_Count = 0;
        for (std::size_t it_Point = 0; it_Point < l_Unique; ++it_Point)
        {
            while (l_Count >= 2 && Cross2D(hull[l_Count - 2], hull[l_Count - 1], l_Points[it_Point]) <= 0.0f)
            {
                --l_Count;
            }

            hull[l_Count++] = l_Points[it_Point];
        }

        for (std::size_t it_Point = l_Unique - 1, l_Lower = l_Count + 1; it_Point-- > 0;)
        {
            while (l_Count >= l_Lower && Cross2D(hull[l_Count - 2], hull[l_Count - 1], l_Points[it_Point]) <= 0.0f)
            {
                --l_Count;
            }

            hull[l_Count++] = l_Points[it_Point];
        }

        return std::span<const glm::vec2>(hull.data(), l_Count - 1);
    }

    // A counter-clockwise outline grown outward by the radius: each edge moved out along its normal, and an arc round each corner. One point with a radius is a circle, and two a capsule
    void DrawOutline2D(std::span<const glm::vec2> outline, float radius, float depth, const glm::vec4& color)
    {
        const auto a_At = [depth](const glm::vec2& point) { return glm::vec3(point, depth); };
        if (outline.size() == 1)
        {
            Trinity::DebugDraw::Circle(a_At(outline[0]), glm::vec3(0.0f, 0.0f, 1.0f), radius, color);

            return;
        }

        const std::size_t l_Count = outline.size();
        const auto a_Normal = [&](std::size_t edge)
        {
            const glm::vec2 l_Edge = outline[(edge + 1) % l_Count] - outline[edge];
            const float l_Length = glm::length(l_Edge);

            return l_Length > 0.0f ? glm::vec2(l_Edge.y, -l_Edge.x) / l_Length : glm::vec2(0.0f);
        };

        for (std::size_t it_Edge = 0; it_Edge < l_Count; ++it_Edge)
        {
            const glm::vec2 l_Out = a_Normal(it_Edge) * radius;
            Trinity::DebugDraw::Line(a_At(outline[it_Edge] + l_Out), a_At(outline[(it_Edge + 1) % l_Count] + l_Out), color);
            if (radius <= 0.0f)
            {
                continue;
            }

            // Round the corner at the edge's end, from this edge's normal to the next one's
            const glm::vec2 l_Corner = outline[(it_Edge + 1) % l_Count];
            const glm::vec2 l_From = a_Normal(it_Edge);
            const glm::vec2 l_To = a_Normal((it_Edge + 1) % l_Count);
            const float l_Start = std::atan2(l_From.y, l_From.x);
            const float l_Sweep = std::fmod(std::atan2(l_To.y, l_To.x) - l_Start + 2.0f * std::numbers::pi_v<float>, 2.0f * std::numbers::pi_v<float>);
            const int l_Steps = std::max(static_cast<int>(std::ceil(l_Sweep / c_ArcStep)), 1);
            glm::vec2 l_Previous = l_Corner + l_From * radius;
            for (int it_Step = 1; it_Step <= l_Steps; ++it_Step)
            {
                const float l_Angle = l_Start + l_Sweep * static_cast<float>(it_Step) / static_cast<float>(l_Steps);
                const glm::vec2 l_Next = l_Corner + glm::vec2(std::cos(l_Angle), std::sin(l_Angle)) * radius;
                Trinity::DebugDraw::Line(a_At(l_Previous), a_At(l_Next), color);
                l_Previous = l_Next;
            }
        }
    }

    // A box or polygon by the hull of its points in the world, with a polygon's corners rounded by its radius at the larger scale. Nothing for points that make no area, which the 2D world leaves out
    void DrawPolygon2D(const Frame2D& frame, std::span<const glm::vec2> points, float radius, const glm::vec4& color)
    {
        Points2D l_World{};
        const std::size_t l_Count = std::min(points.size(), l_World.size());
        for (std::size_t it_Point = 0; it_Point < l_Count; ++it_Point)
        {
            l_World[it_Point] = frame.ToWorld(points[it_Point]);
        }

        HullPoints l_HullPoints{};
        const std::span<const glm::vec2> l_Hull = GetHull(l_World, l_Count, l_HullPoints);
        if (l_Hull.size() >= 3)
        {
            DrawOutline2D(l_Hull, std::max(radius, 0.0f) * std::max(frame.ScaleX, frame.ScaleY), frame.Depth, color);
        }
    }

    // As the 2D world takes them: a circle by the larger of its X and Y scales, and a capsule by its Y scale along its length and its X scale around it
    void DrawEntityColliders2D(Trinity::Entity entity)
    {
        const glm::mat4& l_World = entity.Get<Trinity::WorldTransformComponent>().Matrix;

        if (entity.Has<Trinity::BoxCollider2DComponent>())
        {
            const Trinity::BoxCollider2DComponent& l_Box = entity.Get<Trinity::BoxCollider2DComponent>();
            const glm::vec2 l_Half = glm::max(l_Box.HalfExtents, glm::vec2(1e-3f));
            const std::array<glm::vec2, 4> l_Corners{ { { -l_Half.x, -l_Half.y }, { l_Half.x, -l_Half.y }, { l_Half.x, l_Half.y }, { -l_Half.x, l_Half.y } } };
            DrawPolygon2D(GetFrame2D(l_World, l_Box), l_Corners, 0.0f, GetColor(l_Box));
        }

        if (entity.Has<Trinity::CircleCollider2DComponent>())
        {
            const Trinity::CircleCollider2DComponent& l_Circle = entity.Get<Trinity::CircleCollider2DComponent>();
            const Frame2D l_Frame = GetFrame2D(l_World, l_Circle);
            const glm::vec2 l_Centre = l_Frame.Origin;
            DrawOutline2D(std::span(&l_Centre, 1), l_Circle.Radius * std::max(l_Frame.ScaleX, l_Frame.ScaleY), l_Frame.Depth, GetColor(l_Circle));
        }

        if (entity.Has<Trinity::CapsuleCollider2DComponent>())
        {
            const Trinity::CapsuleCollider2DComponent& l_Capsule = entity.Get<Trinity::CapsuleCollider2DComponent>();
            const Frame2D l_Frame = GetFrame2D(l_World, l_Capsule);
            const float l_HalfSegment = l_Capsule.GetHalfSegment();
            const std::array<glm::vec2, 2> l_Ends{ { l_Frame.ToWorld({ 0.0f, -l_HalfSegment }), l_Frame.ToWorld({ 0.0f, l_HalfSegment }) } };
            const bool l_Round = l_HalfSegment * l_Frame.ScaleY < 1e-3f;
            DrawOutline2D(std::span(l_Ends).first(l_Round ? 1 : 2), l_Capsule.Radius * l_Frame.ScaleX, l_Frame.Depth, GetColor(l_Capsule));
        }

        if (entity.Has<Trinity::PolygonCollider2DComponent>())
        {
            const Trinity::PolygonCollider2DComponent& l_Polygon = entity.Get<Trinity::PolygonCollider2DComponent>();
            DrawPolygon2D(GetFrame2D(l_World, l_Polygon), l_Polygon.Points, l_Polygon.Radius, GetColor(l_Polygon));
        }
    }

    // A round shape along the frame's Y: its centre, its unit axis and the scale along it, and the larger scale across it
    struct RoundFrame
    {
        glm::vec3 Center{ 0.0f };
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f };
        glm::vec3 Side{ 1.0f, 0.0f, 0.0f };
        glm::vec3 Front{ 0.0f, 0.0f, 1.0f };
        float AxialScale = 1.0f;
        float RadialScale = 1.0f;
    };

    RoundFrame GetRoundFrame(const glm::mat4& frame)
    {
        const glm::vec3 l_X(frame[0]);
        const glm::vec3 l_Y(frame[1]);
        const glm::vec3 l_Z(frame[2]);

        RoundFrame l_Round;
        l_Round.Center = glm::vec3(frame[3]);
        l_Round.AxialScale = glm::length(l_Y);
        l_Round.RadialScale = std::max(glm::length(l_X), glm::length(l_Z));
        l_Round.Axis = l_Round.AxialScale > 1e-6f ? l_Y / l_Round.AxialScale : glm::vec3(0.0f, 1.0f, 0.0f);
        l_Round.Side = glm::length(l_X) > 1e-6f ? glm::normalize(l_X) : glm::vec3(1.0f, 0.0f, 0.0f);
        l_Round.Front = glm::normalize(glm::cross(l_Round.Side, l_Round.Axis));

        return l_Round;
    }

    // Each edge of the shape as it sits on its entity, or its bounds when it has too many edges to keep. Nothing until it has loaded
    template<typename T>
    void DrawShape(const glm::mat4& frame, Trinity::UUID shape, const glm::vec4& color)
    {
        const Trinity::Asset* l_Asset = shape ? Trinity::AssetManager::GetAsset(shape) : nullptr;
        if (l_Asset == nullptr || l_Asset->GetAssetType() != T::c_AssetType)
        {
            return;
        }

        const T& l_Shape = static_cast<const T&>(*l_Asset);
        const std::span<const glm::vec3> l_Wireframe = l_Shape.GetWireframe();
        if (l_Wireframe.empty())
        {
            const Trinity::MeshBounds& l_Bounds = l_Shape.GetBounds();
            Trinity::DebugDraw::Box(glm::scale(glm::translate(frame, (l_Bounds.Min + l_Bounds.Max) * 0.5f), (l_Bounds.Max - l_Bounds.Min) * 0.5f), color);

            return;
        }

        for (std::size_t it_Point = 0; it_Point + 1 < l_Wireframe.size(); it_Point += 2)
        {
            Trinity::DebugDraw::Line(glm::vec3(frame * glm::vec4(l_Wireframe[it_Point], 1.0f)), glm::vec3(frame * glm::vec4(l_Wireframe[it_Point + 1], 1.0f)), color);
        }
    }

    void DrawEntityColliders(Trinity::Entity entity, std::vector<Trinity::UUID>& shapes)
    {
        const glm::mat4& l_World = entity.Get<Trinity::WorldTransformComponent>().Matrix;

        if (entity.Has<Trinity::BoxColliderComponent>())
        {
            const Trinity::BoxColliderComponent& l_Box = entity.Get<Trinity::BoxColliderComponent>();
            Trinity::DebugDraw::Box(glm::scale(GetColliderMatrix(l_World, l_Box), l_Box.HalfExtents), GetColor(l_Box));
        }

        if (entity.Has<Trinity::SphereColliderComponent>())
        {
            const Trinity::SphereColliderComponent& l_Sphere = entity.Get<Trinity::SphereColliderComponent>();
            const glm::mat4 l_Frame = GetColliderMatrix(l_World, l_Sphere);
            const float l_Scale = std::max({ glm::length(glm::vec3(l_Frame[0])), glm::length(glm::vec3(l_Frame[1])), glm::length(glm::vec3(l_Frame[2])) });
            Trinity::DebugDraw::Sphere(glm::vec3(l_Frame[3]), l_Sphere.Radius * l_Scale, GetColor(l_Sphere));
        }

        if (entity.Has<Trinity::CapsuleColliderComponent>())
        {
            const Trinity::CapsuleColliderComponent& l_Capsule = entity.Get<Trinity::CapsuleColliderComponent>();
            const RoundFrame l_Round = GetRoundFrame(GetColliderMatrix(l_World, l_Capsule));
            const glm::vec3 l_HalfSegment = l_Round.Axis * (l_Capsule.GetHalfSegment() * l_Round.AxialScale);
            Trinity::DebugDraw::Capsule(l_Round.Center - l_HalfSegment, l_Round.Center + l_HalfSegment, l_Capsule.Radius * l_Round.RadialScale, GetColor(l_Capsule));
        }

        if (entity.Has<Trinity::CylinderColliderComponent>())
        {
            const Trinity::CylinderColliderComponent& l_Cylinder = entity.Get<Trinity::CylinderColliderComponent>();
            const RoundFrame l_Round = GetRoundFrame(GetColliderMatrix(l_World, l_Cylinder));
            const glm::vec3 l_HalfHeight = l_Round.Axis * (l_Cylinder.Height * 0.5f * l_Round.AxialScale);
            const float l_Radius = l_Cylinder.Radius * l_Round.RadialScale;
            const glm::vec4 l_Color = GetColor(l_Cylinder);
            Trinity::DebugDraw::Circle(l_Round.Center - l_HalfHeight, l_Round.Axis, l_Radius, l_Color);
            Trinity::DebugDraw::Circle(l_Round.Center + l_HalfHeight, l_Round.Axis, l_Radius, l_Color);
            for (const glm::vec3& it_Side : { l_Round.Side, -l_Round.Side, l_Round.Front, -l_Round.Front })
            {
                Trinity::DebugDraw::Line(l_Round.Center - l_HalfHeight + it_Side * l_Radius, l_Round.Center + l_HalfHeight + it_Side * l_Radius, l_Color);
            }
        }

        if (entity.Has<Trinity::ConvexHullColliderComponent>())
        {
            const Trinity::ConvexHullColliderComponent& l_Hull = entity.Get<Trinity::ConvexHullColliderComponent>();
            shapes.push_back(l_Hull.Shape);
            DrawShape<Trinity::ConvexHullAsset>(GetColliderMatrix(l_World, l_Hull), l_Hull.Shape, GetColor(l_Hull));
        }

        if (entity.Has<Trinity::MeshColliderComponent>())
        {
            const Trinity::MeshColliderComponent& l_Mesh = entity.Get<Trinity::MeshColliderComponent>();
            shapes.push_back(l_Mesh.Shape);
            DrawShape<Trinity::CollisionMeshAsset>(GetColliderMatrix(l_World, l_Mesh), l_Mesh.Shape, GetColor(l_Mesh));
        }
    }

    // The anchor in the entity's frame, the axis turned with it, and a line to the other end: the connected entity's anchor for a distance joint, or its origin, or for a distance joint to the world its anchor there. A joint naming an entity no longer in the scene draws no line
    void DrawEntityJoint(Trinity::Scene& scene, Trinity::Entity entity)
    {
        if (!entity.Has<Trinity::JointComponent>())
        {
            return;
        }

        const Trinity::JointComponent& l_Joint = entity.Get<Trinity::JointComponent>();
        const glm::mat4& l_World = entity.Get<Trinity::WorldTransformComponent>().Matrix;
        const glm::vec3 l_Anchor(l_World * glm::vec4(l_Joint.Anchor, 1.0f));
        Trinity::DebugDraw::Cross(l_Anchor, c_JointMarkSize, c_JointColor, Trinity::DebugDepth::OnTop);

        if (l_Joint.Type == Trinity::JointType::Hinge || l_Joint.Type == Trinity::JointType::Slider)
        {
            const glm::vec3 l_Local = glm::length(l_Joint.Axis) > 1e-6f ? glm::normalize(l_Joint.Axis) : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 l_Turned = glm::mat3(l_World) * l_Local;
            const glm::vec3 l_Axis = glm::length(l_Turned) > 1e-6f ? glm::normalize(l_Turned) : l_Local;
            Trinity::DebugDraw::Arrow(l_Anchor, l_Anchor + l_Axis * c_JointAxisLength, c_JointColor, Trinity::DebugDepth::OnTop);
            if (l_Joint.Type == Trinity::JointType::Hinge)
            {
                Trinity::DebugDraw::Circle(l_Anchor, l_Axis, c_HingeCircleRadius, c_JointColor, Trinity::DebugDepth::OnTop);
            }
        }

        const Trinity::Entity l_Connected = l_Joint.ConnectedBody ? scene.FindEntityByUUID(l_Joint.ConnectedBody) : Trinity::Entity();
        const bool l_Distance = l_Joint.Type == Trinity::JointType::Distance;
        if (l_Connected)
        {
            const glm::mat4& l_Other = l_Connected.Get<Trinity::WorldTransformComponent>().Matrix;
            Trinity::DebugDraw::Line(l_Anchor, glm::vec3(l_Other * glm::vec4(l_Distance ? l_Joint.ConnectedAnchor : glm::vec3(0.0f), 1.0f)), c_JointColor, Trinity::DebugDepth::OnTop);
        }
        else if (l_Distance && !l_Joint.ConnectedBody)
        {
            Trinity::DebugDraw::Line(l_Anchor, l_Joint.ConnectedAnchor, c_JointColor, Trinity::DebugDepth::OnTop);
            Trinity::DebugDraw::Cross(l_Joint.ConnectedAnchor, c_JointMarkSize, c_JointColor, Trinity::DebugDepth::OnTop);
        }
    }

    // As a 3D joint's, flat at its entity's depth: a cross at the anchor, a circle about it for a joint that turns, the axis for one that slides, and a line to the other end. A joint naming an entity no longer in the scene draws no line
    void DrawEntityJoint2D(Trinity::Scene& scene, Trinity::Entity entity)
    {
        if (!entity.Has<Trinity::Joint2DComponent>())
        {
            return;
        }

        const Trinity::Joint2DComponent& l_Joint = entity.Get<Trinity::Joint2DComponent>();
        const glm::mat4& l_World = entity.Get<Trinity::WorldTransformComponent>().Matrix;
        const glm::vec3 l_Anchor(glm::vec2(l_World * glm::vec4(l_Joint.Anchor, 0.0f, 1.0f)), l_World[3].z);
        Trinity::DebugDraw::Cross(l_Anchor, c_JointMarkSize, c_JointColor, Trinity::DebugDepth::OnTop);

        if (l_Joint.Type == Trinity::Joint2DType::Revolute || l_Joint.Type == Trinity::Joint2DType::Wheel)
        {
            Trinity::DebugDraw::Circle(l_Anchor, glm::vec3(0.0f, 0.0f, 1.0f), c_HingeCircleRadius, c_JointColor, Trinity::DebugDepth::OnTop);
        }

        if (l_Joint.Type == Trinity::Joint2DType::Prismatic || l_Joint.Type == Trinity::Joint2DType::Wheel)
        {
            const glm::vec2 l_Turned = glm::mat2(l_World) * (glm::length(l_Joint.Axis) > 1e-6f ? glm::normalize(l_Joint.Axis) : glm::vec2(0.0f, 1.0f));
            const glm::vec2 l_Axis = glm::length(l_Turned) > 1e-6f ? glm::normalize(l_Turned) : glm::vec2(0.0f, 1.0f);
            Trinity::DebugDraw::Arrow(l_Anchor, l_Anchor + glm::vec3(l_Axis * c_JointAxisLength, 0.0f), c_JointColor, Trinity::DebugDepth::OnTop);
        }

        const Trinity::Entity l_Connected = l_Joint.ConnectedBody ? scene.FindEntityByUUID(l_Joint.ConnectedBody) : Trinity::Entity();
        const bool l_Distance = l_Joint.Type == Trinity::Joint2DType::Distance;
        if (l_Connected)
        {
            const glm::mat4& l_Other = l_Connected.Get<Trinity::WorldTransformComponent>().Matrix;
            const glm::vec2 l_End(l_Other * glm::vec4(l_Distance ? l_Joint.ConnectedAnchor : glm::vec2(0.0f), 0.0f, 1.0f));
            Trinity::DebugDraw::Line(l_Anchor, glm::vec3(l_End, l_Other[3].z), c_JointColor, Trinity::DebugDepth::OnTop);
        }
        else if (l_Distance && !l_Joint.ConnectedBody)
        {
            const glm::vec3 l_End(l_Joint.ConnectedAnchor, l_Anchor.z);
            Trinity::DebugDraw::Line(l_Anchor, l_End, c_JointColor, Trinity::DebugDepth::OnTop);
            Trinity::DebugDraw::Cross(l_End, c_JointMarkSize, c_JointColor, Trinity::DebugDepth::OnTop);
        }
    }
}

ColliderDrawing::~ColliderDrawing()
{
    Release();
}

// The shapes drawn this time are acquired before those drawn last time are released, so one still in use is never unloaded between the two
void ColliderDrawing::Draw(Trinity::Scene& scene, Trinity::Entity root)
{
    m_Drawn.clear();
    const auto a_Next = [&scene, root](Trinity::Entity entity) { return root ? scene.GetNextInSubtree(entity, root) : scene.GetNextInHierarchyOrder(entity); };
    for (Trinity::Entity it_Entity = root ? root : scene.GetFirstRoot(); it_Entity; it_Entity = a_Next(it_Entity))
    {
        DrawEntityColliders(it_Entity, m_Drawn);
        DrawEntityColliders2D(it_Entity);
        DrawEntityJoint(scene, it_Entity);
        DrawEntityJoint2D(scene, it_Entity);
    }

    std::erase(m_Drawn, Trinity::UUID());
    std::ranges::sort(m_Drawn);
    const auto [a_First, a_Last] = std::ranges::unique(m_Drawn);
    m_Drawn.erase(a_First, a_Last);
    if (m_Drawn == m_Held)
    {
        return;
    }

    for (const Trinity::UUID it_Shape : m_Drawn)
    {
        Trinity::AssetManager::Acquire(it_Shape);
    }

    for (const Trinity::UUID it_Shape : m_Held)
    {
        Trinity::AssetManager::Release(it_Shape);
    }

    std::swap(m_Held, m_Drawn);
}

// A cross at each point and a short arrow along its normal, over everything
void ColliderDrawing::DrawContacts(std::span<const Trinity::PhysicsContactPoint> points)
{
    for (const Trinity::PhysicsContactPoint& it_Point : points)
    {
        Trinity::DebugDraw::Cross(it_Point.Position, c_ContactMarkSize, c_ContactColor, Trinity::DebugDepth::OnTop);
        Trinity::DebugDraw::Arrow(it_Point.Position, it_Point.Position + it_Point.Normal * c_ContactNormalLength, c_ContactColor, Trinity::DebugDepth::OnTop);
    }
}

void ColliderDrawing::Release()
{
    for (const Trinity::UUID it_Shape : m_Held)
    {
        Trinity::AssetManager::Release(it_Shape);
    }

    m_Held.clear();
}