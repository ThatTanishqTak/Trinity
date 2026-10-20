#pragma once

#include <Trinity.hpp>

#include <span>
#include <vector>

class ColliderDrawing
{
public:
    ColliderDrawing() = default;
    ~ColliderDrawing();

    ColliderDrawing(const ColliderDrawing&) = delete;
    ColliderDrawing& operator=(const ColliderDrawing&) = delete;

    // A hull or mesh collider draws once its shape has loaded, which drawing it starts. A shape is held while a collider using it is drawn, and let go the first time none is. No root draws the whole scene. The physics of a running scene, 3D and 2D, when given, shows how its characters stand and move
    void Draw(Trinity::Scene& scene, Trinity::Entity root, Trinity::PhysicsScene3D* physics, Trinity::PhysicsScene2D* physics2D);
    // Where a running scene's bodies touch, with each point's normal
    static void DrawContacts(std::span<const Trinity::PhysicsContactPoint> points);
    // Every shape held is let go, as when nothing is drawn or the project closes
    void Release();

private:
    std::vector<Trinity::UUID> m_Held;
    std::vector<Trinity::UUID> m_Drawn;
};