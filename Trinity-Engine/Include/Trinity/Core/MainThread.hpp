#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstddef>
#include <functional>

namespace Trinity
{
    namespace MainThread
    {
        using Task = std::move_only_function<void()>;

        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();

        // Safe from any thread.
        TRINITY_API void Post(Task task);

        TRINITY_API std::size_t ExecutePending();

        [[nodiscard]] TRINITY_API bool IsMainThread();
    }
}