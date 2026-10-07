#include "Trinity/Asset/MeshLoader.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <algorithm>
#include <cstring>
#include <format>

namespace Trinity
{
    namespace
    {
        constexpr RHI::BufferUsage c_MeshUsage = RHI::BufferUsage::ShaderResource | RHI::BufferUsage::Index | RHI::BufferUsage::CopyDestination | RHI::BufferUsage::CopySource;
        constexpr std::uint64_t c_StagingAlignment = 16;
    }

    MeshLoader::MeshLoader(RHI::Device& device) : m_Device(device)
    {

    }

    std::string MeshLoader::GetLoadPath(const AssetRecord& record) const
    {
        return GetCookedMeshPath(record.ID);
    }

    // On a worker
    Expected<Asset*, std::string> MeshLoader::Load(std::span<const std::byte> data) const
    {
        TR_PROFILE_FUNCTION();

        const Expected<MeshFile, std::string> l_File = ReadMeshFile(data);
        if (!l_File)
        {
            return Unexpected{ l_File.GetError() };
        }

        MeshAsset* l_Mesh = Memory::New<MeshAsset>(MemoryTag::Assets);
        l_Mesh->m_Layout = l_File->Layout;
        l_Mesh->m_Bounds = l_File->Bounds;
        l_Mesh->m_Submeshes.assign(l_File->Submeshes.begin(), l_File->Submeshes.end());
        l_Mesh->m_Data.assign(l_File->Data.begin(), l_File->Data.end());

        return l_Mesh;
    }

    Expected<void, std::string> MeshLoader::Finish(Asset& asset) const
    {
        MeshAsset& l_Mesh = static_cast<MeshAsset&>(asset);

        RHI::BufferDescription l_Description;
        l_Description.Size = l_Mesh.m_Layout.Size;
        l_Description.Usage = c_MeshUsage;
        l_Description.DebugName = "Mesh";

        l_Mesh.m_Buffer = m_Device.CreateBuffer(l_Description);
        if (!l_Mesh.m_Buffer)
        {
            return Unexpected{ std::format("the device could not create a mesh buffer of {}", Memory::FormatBytes(l_Mesh.m_Layout.Size)) };
        }

        l_Mesh.m_ShaderResourceIndex = m_Device.GetShaderResourceIndex(l_Mesh.m_Buffer);
        l_Mesh.m_Loader = this;
        m_Pending.push_back(&l_Mesh);

        return {};
    }

    // Before any layer draws, so a mesh that became ready this frame already holds its data when it is drawn
    void MeshLoader::RecordUploads(RHI::CommandList& commands)
    {
        TR_PROFILE_FUNCTION();

        for (MeshAsset* it_Mesh : m_Pending)
        {
            Upload(commands, *it_Mesh);
        }

        m_Pending.clear();
    }

    // A mesh released before its upload was recorded is dropped from the queue, and the release queue keeps the GPU buffer until frames using it have finished
    void MeshLoader::Release(MeshAsset& mesh) const
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "Meshes are released on the main thread.");

        std::erase(m_Pending, &mesh);
        m_Device.DestroyBuffer(mesh.m_Buffer);
        mesh.m_Buffer = {};
        mesh.m_ShaderResourceIndex = RHI::c_NoBindlessIndex;
        mesh.m_Loader = nullptr;
    }

    // Through the upload ring when the mesh is small, otherwise a staging buffer of its own, destroyed at once and kept by the release queue until the copy has run
    void MeshLoader::Upload(RHI::CommandList& commands, MeshAsset& mesh)
    {
        const std::uint64_t l_Size = mesh.m_Layout.Size;

        RHI::BufferHandle l_Staging;
        std::uint64_t l_Base = 0;
        std::span<std::byte> l_Data;
        const bool l_FromRing = l_Size <= m_Device.GetUploadCapacity() / 4;
        if (l_FromRing)
        {
            const RHI::UploadAllocation l_Allocation = m_Device.AllocateUpload(l_Size, c_StagingAlignment);
            l_Staging = l_Allocation.Buffer;
            l_Base = l_Allocation.Offset;
            l_Data = l_Allocation.Data;
        }
        else
        {
            RHI::BufferDescription l_Description;
            l_Description.Size = l_Size;
            l_Description.Usage = RHI::BufferUsage::CopySource;
            l_Description.Memory = RHI::MemoryType::Upload;
            l_Description.DebugName = "Mesh staging";

            l_Staging = m_Device.CreateBuffer(l_Description);
            l_Data = l_Staging ? m_Device.GetMappedData(l_Staging) : std::span<std::byte>();
        }

        if (l_Data.size() < l_Size || mesh.m_Data.size() != l_Size)
        {
            TR_CORE_ERROR("Meshes: no staging memory for a mesh of {}, so it stays empty", Memory::FormatBytes(l_Size));
            m_Device.DestroyBuffer(l_FromRing ? RHI::BufferHandle{} : l_Staging);

            return;
        }

        std::memcpy(l_Data.data(), mesh.m_Data.data(), static_cast<std::size_t>(l_Size));
        commands.BufferBarrier(mesh.m_Buffer, RHI::ResourceState::Undefined, RHI::ResourceState::CopyDestination);
        commands.CopyBuffer(l_Staging, l_Base, mesh.m_Buffer, 0, l_Size);
        commands.BufferBarrier(mesh.m_Buffer, RHI::ResourceState::CopyDestination, RHI::ResourceState::Geometry);

        if (!l_FromRing)
        {
            m_Device.DestroyBuffer(l_Staging);
        }

        // The GPU copy is all that is needed from here, and an emptied vector would keep its capacity
        decltype(mesh.m_Data)().swap(mesh.m_Data);
    }
}