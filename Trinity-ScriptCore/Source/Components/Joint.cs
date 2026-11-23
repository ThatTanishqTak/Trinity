using Trinity.Interop;

namespace Trinity
{
    // Whether the joint has broken past its break force or torque, which it stays once it has
    public readonly unsafe partial struct Joint
    {
        public bool IsBroken
        {
            get
            {
                bool l_Broken;
                ComponentAccess.Check(m_Entity, (AccessResult)Native.Joint_IsBroken(m_Entity.ID, &l_Broken), "has a Joint the running scene's physics has not made");

                return l_Broken;
            }
        }
    }

    public readonly unsafe partial struct Joint2D
    {
        public bool IsBroken
        {
            get
            {
                bool l_Broken;
                ComponentAccess.Check(m_Entity, (AccessResult)Native.Joint2D_IsBroken(m_Entity.ID, &l_Broken), "has a Joint2D the running scene's physics has not made");

                return l_Broken;
            }
        }
    }
}