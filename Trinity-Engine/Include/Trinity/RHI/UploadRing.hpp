#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class Device;

        // The CPU writes Data, and shaders read the same bytes at Offset through ShaderResourceIndex
        struct UploadAllocation
        {
            std::span<std::byte> Data;
            BufferHandle Buffer;
            std::uint64_t Offset = 0;
            std::uint32_t ShaderResourceIndex = c_NoBindlessIndex;
        };

        // One mapped buffer with a slot per frame in flight, reused once the device has waited for the slot's last frame. What does not fit goes into temporary buffers that are destroyed when the frame ends
        class UploadRing
        {
        public:
            [[nodiscard]] bool Initialize(Device& device, std::string& error);
            void Shutdown();

            void BeginFrame();
            void EndFrame();

            [[nodiscard]] UploadAllocation Allocate(std::uint64_t size, std::uint64_t alignment);
            [[nodiscard]] std::uint64_t GetCapacity() const { return m_Capacity; }

        private:
            using BufferList = std::vector<BufferHandle, TaggedAllocator<BufferHandle, MemoryTag::Renderer>>;

            [[nodiscard]] UploadAllocation AllocateOverflow(std::uint64_t size);

            Device* m_Device = nullptr;
            BufferHandle m_Buffer;
            std::span<std::byte> m_Data;
            std::uint32_t m_ShaderResourceIndex = c_NoBindlessIndex;
            std::uint64_t m_Capacity = 0;
            std::uint64_t m_FrameNumber = 0;
            std::uint64_t m_Begin = 0;
            std::uint64_t m_Offset = 0;
            std::uint64_t m_OverflowBytes = 0;
            BufferList m_OverflowBuffers;
            bool m_InFrame = false;
        };
    }
}