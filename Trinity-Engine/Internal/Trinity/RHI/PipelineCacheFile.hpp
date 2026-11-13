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
        // What a pipeline cache was made by: the graphics API, the adapter, its driver's version and, where the API has one, the driver's own identity for its caches
        struct PipelineCacheKey
        {
            GraphicsAPI API = GraphicsAPI::None;
            std::uint32_t VendorID = 0;
            std::uint32_t DeviceID = 0;
            std::uint64_t DriverVersion = 0;
            std::array<std::uint8_t, 16> DriverCacheID{};
        };

        // A pipeline cache's file in the virtual file system, a header with the key and engine version that made it and the size and hash of the driver's data after it, and the count of the pipelines made through it, which one line gives as it closes
        class PipelineCacheFile
        {
        public:
            // Keeps the driver's data from the file at path when this key and engine version made it and it is whole. One made by another, or a damaged one, gives one line saying why and no data, and a missing one neither. Without a path nothing is loaded or saved
            void Open(std::string_view path, const PipelineCacheKey& key);
            // Saves data as the file when a pipeline was made that the cache did not have, with one line giving the count either way. Does nothing without a path, or a second time
            void Close(std::span<const std::byte> data);

            // Counts a pipeline, from any thread
            void Record(std::chrono::steady_clock::duration duration, bool fromCache);

            // The driver's data from the file, valid while this lives
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