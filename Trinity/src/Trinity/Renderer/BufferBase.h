#pragma once

#include <vulkan/vulkan.h>

namespace Trinity
{
    struct BufferBase
    {
        BufferBase() = default;
        ~BufferBase() = default;

        BufferBase(const BufferBase&) = delete;
        BufferBase& operator=(const BufferBase&) = delete;

        BufferBase(BufferBase&& other) noexcept : m_Buffer(other.m_Buffer), m_BufferMemory(other.m_BufferMemory), m_Count(other.m_Count)
        {
            other.m_Buffer = VK_NULL_HANDLE;
            other.m_BufferMemory = VK_NULL_HANDLE;
            other.m_Count = 0;
        }

        BufferBase& operator=(BufferBase&& other) noexcept
        {
            if (this != &other)
            {
                m_Buffer = other.m_Buffer;
                m_BufferMemory = other.m_BufferMemory;
                m_Count = other.m_Count;

                other.m_Buffer = VK_NULL_HANDLE;
                other.m_BufferMemory = VK_NULL_HANDLE;
                other.m_Count = 0;
            }

            return *this;
        }

    protected:
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_BufferMemory = VK_NULL_HANDLE;
        VkDeviceSize m_Count = 0;
    };
}