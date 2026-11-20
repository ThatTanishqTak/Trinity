#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <concepts>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace Trinity
{
    // On the scene's clock, so they hold while it is paused. Seconds count in fixed steps
    enum class ScriptWaitType : std::uint8_t
    {
        Done,
        Seconds,
        Frames,
        FixedStep,
        Condition
    };

    struct ScriptWait
    {
        ScriptWaitType Type = ScriptWaitType::Done;
        float Seconds = 0.0f;
        std::uint32_t Frames = 0;
        // A condition its language asks itself when this is null, as C# does
        bool (*Condition)(const void* context) = nullptr;
        const void* Context = nullptr;
    };

    enum class ScriptTaskKind : std::uint8_t
    {
        Timer,
        Coroutine
    };

    // Resume runs it to its next wait, false if it failed. Check asks a condition. Free lets it go
    struct ScriptTaskCalls
    {
        bool (*Resume)(void* task, ScriptWait& next) = nullptr;
        bool (*Check)(void* task, bool& met) = nullptr;
        void (*Free)(void* task) = nullptr;
        ScriptLanguage Language = ScriptLanguage::Cpp;
    };

    // Stays valid to stop or ask about after it has gone. Empty if never started or finished at once
    struct TimerHandle
    {
        std::uint64_t ID = 0;

        [[nodiscard]] explicit operator bool() const { return ID != 0; }
        [[nodiscard]] bool operator==(const TimerHandle&) const = default;
    };

    struct CoroutineHandle
    {
        std::uint64_t ID = 0;

        [[nodiscard]] explicit operator bool() const { return ID != 0; }
        [[nodiscard]] bool operator==(const CoroutineHandle&) const = default;
    };

    // Waits with co_await on the Wait functions below only. An exception leaving it ends the process
    class Coroutine
    {
    public:
        struct promise_type
        {
            ScriptWait Wait;

            [[nodiscard]] static void* operator new(std::size_t size) { return Memory::Allocate(size, MemoryTag::Scripting); }
            static void operator delete(void* memory) noexcept { Memory::Free(memory); }

            [[nodiscard]] Coroutine get_return_object() { return Coroutine(std::coroutine_handle<promise_type>::from_promise(*this)); }
            [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
            [[nodiscard]] std::suspend_always final_suspend() const noexcept { return {}; }
            void return_void() const noexcept {}
            [[noreturn]] void unhandled_exception() const noexcept { std::terminate(); }
        };

        Coroutine() = default;
        Coroutine(Coroutine&& other) noexcept : m_Handle(std::exchange(other.m_Handle, nullptr)) {}
        Coroutine& operator=(Coroutine&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_Handle = std::exchange(other.m_Handle, nullptr);
            }

            return *this;
        }

        Coroutine(const Coroutine&) = delete;
        Coroutine& operator=(const Coroutine&) = delete;

        ~Coroutine() { Reset(); }

        // The frame, which the caller now frees
        [[nodiscard]] std::coroutine_handle<promise_type> Release() { return std::exchange(m_Handle, nullptr); }

    private:
        explicit Coroutine(std::coroutine_handle<promise_type> handle) : m_Handle(handle) {}

        void Reset()
        {
            if (m_Handle)
            {
                m_Handle.destroy();
                m_Handle = nullptr;
            }
        }

        std::coroutine_handle<promise_type> m_Handle;
    };

    namespace Detail
    {
        // Each wait tells the coroutine's promise what it waits for as it suspends
        struct WaitAwaiter
        {
            ScriptWait Wait;

            [[nodiscard]] bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) const noexcept { handle.promise().Wait = Wait; }
            void await_resume() const noexcept {}
        };

        // In the engine, so the calls outlive a module whose coroutines all went before it unloaded
        [[nodiscard]] TRINITY_API const ScriptTaskCalls& GetCoroutineCalls();

        // A timer's callback, freed under the tag it was made under
        template<typename F>
        struct TimerCallback
        {
            F Callback;

            static bool Resume(void* task, ScriptWait& next)
            {
                static_cast<TimerCallback*>(task)->Callback();
                next = {};

                return true;
            }

            static void Free(void* task)
            {
                Memory::Delete(static_cast<TimerCallback*>(task));
            }

            static constexpr ScriptTaskCalls c_Calls{ &Resume, nullptr, &Free, ScriptLanguage::Cpp };
        };
    }

    // For that many seconds of the scene's fixed steps. 0 waits for the next frame
    [[nodiscard]] inline Detail::WaitAwaiter WaitForSeconds(float seconds)
    {
        return { { .Type = ScriptWaitType::Seconds, .Seconds = seconds } };
    }

    // Until after OnUpdate that many frames on, 1 for the next, and at least 1
    [[nodiscard]] inline Detail::WaitAwaiter WaitForFrames(std::uint32_t frames)
    {
        return { { .Type = ScriptWaitType::Frames, .Frames = frames } };
    }

    [[nodiscard]] inline Detail::WaitAwaiter WaitForNextFrame()
    {
        return WaitForFrames(1);
    }

    // Until the next fixed step, after the scripts' OnFixedUpdate and ahead of physics
    [[nodiscard]] inline Detail::WaitAwaiter WaitForFixedUpdate()
    {
        return { { .Type = ScriptWaitType::FixedStep } };
    }

    // Asked after each OnUpdate from the next frame on. Kept in the coroutine's frame, so no allocation
    template<typename F, bool Holds = true>
        requires std::is_invocable_r_v<bool, const F&>
    struct WaitUntil
    {
        F Condition;

        explicit WaitUntil(F condition) : Condition(std::move(condition)) {}

        [[nodiscard]] bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) const noexcept
        {
            handle.promise().Wait = { .Type = ScriptWaitType::Condition, .Condition = &Ask, .Context = this };
        }

        void await_resume() const noexcept {}

    private:
        static bool Ask(const void* context)
        {
            return static_cast<bool>(static_cast<const WaitUntil*>(context)->Condition()) == Holds;
        }
    };

    // Until the condition no longer holds
    template<typename F>
        requires std::is_invocable_r_v<bool, const F&>
    struct WaitWhile : WaitUntil<F, false>
    {
        explicit WaitWhile(F condition) : WaitUntil<F, false>(std::move(condition)) {}
    };

    template<typename F>
    WaitUntil(F) -> WaitUntil<F>;

    template<typename F>
    WaitWhile(F) -> WaitWhile<F>;
}