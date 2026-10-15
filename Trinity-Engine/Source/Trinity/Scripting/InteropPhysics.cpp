#include "Trinity/Scripting/Interop.hpp"

#include "Trinity/Physics/PhysicsScene2D.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scripting/ScriptApi.hpp"
#include "Trinity/Scripting/ScriptRuntime.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Trinity::Interop
{
    namespace
    {
        [[nodiscard]] Int32 ToResult(PhysicsBodyResult result)
        {
            switch (result)
            {
                case PhysicsBodyResult::Done: return std::to_underlying(AccessResult::Done);
                case PhysicsBodyResult::NoBody: return std::to_underlying(AccessResult::NoComponent);
                case PhysicsBodyResult::WrongMotion: return std::to_underlying(AccessResult::BadValue);
            }

            return std::to_underlying(AccessResult::BadValue);
        }

        [[nodiscard]] PhysicsScene3D* GetPhysics()
        {
            ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr ? l_Runtime->GetPhysics() : nullptr;
        }

        [[nodiscard]] PhysicsScene2D* GetPhysics2D()
        {
            ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr ? l_Runtime->GetPhysics2D() : nullptr;
        }

        // The call on the entity's body, NoEntity for an entity the running scene does not have, and NoComponent for a scene with no physics of that kind, which has no bodies either
        template<typename Physics, typename F>
        [[nodiscard]] Int32 OnBody(Physics* physics, EntityId entity, F&& call)
        {
            if (!FindEntity(entity))
            {
                return std::to_underlying(AccessResult::NoEntity);
            }

            if (physics == nullptr)
            {
                return std::to_underlying(AccessResult::NoComponent);
            }

            return ToResult(std::forward<F>(call)(*physics, UUID(entity)));
        }

        // A joint's state, NoComponent for an entity whose joint the running scene's physics has not taken in
        template<typename Physics>
        [[nodiscard]] Int32 JointBroken(Physics* physics, EntityId entity, Bool* broken)
        {
            if (!FindEntity(entity))
            {
                return std::to_underlying(AccessResult::NoEntity);
            }

            const std::optional<bool> l_Broken = physics != nullptr ? physics->IsJointBroken(UUID(entity)) : std::nullopt;
            if (!l_Broken)
            {
                return std::to_underlying(AccessResult::NoComponent);
            }

            *broken = *l_Broken;

            return std::to_underlying(AccessResult::Done);
        }

        template<typename Filter>
        [[nodiscard]] Filter ToFilter(const QueryFilter* filter)
        {
            Filter l_Filter;
            if (filter != nullptr)
            {
                l_Filter.Layers = filter->Layers;
                l_Filter.HitTriggers = filter->HitTriggers;
                l_Filter.Ignore = UUID(filter->Ignore);
            }

            return l_Filter;
        }

        [[nodiscard]] PhysicsHit ToHit(const PhysicsHit3D& hit)
        {
            return { hit.Entity.GetValue(), hit.Body.GetValue(), hit.Point, hit.Normal, hit.Distance, hit.Trigger };
        }

        [[nodiscard]] Interop::PhysicsHit2D ToHit(const Trinity::PhysicsHit2D& hit)
        {
            return { hit.Entity.GetValue(), hit.Body.GetValue(), hit.Point, hit.Normal, hit.Distance, hit.Trigger };
        }

        // As many of the engine's hits as there is room for, nearest first as the engine has them
        template<typename Hit, typename Out>
        [[nodiscard]] Int32 WriteHits(std::span<const Hit> hits, Out* out, Int32 capacity)
        {
            const std::size_t l_Count = std::min(hits.size(), static_cast<std::size_t>(std::max(capacity, 0)));
            for (std::size_t it_Hit = 0; it_Hit < l_Count; ++it_Hit)
            {
                out[it_Hit] = ToHit(hits[it_Hit]);
            }

            return static_cast<Int32>(l_Count);
        }

        template<typename Hit, typename Out>
        [[nodiscard]] Int32 WriteHit(const std::optional<Hit>& hit, Out* out, Int32 capacity)
        {
            if (!hit || capacity < 1)
            {
                return 0;
            }

            out[0] = ToHit(*hit);

            return 1;
        }

        // A shape C# names as the engine sizes it, or none for a type the engine does not know
        [[nodiscard]] std::optional<PhysicsQueryShape3D> ToShape(const QueryShape& shape)
        {
            if (shape.Type < 0 || shape.Type > std::to_underlying(PhysicsQueryShapeType3D::Capsule))
            {
                return std::nullopt;
            }

            return PhysicsQueryShape3D{ static_cast<PhysicsQueryShapeType3D>(shape.Type), shape.Radius, shape.HalfExtents, shape.Height };
        }

        [[nodiscard]] std::optional<PhysicsQueryShape2D> ToShape(const QueryShape2D& shape)
        {
            if (shape.Type < 0 || shape.Type > std::to_underlying(PhysicsQueryShapeType2D::Capsule))
            {
                return std::nullopt;
            }

            return PhysicsQueryShape2D{ static_cast<PhysicsQueryShapeType2D>(shape.Type), shape.Radius, shape.HalfExtents, shape.Height };
        }
    }

    Int32 Body_GetVelocity(EntityId entity, Vector3* linear, Vector3* angular)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return physics.GetVelocity(id, *linear, *angular); });
    }

    Int32 Body_SetLinearVelocity(EntityId entity, const Vector3* velocity)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return physics.SetLinearVelocity(id, *velocity); });
    }

    Int32 Body_SetAngularVelocity(EntityId entity, const Vector3* velocity)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return physics.SetAngularVelocity(id, *velocity); });
    }

    Int32 Body_AddForce(EntityId entity, const Vector3* force, const Vector3* point)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return point != nullptr ? physics.AddForceAtPoint(id, *force, *point) : physics.AddForce(id, *force); });
    }

    Int32 Body_AddTorque(EntityId entity, const Vector3* torque)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return physics.AddTorque(id, *torque); });
    }

    Int32 Body_AddImpulse(EntityId entity, const Vector3* impulse, const Vector3* point)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return point != nullptr ? physics.AddImpulseAtPoint(id, *impulse, *point) : physics.AddImpulse(id, *impulse); });
    }

    Int32 Body_AddAngularImpulse(EntityId entity, const Vector3* impulse)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return physics.AddAngularImpulse(id, *impulse); });
    }

    Int32 Body_Teleport(EntityId entity, const Vector3* position, const Quaternion* rotation)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return physics.Teleport(id, *position, glm::normalize(*rotation)); });
    }

    Int32 Body_MoveKinematic(EntityId entity, const Vector3* position, const Quaternion* rotation)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return physics.MoveKinematic(id, *position, glm::normalize(*rotation)); });
    }

    Int32 Body_SetAwake(EntityId entity, Bool awake)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return physics.SetAwake(id, awake); });
    }

    Int32 Body_IsAwake(EntityId entity, Bool* awake)
    {
        return OnBody(GetPhysics(), entity, [&](PhysicsScene3D& physics, UUID id) { return physics.IsAwake(id, *awake); });
    }

    Int32 Joint_IsBroken(EntityId entity, Bool* broken)
    {
        return JointBroken(GetPhysics(), entity, broken);
    }

    Int32 Body2D_GetVelocity(EntityId entity, Vector2* linear, Float* angular)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return physics.GetVelocity(id, *linear, *angular); });
    }

    Int32 Body2D_SetLinearVelocity(EntityId entity, const Vector2* velocity)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return physics.SetLinearVelocity(id, *velocity); });
    }

    Int32 Body2D_SetAngularVelocity(EntityId entity, Float velocity)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return physics.SetAngularVelocity(id, velocity); });
    }

    Int32 Body2D_AddForce(EntityId entity, const Vector2* force, const Vector2* point)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return point != nullptr ? physics.AddForceAtPoint(id, *force, *point) : physics.AddForce(id, *force); });
    }

    Int32 Body2D_AddTorque(EntityId entity, Float torque)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return physics.AddTorque(id, torque); });
    }

    Int32 Body2D_AddImpulse(EntityId entity, const Vector2* impulse, const Vector2* point)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return point != nullptr ? physics.AddImpulseAtPoint(id, *impulse, *point) : physics.AddImpulse(id, *impulse); });
    }

    Int32 Body2D_AddAngularImpulse(EntityId entity, Float impulse)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return physics.AddAngularImpulse(id, impulse); });
    }

    Int32 Body2D_Teleport(EntityId entity, const Vector2* position, Float angle)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return physics.Teleport(id, *position, angle); });
    }

    Int32 Body2D_MoveKinematic(EntityId entity, const Vector2* position, Float angle)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return physics.MoveKinematic(id, *position, angle); });
    }

    Int32 Body2D_SetAwake(EntityId entity, Bool awake)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return physics.SetAwake(id, awake); });
    }

    Int32 Body2D_IsAwake(EntityId entity, Bool* awake)
    {
        return OnBody(GetPhysics2D(), entity, [&](PhysicsScene2D& physics, UUID id) { return physics.IsAwake(id, *awake); });
    }

    Int32 Joint2D_IsBroken(EntityId entity, Bool* broken)
    {
        return JointBroken(GetPhysics2D(), entity, broken);
    }

    Int32 Physics_Raycast(const Vector3* origin, const Vector3* direction, Float maxDistance, const QueryFilter* filter, Bool all, PhysicsHit* hits, Int32 capacity)
    {
        const PhysicsScene3D* l_Physics = GetPhysics();
        if (l_Physics == nullptr)
        {
            return 0;
        }

        const PhysicsQueryFilter3D l_Filter = ToFilter<PhysicsQueryFilter3D>(filter);
        if (all)
        {
            const std::vector<PhysicsHit3D> l_Hits = l_Physics->RaycastAll(*origin, *direction, maxDistance, l_Filter);

            return WriteHits(std::span<const PhysicsHit3D>(l_Hits), hits, capacity);
        }

        return WriteHit(l_Physics->Raycast(*origin, *direction, maxDistance, l_Filter), hits, capacity);
    }

    Int32 Physics_ShapeCast(const QueryShape* shape, const Vector3* position, const Quaternion* rotation, const Vector3* direction, Float maxDistance, const QueryFilter* filter, Bool all, PhysicsHit* hits, Int32 capacity)
    {
        const std::optional<PhysicsQueryShape3D> l_Shape = ToShape(*shape);
        if (!l_Shape)
        {
            return -1;
        }

        const PhysicsScene3D* l_Physics = GetPhysics();
        if (l_Physics == nullptr)
        {
            return 0;
        }

        const PhysicsQueryFilter3D l_Filter = ToFilter<PhysicsQueryFilter3D>(filter);
        const glm::quat l_Rotation = glm::normalize(*rotation);
        if (all)
        {
            const std::vector<PhysicsHit3D> l_Hits = l_Physics->ShapeCastAll(*l_Shape, *position, l_Rotation, *direction, maxDistance, l_Filter);

            return WriteHits(std::span<const PhysicsHit3D>(l_Hits), hits, capacity);
        }

        return WriteHit(l_Physics->ShapeCast(*l_Shape, *position, l_Rotation, *direction, maxDistance, l_Filter), hits, capacity);
    }

    Int32 Physics_Overlap(const QueryShape* shape, const Vector3* position, const Quaternion* rotation, const QueryFilter* filter, PhysicsHit* hits, Int32 capacity)
    {
        const std::optional<PhysicsQueryShape3D> l_Shape = ToShape(*shape);
        if (!l_Shape)
        {
            return -1;
        }

        const PhysicsScene3D* l_Physics = GetPhysics();
        if (l_Physics == nullptr)
        {
            return 0;
        }

        const std::vector<PhysicsHit3D> l_Hits = l_Physics->Overlap(*l_Shape, *position, glm::normalize(*rotation), ToFilter<PhysicsQueryFilter3D>(filter));

        return WriteHits(std::span<const PhysicsHit3D>(l_Hits), hits, capacity);
    }

    Int32 Physics2D_Raycast(const Vector2* origin, const Vector2* direction, Float maxDistance, const QueryFilter* filter, Bool all, PhysicsHit2D* hits, Int32 capacity)
    {
        const PhysicsScene2D* l_Physics = GetPhysics2D();
        if (l_Physics == nullptr)
        {
            return 0;
        }

        const PhysicsQueryFilter2D l_Filter = ToFilter<PhysicsQueryFilter2D>(filter);
        if (all)
        {
            const std::vector<Trinity::PhysicsHit2D> l_Hits = l_Physics->RaycastAll(*origin, *direction, maxDistance, l_Filter);

            return WriteHits(std::span<const Trinity::PhysicsHit2D>(l_Hits), hits, capacity);
        }

        return WriteHit(l_Physics->Raycast(*origin, *direction, maxDistance, l_Filter), hits, capacity);
    }

    Int32 Physics2D_ShapeCast(const QueryShape2D* shape, const Vector2* position, Float angle, const Vector2* direction, Float maxDistance, const QueryFilter* filter, Bool all, PhysicsHit2D* hits, Int32 capacity)
    {
        const std::optional<PhysicsQueryShape2D> l_Shape = ToShape(*shape);
        if (!l_Shape)
        {
            return -1;
        }

        const PhysicsScene2D* l_Physics = GetPhysics2D();
        if (l_Physics == nullptr)
        {
            return 0;
        }

        const PhysicsQueryFilter2D l_Filter = ToFilter<PhysicsQueryFilter2D>(filter);
        if (all)
        {
            const std::vector<Trinity::PhysicsHit2D> l_Hits = l_Physics->ShapeCastAll(*l_Shape, *position, angle, *direction, maxDistance, l_Filter);

            return WriteHits(std::span<const Trinity::PhysicsHit2D>(l_Hits), hits, capacity);
        }

        return WriteHit(l_Physics->ShapeCast(*l_Shape, *position, angle, *direction, maxDistance, l_Filter), hits, capacity);
    }

    Int32 Physics2D_Overlap(const QueryShape2D* shape, const Vector2* position, Float angle, const QueryFilter* filter, PhysicsHit2D* hits, Int32 capacity)
    {
        const std::optional<PhysicsQueryShape2D> l_Shape = ToShape(*shape);
        if (!l_Shape)
        {
            return -1;
        }

        const PhysicsScene2D* l_Physics = GetPhysics2D();
        if (l_Physics == nullptr)
        {
            return 0;
        }

        const std::vector<Trinity::PhysicsHit2D> l_Hits = l_Physics->Overlap(*l_Shape, *position, angle, ToFilter<PhysicsQueryFilter2D>(filter));

        return WriteHits(std::span<const Trinity::PhysicsHit2D>(l_Hits), hits, capacity);
    }
}