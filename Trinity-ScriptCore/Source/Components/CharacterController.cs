using Trinity.Interop;

namespace Trinity
{
    // What a script does to the character itself as the scene runs, in world space, in metres and seconds, on the running scene's physics, which takes the scene's changes in first, so a character added this frame answers. Its velocity is its own, which lasts until changed, against what it stands on. A move is covered over its next step besides its velocity, sliding, climbing and keeping to the floor as its velocity does, and the moves asked for before that step add up. Its ground is as its last step left it. A character the physics has not made throws
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

        // The character and its entity there at once, its velocity kept, drawn there with nothing between. Its capsule stays upright, and its entity takes the rotation
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