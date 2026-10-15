using Trinity.Interop;

namespace Trinity
{
    // What a script does to the body itself as the scene runs, in world space, in metres, kilograms, seconds and radians, on the running scene's physics, which takes the scene's changes in first, so a body added this frame answers. Only a dynamic body takes velocities, forces and impulses, and a static one neither sleeps nor wakes. A force or torque lasts for the next step, so a script adds it in OnFixedUpdate, and an impulse changes the velocity at once. A body pushed is woken. A body the physics has not made, such as one whose shapes are still loading, throws
    public readonly unsafe partial struct RigidBody
    {
        private const string c_NoBody = "has a RigidBody the running scene's physics has not made";

        public Vector3 LinearVelocity
        {
            get
            {
                Vector3 l_Linear;
                Vector3 l_Angular;
                Check(Native.Body_GetVelocity(m_Entity.ID, &l_Linear, &l_Angular), "has velocities");

                return l_Linear;
            }
            set => Check(Native.Body_SetLinearVelocity(m_Entity.ID, &value), "takes a velocity");
        }

        public Vector3 AngularVelocity
        {
            get
            {
                Vector3 l_Linear;
                Vector3 l_Angular;
                Check(Native.Body_GetVelocity(m_Entity.ID, &l_Linear, &l_Angular), "has velocities");

                return l_Angular;
            }
            set => Check(Native.Body_SetAngularVelocity(m_Entity.ID, &value), "takes a velocity");
        }

        public void AddForce(Vector3 force) => Check(Native.Body_AddForce(m_Entity.ID, &force, null), "takes a force");

        public void AddForceAtPoint(Vector3 force, Vector3 point) => Check(Native.Body_AddForce(m_Entity.ID, &force, &point), "takes a force");

        public void AddTorque(Vector3 torque) => Check(Native.Body_AddTorque(m_Entity.ID, &torque), "takes a torque");

        public void AddImpulse(Vector3 impulse) => Check(Native.Body_AddImpulse(m_Entity.ID, &impulse, null), "takes an impulse");

        public void AddImpulseAtPoint(Vector3 impulse, Vector3 point) => Check(Native.Body_AddImpulse(m_Entity.ID, &impulse, &point), "takes an impulse");

        public void AddAngularImpulse(Vector3 impulse) => Check(Native.Body_AddAngularImpulse(m_Entity.ID, &impulse), "takes an impulse");

        // The body and its entity there at once, its velocities kept, drawn there with nothing between
        public void Teleport(Vector3 position, Quaternion rotation) => Check(Native.Body_Teleport(m_Entity.ID, &position, &rotation), "moves");

        // Where a kinematic body goes over the next frame's steps, pushing what is in its way
        public void MoveKinematic(Vector3 position, Quaternion rotation) => Check(Native.Body_MoveKinematic(m_Entity.ID, &position, &rotation), "moves as a kinematic body", "has a RigidBody that is not kinematic, which MoveKinematic cannot move");

        public bool IsAwake
        {
            get
            {
                bool l_Awake;
                Check(Native.Body_IsAwake(m_Entity.ID, &l_Awake), "sleeps");

                return l_Awake;
            }
        }

        public void Wake() => Check(Native.Body_SetAwake(m_Entity.ID, true), "wakes");

        public void Sleep() => Check(Native.Body_SetAwake(m_Entity.ID, false), "sleeps");

        // The message made only for a failure, so a call that works allocates nothing
        private void Check(int result, string what, string? badValue = null)
        {
            if ((AccessResult)result != AccessResult.Done)
            {
                ComponentAccess.Check(m_Entity, (AccessResult)result, c_NoBody, badValue ?? $"has a static or kinematic RigidBody, and only a dynamic one {what}");
            }
        }
    }
}