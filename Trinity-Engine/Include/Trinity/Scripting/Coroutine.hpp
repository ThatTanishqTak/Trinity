#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"

#include <concepts>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace Trinity
{
    // What a timer or a coroutine waits for before it runs again, all on the running scene's clock, so they hold while the scene is paused and move on with Step. Seconds count in fixed steps: a timer runs in the first step that reaches its time, and a coroutine after OnUpdate in the frame that step was in. Frames count frames, the next one for 1, a coroutine waiting for the next fixed step runs in it after the scripts' OnFixedUpdate and ahead of physics, and one waiting on a condition asks it after each OnUpdate until it holds. Done for one that has finished
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

    // How the runtime runs a timer or a coroutine, which is opaque to it. Resume runs it until it next waits, written into the wait, or Done once it has finished, and is false when it failed, logged by its language, which stops its script. Check asks a condition its language keeps, and Free lets it go, whether it finished or was stopped
    struct ScriptTaskCalls
    {
        bool (*Resume)(void* task, ScriptWait& next) = nullptr;
        bool (*Check)(void* task, bool& met) = nullptr;
        void (*Free)(void* task) = nullptr;
    };

    // A timer or a coroutine a script started, which stays valid to stop or ask about after it has gone. Empty for one that was never started, or that finished as it started
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

    // What a C++ coroutine returns, which Script::StartCoroutine runs at once until it first waits, and the runtime resumes as each wait is met. It waits with co_await on WaitForSeconds, WaitForFrames, WaitForNextFrame, WaitForFixedUpdate, WaitUntil or WaitWhile, and nothing else. Its frame is allocated under the Scripting tag. A module is not exception-safe, so an exception that leaves it ends the process. One never started is freed with this
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
        // Each wait tells the coroutine's promise what it waits for as it suspends, and needs nothing back as it resumes
        struct WaitAwaiter
        {
            ScriptWait Wait;

            [[nodiscard]] bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) const noexcept { handle.promise().Wait = Wait; }
            void await_resume() const noexcept {}
        };

        // How the runtime runs a C++ coroutine, which lives in the engine, so the calls outlive a module whose coroutines all went before it unloaded
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

            static constexpr ScriptTaskCalls c_Calls{ &Resume, nullptr, &Free };
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

    // Until the condition holds, asked after each OnUpdate from the next frame on. It lives in the coroutine's frame while it waits, so asking it allocates nothing
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