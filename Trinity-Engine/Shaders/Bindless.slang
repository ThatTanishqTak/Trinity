// Descriptor handles index one array per descriptor type on Vulkan, so the engine's set layout needs no mutable descriptors, and ResourceDescriptorHeap and SamplerDescriptorHeap on D3D12
export T getDescriptorFromHandle<T : IOpaqueDescriptor>(DescriptorHandle<T> handle)
{
    return defaultGetDescriptorFromHandle(handle, BindlessDescriptorOptions.None);
}