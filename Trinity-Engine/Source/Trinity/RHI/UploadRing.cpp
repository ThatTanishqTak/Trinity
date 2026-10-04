#include "Trinity/RHI/UploadRing.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/RHI/Device.hpp"

#include <bit>
#include <format>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            constexpr std::int32_t c_DefaultCapacityKiB = 4096;
            constexpr BufferUsage c_RingUsage = BufferUsage::ShaderResource | BufferUsage::Index;

            ConsoleVariable<std::int32_t> s_UploadCapacityVariable("renderer.upload_capacity", c_DefaultCapacityKiB, "Upload memory per frame in KiB before uploads fall back to temporary buffers; 0 or less uses 4096", ConsoleVariableFlags::ReadOnly);
        }

        bool UploadRing::Initialize(Device& device, std::string& error)
        {
            const std::int32_t l_CapacityKiB = s_UploadCapacityVariable.Get() > 0 ? s_UploadCapacityVariable.Get() : c_DefaultCapacityKiB;
            m_Capacity = std::uint64_t{ static_cast<std::uint32_t>(l_CapacityKiB) } * 1024;

            BufferDescription l_Description;
            l_Description.Size = m_Capacity * c_FramesInFlight;
            l_Description.Usage = c_RingUsage;
            l_Description.Memory = MemoryType::Upload;
            l_Description.DebugName = "Upload ring";

            m_Buffer = device.CreateBuffer(l_Description);
            m_Data = m_Buffer ? device.GetMappedData(m_Buffer) : std::span<std::byte>();
            m_ShaderResourceIndex = m_Buffer ? device.GetShaderResourceIndex(m_Buffer) : c_NoBindlessIndex;
            if (m_Data.size() != l_Description.Size || m_ShaderResourceIndex == c_NoBindlessIndex)
            {
                error = std::format("the {} upload ring could not be created, mapped or given a bindless index", Memory::FormatBytes(l_Description.Size));
                device.DestroyBuffer(m_Buffer);
                m_Buffer = {};

                return false;
            }

            m_Device = &device;
            TR_CORE_INFO("Upload ring: {} for each of {} frames in flight, at bindless index {}", Memory::FormatBytes(m_Capacity), c_FramesInFlight, m_ShaderResourceIndex);

            return true;
        }

        void UploadRing::Shutdown()
        {
            if (m_Device == nullptr)
            {
                return;
            }

            for (BufferHandle it_Buffer : m_OverflowBuffers)
            {
                m_Device->DestroyBuffer(it_Buffer);
            }

            BufferList().swap(m_OverflowBuffers);
            m_Device->DestroyBuffer(m_Buffer);
            m_Buffer = {};
            m_Data = {};
            m_Device = nullptr;
        }

        void UploadRing::BeginFrame()
        {
            m_Begin = (m_FrameNumber % c_FramesInFlight) * m_Capacity;
            m_Offset = m_Begin;
            m_InFrame = true;
        }

        // The device's release queue keeps the temporary buffers until this frame has finished on the GPU
        void UploadRing::EndFrame()
        {
            if (!m_OverflowBuffers.empty())
            {
                TR_CORE_WARN("Upload ring overflowed: the frame used {} of a {} slot, {} of it in {} temporary buffer(s)", Memory::FormatBytes(m_Offset - m_Begin + m_OverflowBytes), Memory::FormatBytes(m_Capacity), Memory::FormatBytes(m_OverflowBytes), m_OverflowBuffers.size());

                for (BufferHandle it_Buffer : m_OverflowBuffers)
                {
                    m_Device->DestroyBuffer(it_Buffer);
                }

                BufferList().swap(m_OverflowBuffers);
                m_OverflowBytes = 0;
            }

            ++m_FrameNumber;
            m_InFrame = false;
        }

        UploadAllocation UploadRing::Allocate(std::uint64_t size, std::uint64_t alignment)
        {
            TR_CORE_ASSERT(m_InFrame, "AllocateUpload is called between BeginFrame and EndFrame.");
            TR_CORE_ASSERT(size != 0 && std::has_single_bit(alignment), "An upload needs a size and a power-of-two alignment.");
            if (m_Device == nullptr || size == 0)
            {
                return {};
            }

            const std::uint64_t l_End = m_Begin + m_Capacity;
            const std::uint64_t l_Start = (m_Offset + alignment - 1) & ~(alignment - 1);
            if (l_Start > l_End || size > l_End - l_Start)
            {
                return AllocateOverflow(size);
            }

            m_Offset = l_Start + size;

            return { m_Data.subspan(static_cast<std::size_t>(l_Start), static_cast<std::size_t>(size)), m_Buffer, l_Start, m_ShaderResourceIndex };
        }

        // Rounded up to whole 16 bytes, since a raw view covers whole 32-bit words
        UploadAllocation UploadRing::AllocateOverflow(std::uint64_t size)
        {
            BufferDescription l_Description;
            l_Description.Size = (size + 15) & ~std::uint64_t{ 15 };
            l_Description.Usage = c_RingUsage;
            l_Description.Memory = MemoryType::Upload;
            l_Description.DebugName = "Upload ring overflow";

            const BufferHandle l_Buffer = m_Device->CreateBuffer(l_Description);
            if (!l_Buffer)
            {
                return {};
            }

            m_OverflowBuffers.push_back(l_Buffer);
            m_OverflowBytes += size;

            return { m_Device->GetMappedData(l_Buffer).first(static_cast<std::size_t>(size)), l_Buffer, 0, m_Device->GetShaderResourceIndex(l_Buffer) };
        }
    }
}