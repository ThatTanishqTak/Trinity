#pragma once

#include "Trinity/FileSystem/FileSystem.hpp"

#include <algorithm>
#include <memory>
#include <span>
#include <utility>

namespace Trinity
{
    // Ranged reads of bytes held in memory, which it shares, so it reads the bytes it opened even after their source replaces or drops them
    class MemoryReadHandle final : public FileReadHandle
    {
    public:
        explicit MemoryReadHandle(std::shared_ptr<const FileBuffer> data) : m_Data(std::move(data))
        {

        }

        [[nodiscard]] std::uint64_t GetSize() const override
        {
            return m_Data->size();
        }

        [[nodiscard]] Expected<std::size_t, FileError> Read(std::uint64_t offset, std::span<std::byte> destination) const override
        {
            if (offset >= m_Data->size())
            {
                return std::size_t{ 0 };
            }

            const std::span<const std::byte> l_Rest = std::span<const std::byte>(*m_Data).subspan(static_cast<std::size_t>(offset));
            const std::size_t l_Count = std::min(destination.size(), l_Rest.size());
            std::ranges::copy(l_Rest.first(l_Count), destination.begin());

            return l_Count;
        }

    private:
        std::shared_ptr<const FileBuffer> m_Data;
    };
}