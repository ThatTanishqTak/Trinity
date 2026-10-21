using Trinity.Interop;

namespace Trinity
{
    // What a script does to the 2D character itself as the scene runs, as to a 3D one, in the XY plane
    public readonly unsafe partial struct CharacterController2D
    {
        private const string c_NoCharacter = "has a CharacterController2D the running scene's physics has not made";
        private const string c_NotCharacter = "is part of a 2D body the running scene's physics made, which is not a character";

        public Vector2 Velocity
        {
            get
            {
                Vector2 l_Velocity;
                Check(Native.Character2D_GetVelocity(m_Entity.ID, &l_Velocity));

                return l_Velocity;
            }
            set => Check(Native.Character2D_SetVelocity(m_Entity.ID, &value));
        }

        public CharacterGround2D Ground
        {
            get
            {
                bool l_Grounded;
                Vector2 l_Normal;
                Vector2 l_Velocity;
                ulong l_Entity;
                Check(Native.Character2D_GetGround(m_Entity.ID, &l_Grounded, &l_Normal, &l_Velocity, &l_Entity));

                return new CharacterGround2D(l_Grounded, l_Normal, l_Velocity, new Entity(l_Entity));
            }
        }

        // Its velocity is left as it was
        public void Move(Vector2 displacement) => Check(Native.Character2D_Move(m_Entity.ID, &displacement));

        // The character and its entity there at once, at the entity's depth, its velocity kept, drawn there with nothing between. Its capsule stays upright, and its entity takes the turn about Z
        public void Teleport(Vector2 position, float angle) => Check(Native.Body2D_Teleport(m_Entity.ID, &position, angle));

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