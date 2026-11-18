#pragma once

#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        // The graphics API, the adapter, its driver's version and the driver's own cache identity
        struct PipelineCacheKey
        {
            GraphicsAPI API = GraphicsAPI::None;
            std::uint32_t VendorID = 0;
            std::uint32_t DeviceID = 0;
            std::uint64_t DriverVersion = 0;
            std::array<std::uint8_t, 16> DriverCacheID{};
        };

        // A header with the key, engine version, size and hash, then the driver's data
        class PipelineCacheFile
        {
        public:
            // Keeps the file's data when this key and engine version made it and it is whole, else logs why
            void Open(std::string_view path, const PipelineCacheKey& key);
            // Saves when a pipeline was made that the cache lacked. Logs the count. Once only
            void Close(std::span<const std::byte> data);

            // From any thread
            void Record(std::chrono::steady_clock::duration duration, bool fromCache);

            // Valid while this lives
            [[nodiscard]] std::span<const std::byte> GetData() const;
            [[nodiscard]] const std::string& GetPath() const { return m_Path; }
            // Whether a pipeline was made that the cache did not have, so there is something new to save
            [[nodiscard]] bool HasNewPipelines() const { return m_FromCache.load() != m_Count.load(); }

        private:
            std::string m_Path;
            PipelineCacheKey m_Key;
            FileBuffer m_Contents;

            std::atomic<std::uint32_t> m_Count{ 0 };
            std::atomic<std::uint32_t> m_FromCache{ 0 };
            std::atomic<std::int64_t> m_Nanoseconds{ 0 };
        };
    }
}