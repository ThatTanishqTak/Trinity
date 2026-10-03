#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/RHI/Types.hpp"

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        struct SwapChainSpecification
        {
            void* NativeWindow = nullptr;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            Format ImageFormat = Format::BGRA8Unorm;
            bool VSync = true;
        };

        class TRINITY_API SwapChain
        {
        public:
            virtual ~SwapChain() = default;

            SwapChain(const SwapChain&) = delete;
            SwapChain& operator=(const SwapChain&) = delete;

            [[nodiscard]] virtual TextureHandle AcquireNextTexture() = 0;
            virtual void Present() = 0;

            virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
            virtual void SetVSync(bool enabled) = 0;

            [[nodiscard]] virtual Format GetFormat() const = 0;
            [[nodiscard]] virtual std::uint32_t GetWidth() const = 0;
            [[nodiscard]] virtual std::uint32_t GetHeight() const = 0;

        protected:
            SwapChain() = default;
        };
    }
}