#pragma once

#include <Trinity.hpp>

#include <vector>

// The colliders and joints of an entity and everything under it, drawn through DebugDraw in world space, with their entity's world scale as the physics world will take it: a box, convex hull or mesh exactly, a sphere by its largest axis, and a capsule or cylinder by its Y axis along its length and its larger other axis around it. 2D colliders are drawn flat at their entity's depth, as the 2D world takes them: a box or polygon by the hull of its points, with every scale, a circle by its larger X or Y scale, and a capsule by its Y scale along its length and its X scale around it. Solid colliders are green and triggers blue. A joint is orange: a cross at its anchor, its axis for a hinge or slider, with a circle around a hinge's, and a line to what it joins, and a 2D joint the same, flat, with a circle for a revolute or wheel joint and the axis for a prismatic or wheel joint. After the transform pass, before the frame graph is built
class ColliderDrawing
{
public:
    ColliderDrawing() = default;
    ~ColliderDrawing();

    ColliderDrawing(const ColliderDrawing&) = delete;
    ColliderDrawing& operator=(const ColliderDrawing&) = delete;

    // A hull or mesh collider draws once its shape has loaded, which drawing it starts. A shape is held while a collider using it is drawn, and let go the first time none is
    void Draw(Trinity::Scene& scene, Trinity::Entity root);
    // Every shape held is let go, as when nothing is drawn or the project closes
    void Release();

private:
    std::vector<Trinity::UUID> m_Held;
    std::vector<Trinity::UUID> m_Drawn;
};