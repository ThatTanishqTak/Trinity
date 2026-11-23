using Trinity.Interop;

namespace Trinity
{
    // In world space, in metres and seconds. Moves before the next step add up. Unmade, it throws
    public readonly unsafe partial struct CharacterController
    {
        private const string c_NoCharacter = "has a CharacterController the running scene's physics has not made";
        private const string c_NotCharacter = "is part of a body the running scene's physics made, which is not a character";

        public Vector3 Velocity
        {
            get
            {
                Vector3 l_Velocity;
                Check(Native.Character_GetVelocity(m_Entity.ID, &l_Velocity));

                return l_Velocity;
            }
            set => Check(Native.Character_SetVelocity(m_Entity.ID, &value));
        }

        public CharacterGround Ground
        {
            get
            {
                bool l_Grounded;
                Vector3 l_Normal;
                Vector3 l_Velocity;
                ulong l_Entity;
                Check(Native.Character_GetGround(m_Entity.ID, &l_Grounded, &l_Normal, &l_Velocity, &l_Entity));

                return new CharacterGround(l_Grounded, l_Normal, l_Velocity, new Entity(l_Entity));
            }
        }

        // Its velocity is left as it was
        public void Move(Vector3 displacement) => Check(Native.Character_Move(m_Entity.ID, &displacement));

        // The character and its entity there at once, its velocity kept. Its capsule stays upright
        public void Teleport(Vector3 position, Quaternion rotation) => Check(Native.Body_Teleport(m_Entity.ID, &position, &rotation));

        // The message made only for a failure, so a call that works allocates nothing
        private void Check(int result)
        {
            if ((AccessResult)result != AccessResult.Done)
            {
                ComponentAccess.Check(m_Entity, (AccessResult)result, c_NoCharacter, c_NotCharacter);
            }
        }
    }
}