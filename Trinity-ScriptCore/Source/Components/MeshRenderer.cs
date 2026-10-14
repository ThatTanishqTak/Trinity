using Trinity.Interop;

namespace Trinity
{
    // The material of each slot, which each submesh names. A slot past the end, or with no material, draws with the default one
    public readonly partial struct MeshRenderer
    {
        public int MaterialCount => Native.MeshRenderer_GetMaterialCount(m_Entity.ID);

        public AssetRef<MaterialAsset> GetMaterial(int slot) => new(Native.MeshRenderer_GetMaterial(m_Entity.ID, slot));

        // The list grows to reach the slot, with no material in the slots between, up to slot 255
        public void SetMaterial(int slot, AssetRef<MaterialAsset> material)
        {
            ComponentAccess.Check(m_Entity, (AccessResult)Native.MeshRenderer_SetMaterial(m_Entity.ID, slot, material.ID), "has no MeshRenderer", $"has a MeshRenderer, which has no material slot {slot}, only 0 to 255");
        }
    }
}