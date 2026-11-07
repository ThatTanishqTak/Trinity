#include "Trinity/FileSystem/ZipArchive.hpp"

#include "Trinity/Core/Hash.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <system_error>

namespace Trinity
{
    namespace
    {
        constexpr std::uint32_t c_LocalHeaderSignature = 0x04034b50u;
        constexpr std::uint32_t c_CentralHeaderSignature = 0x02014b50u;
        constexpr std::uint32_t c_EndSignature = 0x06054b50u;
        constexpr std::uint32_t c_Zip64LocatorSignature = 0x07064b50u;
        constexpr std::size_t c_LocalHeaderSize = 30;
        constexpr std::size_t c_CentralHeaderSize = 46;
        constexpr std::size_t c_EndSize = 22;
        constexpr std::size_t c_Zip64LocatorSize = 20;
        // The end record's comment is at most this long, so the record is somewhere in the file's last 64 KB and 22 bytes
        constexpr std::size_t c_MaximumComment = 0xFFFF;
        constexpr std::uint16_t c_Stored = 0;
        constexpr std::uint16_t c_Deflated = 8;
        // The host of version made by that gives its external attributes' upper half as a Unix mode
        constexpr std::uint8_t c_UnixHost = 3;
        constexpr std::uint16_t c_EncryptedFlag = 1;
        constexpr std::uint32_t c_Zip64Marker = 0xFFFFFFFFu;

        constexpr std::size_t c_ReadSize = 64 * 1024;
        // Deflate refers back at most 32 KB, so the window holds that much behind what is being written, and as much again
        constexpr std::size_t c_WindowSize = 32 * 1024;

        std::uint16_t Read16(const std::byte* data)
        {
            return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(data[0]) | (std::to_integer<std::uint16_t>(data[1]) << 8));
        }

        std::uint32_t Read32(const std::byte* data)
        {
            return std::to_integer<std::uint32_t>(data[0]) | (std::to_integer<std::uint32_t>(data[1]) << 8) | (std::to_integer<std::uint32_t>(data[2]) << 16) | (std::to_integer<std::uint32_t>(data[3]) << 24);
        }

        bool ReadAt(std::ifstream& file, std::uint64_t offset, std::span<std::byte> into)
        {
            file.clear();
            file.seekg(static_cast<std::streamoff>(offset));
            file.read(reinterpret_cast<char*>(into.data()), static_cast<std::streamsize>(into.size()));

            return file.gcount() == static_cast<std::streamsize>(into.size());
        }

        std::string Zip64Refusal()
        {
            return "it is a Zip64 zip, which holds a file of 4 GB or more or more than 65535 files, and Trinity reads no Zip64";
        }

        // An entry's deflated bytes as bits, least significant first, read from the zip as they are needed and never past the entry's end
        class BitReader
        {
        public:
            BitReader(std::ifstream& file, std::uint64_t size) : m_File(file), m_Left(size), m_Buffer(c_ReadSize)
            {

            }

            // At least count bits held, unless the entry has fewer left
            void Fill(int count)
            {
                while (m_Count < count)
                {
                    if (m_Position == m_End && !Refill())
                    {
                        return;
                    }

                    m_Bits |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(m_Buffer[m_Position++])) << m_Count;
                    m_Count += 8;
                }
            }

            // The next count bits, at most 16. Past the entry's end it gives zeros and is cut short from then on
            std::uint32_t Take(int count)
            {
                Fill(count);
                const std::uint32_t l_Value = static_cast<std::uint32_t>(m_Bits & ((std::uint64_t{ 1 } << count) - 1));
                Drop(count);

                return l_Value;
            }

            void Drop(int count)
            {
                if (count > m_Count)
                {
                    m_CutShort = true;
                    m_Bits = 0;
                    m_Count = 0;

                    return;
                }

                m_Bits >>= count;
                m_Count -= count;
            }

            // A stored block starts at the next byte
            void AlignToByte()
            {
                Drop(m_Count % 8);
            }

