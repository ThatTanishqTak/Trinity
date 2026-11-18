// One array per descriptor type on Vulkan, and the descriptor heaps on D3D12
export T getDescriptorFromHandle<T : IOpaqueDescriptor>(DescriptorHandle<T> handle)
{
    return defaultGetDescriptorFromHandle(handle, BindlessDescriptorOptions.None);
}