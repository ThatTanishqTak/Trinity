#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        // Keeps what a device destroyed until no frame in flight can still use it
        template<typename T>
        class ReleaseQueue
        {
        public:
            void Push(T value)
            {
                m_Entries.push_back({ std::move(value), m_FrameNumber });
            }

            // Call once the frame c_FramesInFlight before this one has finished on the GPU
            template<typename Release>
            void BeginFrame(Release&& release)
            {
                if (m_FrameNumber >= c_FramesInFlight)
                {
                    ReleaseBefore(m_FrameNumber + 1 - c_FramesInFlight, release);
                }

                m_InFrame = true;
            }

            void EndFrame()
            {
                m_InFrame = false;
                ++m_FrameNumber;
            }

            // Call once the GPU is idle, what was destroyed during the frame being recorded stays, since its commands are not submitted yet
            template<typename Release>
            void ReleaseIdle(Release&& release)
            {
                ReleaseBefore(m_InFrame ? m_FrameNumber : m_FrameNumber + 1, release);
            }

            template<typename Release>
            void ReleaseAll(Release&& release)
            {
                for (Entry& it_Entry : m_Entries)
                {
                    release(it_Entry.Value);
                }

                m_Entries.clear();
            }

            [[nodiscard]] std::size_t GetCount() const { return m_Entries.size(); }

        private:
            struct Entry
            {
                T Value{};
                std::uint64_t Frame = 0;
            };

            // Entries are pushed in frame order, so the ones to release are always at the front
            template<typename Release>
            void ReleaseBefore(std::uint64_t frame, Release& release)
            {
                std::size_t l_Count = 0;
                while (l_Count < m_Entries.size() && m_Entries[l_Count].Frame < frame)
                {
                    release(m_Entries[l_Count].Value);
                    ++l_Count;
                }

                m_Entries.erase(m_Entries.begin(), m_Entries.begin() + static_cast<std::ptrdiff_t>(l_Count));
            }

            std::vector<Entry, TaggedAllocator<Entry, MemoryTag::Renderer>> m_Entries;
            std::uint64_t m_FrameNumber = 0;
            bool m_InFrame = false;
        };
    }
}