#include "Trinity/RHI/PipelineCacheFile.hpp"

#include "Trinity/Core/Hash.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Version.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            constexpr std::array<char, 8> c_Magic{ 'T', 'R', 'P', 'S', 'O', 'C', 'A', 'C' };
            // Raised whenever the header changes, so a file of another layout is refused before anything else in it is read
            constexpr std::uint32_t c_FormatVersion = 1;

            struct FileHeader
            {
                std::array<char, 8> Magic{};
                std::uint32_t FormatVersion = 0;
                std::uint32_t API = 0;
                std::uint32_t VendorID = 0;
                std::uint32_t DeviceID = 0;
                std::uint64_t DriverVersion = 0;
                std::array<std::uint8_t, 16> DriverCacheID{};
                // The engine's version and commit, padded with zeros
                std::array<char, 64> EngineVersion{};
                std::uint64_t DataSize = 0;
                // The driver's data hashed with XXH64, seeded with the hash of the header before this
                std::uint64_t Checksum = 0;
            };

            static_assert(std::is_trivially_copyable_v<FileHeader> && sizeof(FileHeader) == 128, "The header has no padding, so it is written as it is");

            // Why a file cannot be used: damaged, or made by another API, adapter, driver or engine version
            struct Refusal
            {
                std::string Reason;
                bool Damaged = false;
            };

            FileHeader MakeHeader(const PipelineCacheKey& key)
            {
                FileHeader l_Header;
                l_Header.Magic = c_Magic;
                l_Header.FormatVersion = c_FormatVersion;
                l_Header.API = static_cast<std::uint32_t>(key.API);
                l_Header.VendorID = key.VendorID;
                l_Header.DeviceID = key.DeviceID;
                l_Header.DriverVersion = key.DriverVersion;
                l_Header.DriverCacheID = key.DriverCacheID;

                const std::string l_Version = std::format("{} {}", GetVersionString(), GetBuildCommit());
                std::copy_n(l_Version.begin(), std::min(l_Version.size(), l_Header.EngineVersion.size() - 1), l_Header.EngineVersion.begin());

                return l_Header;
            }

            std::uint64_t GetChecksum(const FileHeader& header, std::span<const std::byte> data)
            {
                const std::uint64_t l_Seed = Hash::XXHash64(std::as_bytes(std::span(&header, 1)).first(offsetof(FileHeader, Checksum)));

                return Hash::XXHash64(data, l_Seed);
            }

            std::string_view GetEngineVersion(const FileHeader& header)
            {
                const std::string_view l_Version(header.EngineVersion.data(), header.EngineVersion.size());

                return l_Version.substr(0, l_Version.find('\0'));
            }

            // The layout and the checksum first, so nothing a damaged file holds is read as what made it
            std::optional<Refusal> Check(std::span<const std::byte> file, const FileHeader& expected)
            {
                if (file.size() < sizeof(FileHeader))
                {
                    return Refusal{ std::format("is damaged: it is {} bytes, shorter than its header", file.size()), true };
                }

                FileHeader l_Header;
                std::memcpy(&l_Header, file.data(), sizeof(FileHeader));
                if (l_Header.Magic != c_Magic)
                {
                    return Refusal{ "is damaged: it does not begin as a pipeline cache does", true };
                }

                if (l_Header.FormatVersion != c_FormatVersion)
                {
                    return Refusal{ std::format("was written in format {}, and this build reads format {}", l_Header.FormatVersion, c_FormatVersion), false };
                }

                const std::span<const std::byte> l_Data = file.subspan(sizeof(FileHeader));
                if (l_Header.DataSize != l_Data.size())
                {
                    return Refusal{ std::format("is damaged: its header gives {} bytes of data, and it holds {}", l_Header.DataSize, l_Data.size()), true };
                }

                if (GetChecksum(l_Header, l_Data) != l_Header.Checksum)
                {
                    return Refusal{ "is damaged: its data does not match its checksum", true };
                }

                if (l_Header.API != expected.API)
                {
                    return Refusal{ std::format("was made for {}", ToString(static_cast<GraphicsAPI>(l_Header.API))), false };
                }

                if (l_Header.VendorID != expected.VendorID || l_Header.DeviceID != expected.DeviceID)
                {
                    return Refusal{ std::format("was made on adapter {:04x}:{:04x}, and this one is {:04x}:{:04x}", l_Header.VendorID, l_Header.DeviceID, expected.VendorID, expected.DeviceID), false };
                }

                if (l_Header.DriverVersion != expected.DriverVersion)
                {
                    return Refusal{ std::format("was made by driver version {:#x}, and this one is {:#x}", l_Header.DriverVersion, expected.DriverVersion), false };
                }

                if (l_Header.DriverCacheID != expected.DriverCacheID)
                {
                    return Refusal{ "was made by a driver whose caches this one does not read", false };
                }

                if (l_Header.EngineVersion != expected.EngineVersion)
                {
                    return Refusal{ std::format("was made by Trinity {}, and this is {}", GetEngineVersion(l_Header), GetEngineVersion(expected)), false };
                }

                return std::nullopt;
            }
        }

        void PipelineCacheFile::Open(std::string_view path, const PipelineCacheKey& key)
        {
            m_Path = path;
            m_Key = key;
            if (m_Path.empty())
            {
                return;
            }

            const std::string_view l_API = ToString(key.API);
            Expected<FileBuffer, FileError> l_File = FileSystem::ReadFile(m_Path);
            if (!l_File)
            {
                if (l_File.GetError() != FileError::NotFound)
                {
                    TR_CORE_WARN("{}: the pipeline cache '{}' could not be read ({}), so a new one is started", l_API, m_Path, ToString(l_File.GetError()));
                }

                return;
            }

            if (const std::optional<Refusal> l_Refusal = Check(*l_File, MakeHeader(key)))
            {
                if (l_Refusal->Damaged)
                {
                    TR_CORE_WARN("{}: the pipeline cache '{}' {}, so a new one is started", l_API, m_Path, l_Refusal->Reason);
                }
                else
                {
                    TR_CORE_INFO("{}: the pipeline cache '{}' {}, so a new one is started", l_API, m_Path, l_Refusal->Reason);
                }

                return;
            }

            m_Contents = std::move(*l_File);
            TR_CORE_INFO("{}: the pipeline cache '{}' was loaded ({})", l_API, m_Path, Memory::FormatBytes(GetData().size()));
        }

        void PipelineCacheFile::Close(std::span<const std::byte> data)
        {
            if (m_Path.empty())
            {
                return;
            }

            const std::string l_Path = std::exchange(m_Path, std::string());
            const std::string_view l_API = ToString(m_Key.API);
            const std::uint32_t l_Count = m_Count.load();
            const std::uint32_t l_FromCache = m_FromCache.load();
            const std::string l_Pipelines = std::format("{} pipeline(s) took {:.1f} ms, {} of them from the pipeline cache", l_Count, static_cast<double>(m_Nanoseconds.load()) / 1.0e6, l_FromCache);
            if (l_FromCache == l_Count)
            {
                TR_CORE_INFO("{}: {}, which is left as it was", l_API, l_Pipelines);

                return;
            }

            if (data.empty())
            {
                TR_CORE_WARN("{}: {}, and the driver gave nothing to save", l_API, l_Pipelines);

                return;
            }

            FileHeader l_Header = MakeHeader(m_Key);
            l_Header.DataSize = data.size();
            l_Header.Checksum = GetChecksum(l_Header, data);

            std::vector<std::byte> l_File(sizeof(FileHeader) + data.size());
            std::memcpy(l_File.data(), &l_Header, sizeof(FileHeader));
            std::ranges::copy(data, l_File.begin() + sizeof(FileHeader));

            if (const Expected<void, FileError> l_Written = FileSystem::WriteFile(l_Path, l_File); !l_Written)
            {
                TR_CORE_WARN("{}: {}, and the pipeline cache could not be saved to '{}' ({})", l_API, l_Pipelines, l_Path, ToString(l_Written.GetError()));

                return;
            }

            TR_CORE_INFO("{}: {}, and the pipeline cache was saved to '{}' ({})", l_API, l_Pipelines, l_Path, Memory::FormatBytes(l_File.size()));
        }

        void PipelineCacheFile::Record(std::chrono::steady_clock::duration duration, bool fromCache)
        {
            m_Nanoseconds.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(), std::memory_order_relaxed);
            m_FromCache.fetch_add(fromCache ? 1 : 0, std::memory_order_relaxed);
            m_Count.fetch_add(1, std::memory_order_relaxed);
        }

        std::span<const std::byte> PipelineCacheFile::GetData() const
        {
            if (m_Contents.empty())
            {
                return {};
            }

            return std::span<const std::byte>(m_Contents.data(), m_Contents.size()).subspan(sizeof(FileHeader));
        }
    }
}