            [[nodiscard]] std::uint64_t Peek() const { return m_Bits; }
            [[nodiscard]] int GetCount() const { return m_Count; }
            [[nodiscard]] bool IsCutShort() const { return m_CutShort; }
            [[nodiscard]] bool HasReadFailed() const { return m_ReadFailed; }

        private:
            bool Refill()
            {
                if (m_Left == 0)
                {
                    return false;
                }

                const std::size_t l_Size = static_cast<std::size_t>(std::min<std::uint64_t>(m_Left, m_Buffer.size()));
                m_File.read(reinterpret_cast<char*>(m_Buffer.data()), static_cast<std::streamsize>(l_Size));
                if (m_File.gcount() != static_cast<std::streamsize>(l_Size))
                {
                    m_ReadFailed = true;
                    m_Left = 0;

                    return false;
                }

                m_Left -= l_Size;
                m_Position = 0;
                m_End = l_Size;

                return true;
            }

            std::ifstream& m_File;
            std::uint64_t m_Left = 0;
            std::vector<std::byte> m_Buffer;
            std::size_t m_Position = 0;
            std::size_t m_End = 0;
            std::uint64_t m_Bits = 0;
            int m_Count = 0;
            bool m_CutShort = false;
            bool m_ReadFailed = false;
        };

        // What has been inflated, handed on 32 KB at a time once the window fills, keeping the 32 KB before it for what refers back
        class InflateOutput
        {
        public:
            explicit InflateOutput(const ZipArchive::Sink& sink) : m_Sink(sink), m_Window(c_WindowSize * 2)
            {

            }

            // False once the sink has stopped it
            bool Put(std::byte value)
            {
                m_Window[m_Position++] = value;

                return m_Position < m_Window.size() || Slide();
            }

            // False for a distance further back than anything written, which the caller tells apart by IsStopped
            bool Copy(std::size_t distance, std::size_t length)
            {
                if (distance == 0 || distance > m_Position)
                {
                    return false;
                }

                for (std::size_t it_Byte = 0; it_Byte < length; ++it_Byte)
                {
                    if (!Put(m_Window[m_Position - distance]))
                    {
                        return false;
                    }
                }

                return true;
            }

            bool Flush()
            {
                if (m_Position == m_Flushed)
                {
                    return true;
                }

                const std::span<const std::byte> l_Piece(m_Window.data() + m_Flushed, m_Position - m_Flushed);
                m_Crc = Hash::Crc32(l_Piece, m_Crc);
                m_Written += l_Piece.size();
                m_Flushed = m_Position;
                if (!m_Sink(l_Piece))
                {
                    m_Stopped = true;

                    return false;
                }

                return true;
            }

            [[nodiscard]] std::uint64_t GetWritten() const { return m_Written + (m_Position - m_Flushed); }
            [[nodiscard]] std::uint32_t GetCrc() const { return m_Crc; }
            [[nodiscard]] bool IsStopped() const { return m_Stopped; }

        private:
            bool Slide()
            {
                if (!Flush())
                {
                    return false;
                }

                std::memmove(m_Window.data(), m_Window.data() + c_WindowSize, c_WindowSize);
                m_Position = c_WindowSize;
                m_Flushed = c_WindowSize;

                return true;
            }

            const ZipArchive::Sink& m_Sink;
            std::vector<std::byte> m_Window;
            std::size_t m_Position = 0;
            std::size_t m_Flushed = 0;
            std::uint64_t m_Written = 0;
            std::uint32_t m_Crc = 0;
            bool m_Stopped = false;
        };

        constexpr int c_MaximumBits = 15;
        // Codes this long or shorter are decoded by one look into a table, and longer ones a bit at a time
        constexpr int c_FastBits = 9;

        // A canonical Huffman code as deflate gives it, by its lengths, as RFC 1951 and zlib's puff decode it
        struct HuffmanTable
        {
            std::array<std::uint16_t, c_MaximumBits + 1> Count{};
            std::array<std::uint16_t, 288> Symbol{};
            // Each code of at most c_FastBits bits, at every index whose low bits are the code as it arrives: its symbol shifted left by 4, and its length. 0 for none
            std::array<std::uint16_t, 1u << c_FastBits> Fast{};
        };

        // How many codes the lengths leave unused, negative when they give more codes than there are. Deflate allows an unused code only where one code is given alone
        int BuildTable(HuffmanTable& table, std::span<const std::uint8_t> lengths)
        {
            table.Count.fill(0);
            table.Fast.fill(0);
            for (const std::uint8_t it_Length : lengths)
            {
                ++table.Count[it_Length];
            }

            if (table.Count[0] == lengths.size())
            {
                return 0;
            }

            int l_Left = 1;
            for (std::size_t it_Length = 1; it_Length <= c_MaximumBits; ++it_Length)
            {
                l_Left <<= 1;
                l_Left -= table.Count[it_Length];
                if (l_Left < 0)
                {
                    return l_Left;
                }
            }

            std::array<std::uint16_t, c_MaximumBits + 1> l_Offsets{};
            for (std::size_t it_Length = 1; it_Length < c_MaximumBits; ++it_Length)
            {
                l_Offsets[it_Length + 1] = static_cast<std::uint16_t>(l_Offsets[it_Length] + table.Count[it_Length]);
            }

            std::array<std::uint16_t, c_MaximumBits + 1> l_Codes{};
            std::uint32_t l_Code = 0;
            for (std::size_t it_Length = 1; it_Length <= c_MaximumBits; ++it_Length)
            {
                l_Code = (l_Code + (it_Length > 1 ? table.Count[it_Length - 1] : 0u)) << 1;
                l_Codes[it_Length] = static_cast<std::uint16_t>(l_Code);
            }

            for (std::size_t it_Symbol = 0; it_Symbol < lengths.size(); ++it_Symbol)
            {
                const std::uint8_t l_Length = lengths[it_Symbol];
                if (l_Length == 0)
                {
                    continue;
                }

                table.Symbol[l_Offsets[l_Length]++] = static_cast<std::uint16_t>(it_Symbol);
                const std::uint32_t l_SymbolCode = l_Codes[l_Length]++;
                if (l_Length > c_FastBits)
                {
                    continue;
                }

                // Deflate packs a code's bits most significant first into a stream read least significant first
                std::uint32_t l_Reversed = 0;
                for (int it_Bit = 0; it_Bit < l_Length; ++it_Bit)
                {
                    l_Reversed |= ((l_SymbolCode >> it_Bit) & 1u) << (l_Length - 1 - it_Bit);
                }

                for (std::uint32_t it_Index = l_Reversed; it_Index < table.Fast.size(); it_Index += 1u << l_Length)
                {
                    table.Fast[it_Index] = static_cast<std::uint16_t>((it_Symbol << 4) | l_Length);
                }
            }

            return l_Left;
        }

        // Complete, or one code alone of length 1, as zlib's puff allows
        bool IsUsable(const HuffmanTable& table, int left, std::size_t count)
        {
            return left == 0 || (left > 0 && count == static_cast<std::size_t>(table.Count[0]) + table.Count[1]);
        }

        // The next symbol, or -1 for bits that are no code or run past the entry's end
        int Decode(BitReader& bits, const HuffmanTable& table)
        {
            bits.Fill(c_FastBits);
            const std::uint16_t l_Entry = table.Fast[bits.Peek() & ((1u << c_FastBits) - 1)];
            const int l_Length = l_Entry & 15;
            if (l_Entry != 0 && l_Length <= bits.GetCount())
            {
                bits.Drop(l_Length);

                return l_Entry >> 4;
            }

            int l_Code = 0;
            int l_First = 0;
            int l_Index = 0;
            for (std::size_t it_Length = 1; it_Length <= c_MaximumBits; ++it_Length)
            {
                l_Code |= static_cast<int>(bits.Take(1));
                if (bits.IsCutShort())
                {
                    return -1;
                }

                const int l_Count = table.Count[it_Length];
                if (l_Code - l_Count < l_First)
                {
                    return table.Symbol[static_cast<std::size_t>(l_Index + (l_Code - l_First))];
                }

                l_Index += l_Count;
                l_First += l_Count;
                l_First <<= 1;
                l_Code <<= 1;
            }

            return -1;
        }

        enum class InflateResult : std::uint8_t
        {
            Done,
            Damaged,
            CutShort,
            Stopped
        };

        constexpr std::array<std::uint16_t, 29> c_LengthBase{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
        constexpr std::array<std::uint8_t, 29> c_LengthExtra{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
        constexpr std::array<std::uint16_t, 30> c_DistanceBase{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
        constexpr std::array<std::uint8_t, 30> c_DistanceExtra{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
        constexpr std::array<std::uint8_t, 19> c_CodeLengthOrder{ 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

        InflateResult Failed(const BitReader& bits, const InflateOutput& output)
        {
            if (output.IsStopped())
            {
                return InflateResult::Stopped;
            }

            return bits.IsCutShort() ? InflateResult::CutShort : InflateResult::Damaged;
        }

        // A block's literals and the lengths and distances that copy what came before, up to its end code
        InflateResult InflateCodes(BitReader& bits, InflateOutput& output, const HuffmanTable& literals, const HuffmanTable& distances)
        {
            while (true)
            {
                const int l_Symbol = Decode(bits, literals);
                if (l_Symbol < 0)
                {
                    return Failed(bits, output);
                }

                if (l_Symbol < 256)
                {
                    if (!output.Put(static_cast<std::byte>(l_Symbol)))
                    {
                        return InflateResult::Stopped;
                    }

                    continue;
                }

                if (l_Symbol == 256)
                {
                    return InflateResult::Done;
                }

                const std::size_t l_LengthIndex = static_cast<std::size_t>(l_Symbol - 257);
                if (l_LengthIndex >= c_LengthBase.size())
                {
                    return InflateResult::Damaged;
                }

                const std::size_t l_Length = c_LengthBase[l_LengthIndex] + bits.Take(c_LengthExtra[l_LengthIndex]);
                const int l_DistanceSymbol = Decode(bits, distances);
                if (l_DistanceSymbol < 0)
                {
                    return Failed(bits, output);
                }

                if (static_cast<std::size_t>(l_DistanceSymbol) >= c_DistanceBase.size())
                {
                    return InflateResult::Damaged;
                }

                const std::size_t l_Distance = c_DistanceBase[static_cast<std::size_t>(l_DistanceSymbol)] + bits.Take(c_DistanceExtra[static_cast<std::size_t>(l_DistanceSymbol)]);
                if (bits.IsCutShort())
                {
                    return InflateResult::CutShort;
                }

                if (!output.Copy(l_Distance, l_Length))
                {
                    return Failed(bits, output);
                }
            }
        }

        struct FixedTables
        {
            HuffmanTable Literals;
            HuffmanTable Distances;
        };

        // The codes RFC 1951 fixes for blocks of type 1
        const FixedTables& GetFixedTables()
        {
            static const FixedTables s_Tables = []
            {
                FixedTables l_Tables;
                std::array<std::uint8_t, 288> l_Literals{};
                std::fill(l_Literals.begin(), l_Literals.begin() + 144, std::uint8_t{ 8 });
                std::fill(l_Literals.begin() + 144, l_Literals.begin() + 256, std::uint8_t{ 9 });
                std::fill(l_Literals.begin() + 256, l_Literals.begin() + 280, std::uint8_t{ 7 });
                std::fill(l_Literals.begin() + 280, l_Literals.end(), std::uint8_t{ 8 });
                static_cast<void>(BuildTable(l_Tables.Literals, l_Literals));

                std::array<std::uint8_t, 30> l_Distances{};
                l_Distances.fill(5);
                static_cast<void>(BuildTable(l_Tables.Distances, l_Distances));

                return l_Tables;
            }();

            return s_Tables;
        }

        // A block of type 2, which gives its own codes first, as lengths coded with a code of their own
        InflateResult InflateDynamic(BitReader& bits, InflateOutput& output)
        {
            const std::size_t l_LiteralCount = bits.Take(5) + 257;
            const std::size_t l_DistanceCount = bits.Take(5) + 1;
            const std::size_t l_CodeCount = bits.Take(4) + 4;
            if (bits.IsCutShort())
            {
                return InflateResult::CutShort;
            }

            if (l_LiteralCount > 286 || l_DistanceCount > 30)
            {
                return InflateResult::Damaged;
            }

            std::array<std::uint8_t, 286 + 30> l_Lengths{};
            for (std::size_t it_Code = 0; it_Code < l_CodeCount; ++it_Code)
            {
                l_Lengths[c_CodeLengthOrder[it_Code]] = static_cast<std::uint8_t>(bits.Take(3));
            }

            HuffmanTable l_CodeLengths;
            if (BuildTable(l_CodeLengths, std::span<const std::uint8_t>(l_Lengths.data(), c_CodeLengthOrder.size())) != 0)
            {
                return bits.IsCutShort() ? InflateResult::CutShort : InflateResult::Damaged;
            }

            const std::size_t l_Total = l_LiteralCount + l_DistanceCount;
            std::size_t l_Index = 0;
            while (l_Index < l_Total)
            {
                const int l_Symbol = Decode(bits, l_CodeLengths);
                if (l_Symbol < 0)
                {
                    return Failed(bits, output);
                }

                if (l_Symbol < 16)
                {
                    l_Lengths[l_Index++] = static_cast<std::uint8_t>(l_Symbol);

                    continue;
                }

                std::uint8_t l_Repeated = 0;
                std::size_t l_Times = 0;
                if (l_Symbol == 16)
                {
                    if (l_Index == 0)
                    {
                        return InflateResult::Damaged;
                    }

                    l_Repeated = l_Lengths[l_Index - 1];
                    l_Times = 3 + bits.Take(2);
                }
                else if (l_Symbol == 17)
                {
                    l_Times = 3 + bits.Take(3);
                }
                else
                {
                    l_Times = 11 + bits.Take(7);
                }

                if (l_Index + l_Times > l_Total)
                {
                    return InflateResult::Damaged;
                }

                std::fill_n(l_Lengths.begin() + static_cast<std::ptrdiff_t>(l_Index), l_Times, l_Repeated);
                l_Index += l_Times;
            }

            if (bits.IsCutShort())
            {
                return InflateResult::CutShort;
            }

            // A block with no end code could never end
            if (l_Lengths[256] == 0)
            {
                return InflateResult::Damaged;
            }

            HuffmanTable l_Literals;
            const int l_LiteralsLeft = BuildTable(l_Literals, std::span<const std::uint8_t>(l_Lengths.data(), l_LiteralCount));
            HuffmanTable l_Distances;
            const int l_DistancesLeft = BuildTable(l_Distances, std::span<const std::uint8_t>(l_Lengths.data() + l_LiteralCount, l_DistanceCount));
            if (!IsUsable(l_Literals, l_LiteralsLeft, l_LiteralCount) || !IsUsable(l_Distances, l_DistancesLeft, l_DistanceCount))
            {
                return InflateResult::Damaged;
            }

            return InflateCodes(bits, output, l_Literals, l_Distances);
        }

        // RFC 1951's blocks one after another, up to the one marked last
        InflateResult Inflate(BitReader& bits, InflateOutput& output)
        {
            bool l_Last = false;
            while (!l_Last)
            {
                l_Last = bits.Take(1) != 0;
                const std::uint32_t l_Type = bits.Take(2);
                if (bits.IsCutShort())
                {
                    return InflateResult::CutShort;
                }

                InflateResult l_Result = InflateResult::Damaged;
                switch (l_Type)
                {
                    case 0:
                    {
                        bits.AlignToByte();
                        const std::uint32_t l_Length = bits.Take(16);
                        const std::uint32_t l_Complement = bits.Take(16);
                        if (bits.IsCutShort())
                        {
                            return InflateResult::CutShort;
                        }

                        if (l_Length != (~l_Complement & 0xFFFFu))
                        {
                            return InflateResult::Damaged;
                        }

                        l_Result = InflateResult::Done;
                        for (std::uint32_t it_Byte = 0; it_Byte < l_Length && l_Result == InflateResult::Done; ++it_Byte)
                        {
                            const std::uint32_t l_Value = bits.Take(8);
                            if (bits.IsCutShort())
                            {
                                l_Result = InflateResult::CutShort;
                            }
                            else if (!output.Put(static_cast<std::byte>(l_Value)))
                            {
                                l_Result = InflateResult::Stopped;
                            }
                        }

                        break;
                    }
                    case 1:
                    {
                        const FixedTables& l_Tables = GetFixedTables();
                        l_Result = InflateCodes(bits, output, l_Tables.Literals, l_Tables.Distances);

                        break;
                    }
                    case 2:
                    {
                        l_Result = InflateDynamic(bits, output);

                        break;
                    }
                    default:
                    {
                        break;
                    }
                }

                if (l_Result != InflateResult::Done)
                {
                    return l_Result;
                }
            }

            return output.Flush() ? InflateResult::Done : InflateResult::Stopped;
        }
    }

    // The end record is found from the end of the file, since the comment before it may hold anything, and the central directory it points to names every entry
    Expected<ZipArchive, std::string> ZipArchive::Open(const std::filesystem::path& path)
    {
        ZipArchive l_Archive;
        l_Archive.m_File.open(path, std::ios::binary);
        if (!l_Archive.m_File)
        {
            return Unexpected{ std::string("it could not be opened") };
        }

        std::error_code l_Error;
        const std::uintmax_t l_FileSize = std::filesystem::file_size(path, l_Error);
        if (l_Error)
        {
            return Unexpected{ std::format("its size could not be read: {}", l_Error.message()) };
        }

        if (l_FileSize < c_EndSize)
        {
            return Unexpected{ std::string("it is too short to be a zip") };
        }

        const std::size_t l_TailSize = static_cast<std::size_t>(std::min<std::uintmax_t>(l_FileSize, c_EndSize + c_MaximumComment));
        const std::uint64_t l_TailStart = l_FileSize - l_TailSize;
        std::vector<std::byte> l_Tail(l_TailSize);
        if (!ReadAt(l_Archive.m_File, l_TailStart, l_Tail))
        {
            return Unexpected{ std::string("it could not be read") };
        }

        std::optional<std::size_t> l_End;
        for (std::size_t it_Offset = l_TailSize - c_EndSize + 1; it_Offset-- > 0;)
        {
            if (Read32(&l_Tail[it_Offset]) == c_EndSignature && it_Offset + c_EndSize + Read16(&l_Tail[it_Offset + 20]) == l_TailSize)
            {
                l_End = it_Offset;

                break;
            }
        }

        if (!l_End)
        {
            return Unexpected{ std::string("it is not a zip, or is cut short, as it has no end of central directory") };
        }

        const std::byte* l_Record = &l_Tail[*l_End];
        const std::uint16_t l_Disk = Read16(l_Record + 4);
        const std::uint16_t l_DirectoryDisk = Read16(l_Record + 6);
        const std::uint16_t l_DiskEntries = Read16(l_Record + 8);
        const std::uint16_t l_EntryCount = Read16(l_Record + 10);
        const std::uint32_t l_DirectorySize = Read32(l_Record + 12);
        const std::uint32_t l_DirectoryOffset = Read32(l_Record + 16);
        const bool l_HasLocator = *l_End >= c_Zip64LocatorSize && Read32(&l_Tail[*l_End - c_Zip64LocatorSize]) == c_Zip64LocatorSignature;
        if (l_HasLocator || l_EntryCount == 0xFFFFu || l_DirectorySize == c_Zip64Marker || l_DirectoryOffset == c_Zip64Marker)
        {
            return Unexpected{ Zip64Refusal() };
        }

        if (l_Disk != 0 || l_DirectoryDisk != 0 || l_DiskEntries != l_EntryCount)
        {
            return Unexpected{ std::string("it is split across several files, and Trinity reads only a zip that is whole") };
        }

        if (static_cast<std::uint64_t>(l_DirectoryOffset) + l_DirectorySize > l_TailStart + *l_End)
        {
            return Unexpected{ std::string("it is cut short, as its central directory runs past where it ends") };
        }

        std::vector<std::byte> l_Directory(l_DirectorySize);
        if (!ReadAt(l_Archive.m_File, l_DirectoryOffset, l_Directory))
        {
            return Unexpected{ std::string("its central directory could not be read") };
        }

        l_Archive.m_DirectoryOffset = l_DirectoryOffset;
        l_Archive.m_Entries.reserve(l_EntryCount);
        std::size_t l_Offset = 0;
        for (std::size_t it_Entry = 0; it_Entry < l_EntryCount; ++it_Entry)
        {
            if (l_Offset + c_CentralHeaderSize > l_Directory.size() || Read32(&l_Directory[l_Offset]) != c_CentralHeaderSignature)
            {
                return Unexpected{ std::format("its central directory is damaged at its entry {}", it_Entry + 1) };
            }

            const std::byte* l_Header = &l_Directory[l_Offset];
            const std::size_t l_NameSize = Read16(l_Header + 28);
            const std::size_t l_RecordSize = c_CentralHeaderSize + l_NameSize + Read16(l_Header + 30) + Read16(l_Header + 32);
            if (l_Offset + l_RecordSize > l_Directory.size())
            {
                return Unexpected{ std::format("its central directory is damaged at its entry {}", it_Entry + 1) };
            }

            ZipEntry l_Entry;
            l_Entry.Name.assign(reinterpret_cast<const char*>(l_Header + c_CentralHeaderSize), l_NameSize);
            l_Entry.Method = Read16(l_Header + 10);
            l_Entry.Crc = Read32(l_Header + 16);
            const std::uint32_t l_CompressedSize = Read32(l_Header + 20);
            const std::uint32_t l_Size = Read32(l_Header + 24);
            const std::uint32_t l_LocalHeaderOffset = Read32(l_Header + 42);
            if (l_CompressedSize == c_Zip64Marker || l_Size == c_Zip64Marker || l_LocalHeaderOffset == c_Zip64Marker)
            {
                return Unexpected{ Zip64Refusal() };
            }

            l_Entry.CompressedSize = l_CompressedSize;
            l_Entry.Size = l_Size;
            l_Entry.LocalHeaderOffset = l_LocalHeaderOffset;
            if (std::to_integer<std::uint8_t>(l_Header[5]) == c_UnixHost)
            {
                l_Entry.UnixMode = Read32(l_Header + 38) >> 16;
            }

            if ((Read16(l_Header + 8) & c_EncryptedFlag) != 0)
            {
                return Unexpected{ std::format("{} in it is encrypted, and Trinity reads no encrypted zip", l_Entry.Name) };
            }

            if (l_Entry.Method != c_Stored && l_Entry.Method != c_Deflated)
            {
                return Unexpected{ std::format("{} in it is compressed with method {}, and Trinity reads only stored and deflated files", l_Entry.Name, l_Entry.Method) };
            }

            if (l_Entry.Method == c_Stored && l_Entry.CompressedSize != l_Entry.Size)
            {
                return Unexpected{ std::format("{} in it is damaged, as it is stored, and its two sizes differ", l_Entry.Name) };
            }

            l_Archive.m_Entries.push_back(std::move(l_Entry));
            l_Offset += l_RecordSize;
        }

        return l_Archive;
    }

    const ZipEntry* ZipArchive::Find(std::string_view name) const
    {
        const auto a_Entry = std::ranges::find(m_Entries, name, &ZipEntry::Name);

        return a_Entry != m_Entries.end() ? &*a_Entry : nullptr;
    }

    // The local header gives the lengths of its own name and extra field, which can differ from the central directory's, and the bytes follow them
    Expected<void, std::string> ZipArchive::Extract(const ZipEntry& entry, const Sink& sink)
    {
        std::array<std::byte, c_LocalHeaderSize> l_Header{};
        if (!ReadAt(m_File, entry.LocalHeaderOffset, l_Header) || Read32(l_Header.data()) != c_LocalHeaderSignature)
        {
            return Unexpected{ std::format("{} is damaged, as its local header is missing", entry.Name) };
        }

        const std::uint64_t l_Start = entry.LocalHeaderOffset + c_LocalHeaderSize + Read16(&l_Header[26]) + Read16(&l_Header[28]);
        if (l_Start + entry.CompressedSize > m_DirectoryOffset)
        {
            return Unexpected{ std::format("{} is cut short, as it runs past where the zip's files end", entry.Name) };
        }

        m_File.clear();
        m_File.seekg(static_cast<std::streamoff>(l_Start));

        std::uint64_t l_Written = 0;
        std::uint32_t l_Crc = 0;
        if (entry.Method == c_Stored)
        {
            std::vector<std::byte> l_Buffer(static_cast<std::size_t>(std::min<std::uint64_t>(entry.Size, c_ReadSize)));
            while (l_Written < entry.Size)
            {
                const std::size_t l_Size = static_cast<std::size_t>(std::min<std::uint64_t>(entry.Size - l_Written, l_Buffer.size()));
                m_File.read(reinterpret_cast<char*>(l_Buffer.data()), static_cast<std::streamsize>(l_Size));
                if (m_File.gcount() != static_cast<std::streamsize>(l_Size))
                {
                    return Unexpected{ std::format("{} could not be read", entry.Name) };
                }

                const std::span<const std::byte> l_Piece(l_Buffer.data(), l_Size);
                l_Crc = Hash::Crc32(l_Piece, l_Crc);
                l_Written += l_Size;
                if (!sink(l_Piece))
                {
                    return Unexpected{ std::format("{} was stopped before its end", entry.Name) };
                }
            }
        }
        else
        {
            BitReader l_Bits(m_File, entry.CompressedSize);
            InflateOutput l_Output(sink);
            switch (Inflate(l_Bits, l_Output))
            {
                case InflateResult::Done:
                {
                    break;
                }
                case InflateResult::Stopped:
                {
                    return Unexpected{ std::format("{} was stopped before its end", entry.Name) };
                }
                case InflateResult::CutShort:
                {
                    return Unexpected{ l_Bits.HasReadFailed() ? std::format("{} could not be read", entry.Name) : std::format("{} is damaged, as its deflated bytes end too soon", entry.Name) };
                }
                case InflateResult::Damaged:
                {
                    return Unexpected{ std::format("{} is damaged, as its deflated bytes are not deflate's", entry.Name) };
                }
            }

            l_Written = l_Output.GetWritten();
            l_Crc = l_Output.GetCrc();
        }

        if (l_Written != entry.Size)
        {
            return Unexpected{ std::format("{} is damaged, as it unpacks to {} bytes, and the zip gives {}", entry.Name, l_Written, entry.Size) };
        }

        if (l_Crc != entry.Crc)
        {
            return Unexpected{ std::format("{} is damaged, as its CRC-32 is {:08x}, and the zip gives {:08x}", entry.Name, l_Crc, entry.Crc) };
        }

        return {};
    }

    Expected<std::string, std::string> ZipArchive::ReadText(const ZipEntry& entry, std::size_t limit)
    {
        if (entry.Size > limit)
        {
            return Unexpected{ std::format("{} is {} bytes, more than the {} it may be", entry.Name, entry.Size, limit) };
        }

        std::string l_Text;
        l_Text.reserve(static_cast<std::size_t>(entry.Size));
        const Expected<void, std::string> l_Extracted = Extract(entry, [&l_Text, limit](std::span<const std::byte> piece)
        {
            if (l_Text.size() + piece.size() > limit)
            {
                return false;
            }

            l_Text.append(reinterpret_cast<const char*>(piece.data()), piece.size());

            return true;
        });

        if (!l_Extracted)
        {
            return Unexpected{ l_Extracted.GetError() };
        }

        return l_Text;
    }
}