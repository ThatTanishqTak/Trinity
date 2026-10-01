#pragma once

namespace Trinity
{
    namespace Platform
    {
        void Initialize();
        void Shutdown();

        [[nodiscard]] const char* GetName();
    }
}