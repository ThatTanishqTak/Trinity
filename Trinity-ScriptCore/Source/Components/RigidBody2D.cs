using Trinity.Interop;

namespace Trinity
{
    // What a script does to the 2D body itself as the scene runs, as to a 3D one: in the XY plane, with turns counter-clockwise in radians. Only a dynamic body takes velocities, forces and impulses, a force or torque lasts for the next step, and an impulse changes the velocity at once. A body the physics has not made throws
    public readonly unsafe partial struct RigidBody2D
    {
        private const string c_NoBody = "has a RigidBody2D the running scene's physics has not made";

        public Vector2 LinearVelocity
        {
            get
            {
                Vector2 l_Linear;
                float l_Angular;
                Check(Native.Body2D_GetVelocity(m_Entity.ID, &l_Linear, &l_Angular), "has velocities");

                return l_Linear;
            }
            set => Check(Native.Body2D_SetLinearVelocity(m_Entity.ID, &value), "takes a velocity");
        }

        public float AngularVelocity
        {
            get
            {
                Vector2 l_Linear;
                float l_Angular;
                Check(Native.Body2D_GetVelocity(m_Entity.ID, &l_Linear, &l_Angular), "has velocities");

                return l_Angular;
            }
            set => Check(Native.Body2D_SetAngularVelocity(m_Entity.ID, value), "takes a velocity");
        }

        public void AddForce(Vector2 force) => Check(Native.Body2D_AddForce(m_Entity.ID, &force, null), "takes a force");

        public void AddForceAtPoint(Vector2 force, Vector2 point) => Check(Native.Body2D_AddForce(m_Entity.ID, &force, &point), "takes a force");

        public void AddTorque(float torque) => Check(Native.Body2D_AddTorque(m_Entity.ID, torque), "takes a torque");

        public void AddImpulse(Vector2 impulse) => Check(Native.Body2D_AddImpulse(m_Entity.ID, &impulse, null), "takes an impulse");

        public void AddImpulseAtPoint(Vector2 impulse, Vector2 point) => Check(Native.Body2D_AddImpulse(m_Entity.ID, &impulse, &point), "takes an impulse");

        public void AddAngularImpulse(float impulse) => Check(Native.Body2D_AddAngularImpulse(m_Entity.ID, impulse), "takes an impulse");

        // The body and its entity there at once, at the entity's depth and turned about Z alone, its velocities kept
        public void Teleport(Vector2 position, float angle) => Check(Native.Body2D_Teleport(m_Entity.ID, &position, angle), "moves");

        // Where a kinematic body goes over the next frame's steps
        public void MoveKinematic(Vector2 position, float angle) => Check(Native.Body2D_MoveKinematic(m_Entity.ID, &position, angle), "moves as a kinematic body", "has a RigidBody2D that is not kinematic, which MoveKinematic cannot move");

        public bool IsAwake
        {
            get
            {
                bool l_Awake;
                Check(Native.Body2D_IsAwake(m_Entity.ID, &l_Awake), "sleeps");

                return l_Awake;
            }
        }

        public void Wake() => Check(Native.Body2D_SetAwake(m_Entity.ID, true), "wakes");

        public void Sleep() => Check(Native.Body2D_SetAwake(m_Entity.ID, false), "sleeps");

        // The message made only for a failure, so a call that works allocates nothing
        private void Check(int result, string what, string? badValue = null)
        {
            if ((AccessResult)result != AccessResult.Done)
            {
                ComponentAccess.Check(m_Entity, (AccessResult)result, c_NoBody, badValue ?? $"has a static or kinematic RigidBody2D, and only a dynamic one {what}");
            }
        }
    }
}