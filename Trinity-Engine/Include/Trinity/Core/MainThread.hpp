#pragma once

#include <cstddef>
#include <functional>

namespace Trinity
{
    // Work handed to the main thread from anywhere
    namespace MainThread
    {
        using Task = std::move_only_function<void()>;

        void Initialize();
        void Shutdown();

        // Safe from any thread.
        void Post(Task task);

        std::size_t ExecutePending();

        [[nodiscard]] bool IsMainThread();
    }
}