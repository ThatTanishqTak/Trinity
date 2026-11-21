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

    // Drawing a hull or mesh collider starts its shape's load. No root draws the whole scene
    void Draw(Trinity::Scene& scene, Trinity::Entity root, Trinity::PhysicsScene3D* physics, Trinity::PhysicsScene2D* physics2D);
    // Where a running scene's bodies touch, with each point's normal
    static void DrawContacts(std::span<const Trinity::PhysicsContactPoint> points);
    // Every shape held is let go, as when nothing is drawn or the project closes
    void Release();

private:
    std::vector<Trinity::UUID> m_Held;
    std::vector<Trinity::UUID> m_Drawn;
};