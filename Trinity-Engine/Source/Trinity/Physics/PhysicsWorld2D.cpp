#include "Trinity/Physics/PhysicsWorld2DState.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/JobSystem.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Trinity
{
    namespace
    {
        // Box2D keeps scratch for each of its workers, and has room for this many
        constexpr std::uint32_t c_MaxWorkers = 64;
        // Shapes smaller than this are taken as this, since Box2D refuses empty ones
        constexpr float c_MinimumSize = 1e-3f;

        void* AllocateBox2D(unsigned int size, int alignment)
        {
            return Memory::Allocate(size, MemoryTag::Physics, static_cast<std::size_t>(std::max(alignment, 16)));
        }

        void FreeBox2D(void* block)
        {
            if (block != nullptr)
            {
                Memory::Free(block);
            }
        }

        // Nonzero breaks into the debugger, as TR_CORE_ASSERT does
        int AssertBox2D(const char* condition, const char* file, int line)
        {
            TR_CORE_ERROR("Box2D: assertion '{}' failed at {}:{}", condition, file, line);

            return 1;
        }

        // Box2D's hooks are its own globals, set before its first allocation and left set, since they hold no state. Said once, rather than with every first world, which Play and Stop make many of
        void InstallHooks()
        {
            static bool s_Installed = false;
            if (std::exchange(s_Installed, true))
            {
                return;
            }

            b2SetAllocator(&AllocateBox2D, &FreeBox2D);
            b2SetAssertFcn(&AssertBox2D);

            const b2Version l_Version = b2GetVersion();
            TR_CORE_INFO("Physics: Box2D {}.{}.{} built for {}, running its tasks on Trinity's {} worker(s) and the main thread", l_Version.major, l_Version.minor, l_Version.revision, TR_PHYSICS_AVX2 ? "AVX2" : "SSE2", JobSystem::GetWorkerCount());
        }

        b2BodyId ToBox2D(PhysicsBodyID2D body)
        {
            return b2LoadBodyId(body.Value);
        }

        b2Vec2 ToBox2D(const glm::vec2& vector)
        {
            return { vector.x, vector.y };
        }

        glm::vec2 FromBox2D(b2Vec2 vector)
        {
            return { vector.x, vector.y };
        }

        b2BodyType ToBox2D(BodyMotion motion)
        {
            switch (motion)
            {
                case BodyMotion::Static:
                {
                    return b2_staticBody;
                }
                case BodyMotion::Kinematic:
                {
                    return b2_kinematicBody;
                }
                case BodyMotion::Dynamic:
                {
                    break;
                }
            }

            return b2_dynamicBody;
        }

        // In as many ranges as there are threads, none shorter than Box2D asks, and at least one job, since the solver's tasks each hold a worker that the others wait on. Each range is told the index of the thread it runs on, which is unique among the threads running at once. A step that wants more tasks than there are records runs the rest where it is
        void* EnqueueTask(b2TaskCallback* task, int itemCount, int minRange, void* taskContext, void* userContext)
        {
            Box2DTaskQueue& l_Queue = *static_cast<Box2DTaskQueue*>(userContext);
            const std::size_t l_Index = l_Queue.Count.fetch_add(1, std::memory_order_relaxed);
            if (l_Index >= Box2DTaskQueue::c_MaxTasks || itemCount <= 0)
            {
                task(0, itemCount, JobSystem::GetThreadIndex(), taskContext);

                return nullptr;
            }

            const int l_Threads = static_cast<int>(JobSystem::GetWorkerCount()) + 1;
            const int l_Jobs = std::clamp(itemCount / std::max(minRange, 1), 1, l_Threads);
            const int l_Size = (itemCount + l_Jobs - 1) / l_Jobs;
            JobCounter& l_Task = l_Queue.Tasks[l_Index];
            for (int it_Start = 0; it_Start < itemCount; it_Start += l_Size)
            {
                const int l_End = std::min(it_Start + l_Size, itemCount);
                JobSystem::Submit([task, it_Start, l_End, taskContext]
                {
                    task(it_Start, l_End, JobSystem::GetThreadIndex(), taskContext);
                }, &l_Task);
            }

            return &l_Task;
        }

        void FinishTask(void* userTask, [[maybe_unused]] void* userContext)
        {
            JobSystem::Wait(*static_cast<JobCounter*>(userTask));
        }
    }

    // Box2D's tasks run in parallel when every thread's index fits its workers, which all but a machine with Trinity's most workers do. On that one they run on the main thread
    PhysicsWorld2D::PhysicsWorld2D(const PhysicsWorldSettings2D& settings)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "Physics worlds are made on the main thread.");
        InstallHooks();

        m_State = Memory::New<State>(MemoryTag::Physics);
        m_State->SubSteps = std::max(settings.SubSteps, 1u);
        m_State->Layers = settings.Layers;

        const std::uint32_t l_Threads = JobSystem::GetWorkerCount() + 1;
        b2WorldDef l_Definition = b2DefaultWorldDef();
        l_Definition.gravity = ToBox2D(settings.Gravity);
        if (l_Threads <= c_MaxWorkers)
        {
            l_Definition.workerCount = static_cast<int>(l_Threads);
            l_Definition.enqueueTask = &EnqueueTask;
            l_Definition.finishTask = &FinishTask;
            l_Definition.userTaskContext = &m_State->Tasks;
        }

        m_State->World = b2CreateWorld(&l_Definition);
    }

    // Box2D frees every body still in the world with it
    PhysicsWorld2D::~PhysicsWorld2D()
    {
        b2DestroyWorld(m_State->World);
        Memory::Delete(m_State);
    }

    // A box's corners are left sharp, as a box collider's are. A layer's filter puts the body in its layer and lets it meet the layers the matrix says, which is exact both ways since the matrix is even
    PhysicsBodyID2D PhysicsWorld2D::CreateBody(const BodyDescription2D& description)
    {
        b2BodyDef l_Body = b2DefaultBodyDef();
        l_Body.type = ToBox2D(description.Motion);
        l_Body.position = ToBox2D(description.Position);
        l_Body.rotation = Box2DConvert::ToRotation(description.Angle);
        l_Body.linearVelocity = ToBox2D(description.LinearVelocity);
        l_Body.angularVelocity = description.AngularVelocity;
        l_Body.linearDamping = description.LinearDamping;
        l_Body.angularDamping = description.AngularDamping;
        l_Body.gravityScale = description.GravityFactor;
        l_Body.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(description.UserData));
        const b2BodyId l_ID = b2CreateBody(m_State->World, &l_Body);

        const std::uint32_t l_Layer = description.Layer < c_MaxCollisionLayers ? description.Layer : 0;
        b2ShapeDef l_Shape = b2DefaultShapeDef();
        l_Shape.material.friction = description.Friction;
        l_Shape.material.restitution = description.Restitution;
        l_Shape.filter.categoryBits = 1ull << l_Layer;
        l_Shape.filter.maskBits = m_State->Layers.Rows[l_Layer] | Box2DLayers::c_QueryBit;
        if (description.Shape == BodyShape2D::Circle)
        {
            const b2Circle l_Circle{ { 0.0f, 0.0f }, std::max(description.Radius, c_MinimumSize) };
            b2CreateCircleShape(l_ID, &l_Shape, &l_Circle);
        }
        else
        {
            const b2Polygon l_Box = b2MakeBox(std::max(description.HalfExtents.x, c_MinimumSize), std::max(description.HalfExtents.y, c_MinimumSize));
            b2CreatePolygonShape(l_ID, &l_Shape, &l_Box);
        }

        // The shape's mass and inertia scaled to the mass asked for
        if (description.Motion == BodyMotion::Dynamic && description.Mass > 0.0f)
        {
            b2MassData l_Mass = b2Body_GetMassData(l_ID);
            const float l_Scale = l_Mass.mass > 0.0f ? description.Mass / l_Mass.mass : 1.0f;
            l_Mass.mass = description.Mass;
            l_Mass.rotationalInertia *= l_Scale;
            b2Body_SetMassData(l_ID, l_Mass);
        }

        return { b2StoreBodyId(l_ID) };
    }

    void PhysicsWorld2D::DestroyBody(PhysicsBodyID2D body)
    {
        if (body && b2Body_IsValid(ToBox2D(body)))
        {
            b2DestroyBody(ToBox2D(body));
        }
    }

    glm::vec2 PhysicsWorld2D::GetBodyPosition(PhysicsBodyID2D body) const
    {
        return FromBox2D(b2Body_GetPosition(ToBox2D(body)));
    }

    float PhysicsWorld2D::GetBodyAngle(PhysicsBodyID2D body) const
    {
        return Box2DConvert::ToAngle(b2Body_GetRotation(ToBox2D(body)));
    }

    glm::vec2 PhysicsWorld2D::GetBodyLinearVelocity(PhysicsBodyID2D body) const
    {
        return FromBox2D(b2Body_GetLinearVelocity(ToBox2D(body)));
    }

    bool PhysicsWorld2D::IsBodyActive(PhysicsBodyID2D body) const
    {
        return b2Body_IsAwake(ToBox2D(body));
    }

    void PhysicsWorld2D::Step(Timestep step)
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A 2D physics world steps on the main thread.");

        m_State->Tasks.Count.store(0, std::memory_order_relaxed);
        b2World_Step(m_State->World, step.GetSeconds(), static_cast<int>(m_State->SubSteps));
    }

    void PhysicsWorld2D::SetGravity(const glm::vec2& gravity)
    {
        b2World_SetGravity(m_State->World, ToBox2D(gravity));
    }

    glm::vec2 PhysicsWorld2D::GetGravity() const
    {
        return FromBox2D(b2World_GetGravity(m_State->World));
    }

    std::uint32_t PhysicsWorld2D::GetBodyCount() const
    {
        return static_cast<std::uint32_t>(b2World_GetCounters(m_State->World).bodyCount);
    }

    std::uint32_t PhysicsWorld2D::GetActiveBodyCount() const
    {
        return static_cast<std::uint32_t>(b2World_GetAwakeBodyCount(m_State->World));
    }
}