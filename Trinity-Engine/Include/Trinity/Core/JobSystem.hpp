#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace Trinity
{
    struct JobCounterAccess;

    // Counts submitted jobs that have not finished yet. It must outlive every job submitted with it
    class JobCounter
    {
    public:
        JobCounter() = default;
        ~JobCounter();

        JobCounter(const JobCounter&) = delete;
        JobCounter& operator=(const JobCounter&) = delete;

        [[nodiscard]] bool IsDone() const { return m_Pending.load(std::memory_order_acquire) == 0; }
        [[nodiscard]] std::uint32_t GetPending() const { return m_Pending.load(std::memory_order_acquire); }

    private:
        friend struct JobCounterAccess;

        std::atomic<std::uint32_t> m_Pending{ 0 };
    };

    // A move-only callable stored inline, so submitting a job never allocates
    class Job
    {
    public:
        static constexpr std::size_t c_StorageSize = 48;

        Job() = default;

        template<typename F>
            requires (!std::same_as<std::remove_cvref_t<F>, Job>) && std::invocable<std::decay_t<F>&>
        Job(F&& function)
        {
            using Function = std::decay_t<F>;
            static_assert(sizeof(Function) <= c_StorageSize, "Job captures are limited to Job::c_StorageSize bytes: capture a pointer or reference to your data instead.");
            static_assert(alignof(Function) <= alignof(std::max_align_t), "Job captures cannot be over-aligned.");
            static_assert(std::is_nothrow_move_constructible_v<Function>, "Job captures must be nothrow move constructible.");

            ::new (static_cast<void*>(m_Storage)) Function(std::forward<F>(function));
            m_Invoke = [](void* storage) { (*static_cast<Function*>(storage))(); };
            m_Relocate = [](void* destination, void* source) noexcept
            {
                Function* l_Source = static_cast<Function*>(source);
                if (destination != nullptr)
                {
                    ::new (destination) Function(std::move(*l_Source));
                }
                l_Source->~Function();
            };
        }

        Job(Job&& other) noexcept
        {
            MoveFrom(other);
        }

        Job& operator=(Job&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                MoveFrom(other);
            }

            return *this;
        }

        ~Job()
        {
            Reset();
        }

        void operator()() { m_Invoke(m_Storage); }

        explicit operator bool() const { return m_Invoke != nullptr; }

    private:
        void MoveFrom(Job& other) noexcept
        {
            if (other.m_Relocate != nullptr)
            {
                other.m_Relocate(m_Storage, other.m_Storage);
            }

            m_Invoke = std::exchange(other.m_Invoke, nullptr);
            m_Relocate = std::exchange(other.m_Relocate, nullptr);
        }

        void Reset() noexcept
        {
            if (m_Relocate != nullptr)
            {
                m_Relocate(nullptr, m_Storage);
            }

            m_Invoke = nullptr;
            m_Relocate = nullptr;
        }

        alignas(std::max_align_t) std::byte m_Storage[c_StorageSize];
        void (*m_Invoke)(void*) = nullptr;
        void (*m_Relocate)(void*, void*) noexcept = nullptr;
    };

    namespace JobSystem
    {
        void Initialize();
        void Shutdown();

        void Submit(Job job, JobCounter* counter = nullptr);
        void Wait(const JobCounter& counter);

        using RangeFunction = void (*)(const void* context, std::size_t begin, std::size_t end);
        void ParallelForRanges(std::size_t count, RangeFunction function, const void* context, std::size_t batchSize = 0);

        template<typename F>
        void ParallelFor(std::size_t count, const F& body, std::size_t batchSize = 0)
        {
            static_assert(std::invocable<const F&, std::size_t, std::size_t>, "ParallelFor runs one body on several threads at once, so it must be callable as const: remove 'mutable' and keep shared results in atomics or per-batch locals");

            ParallelForRanges(count, [](const void* context, std::size_t begin, std::size_t end) { (*static_cast<const F*>(context))(begin, end); }, std::addressof(body), batchSize);
        }

        [[nodiscard]] std::uint32_t GetWorkerCount();
        [[nodiscard]] std::uint32_t GetThreadIndex();
    }
}