#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/RHI/SwapChain.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        class VulkanSwapChain final : public SwapChain
        {
        public:
            // Null, with the reason logged, when the window cannot be presented to
            [[nodiscard]] static Scope<VulkanSwapChain> Create(VulkanDevice& device, const SwapChainSpecification& specification);

            VulkanSwapChain(VulkanDevice& device, const SwapChainSpecification& specification);
            ~VulkanSwapChain() override;

            [[nodiscard]] TextureHandle AcquireNextTexture() override;
            void Present() override;

            void Resize(std::uint32_t width, std::uint32_t height) override;
            void SetVSync(bool enabled) override;

            [[nodiscard]] Format GetFormat() const override { return m_Format; }
            [[nodiscard]] std::uint32_t GetWidth() const override { return m_Width; }
            [[nodiscard]] std::uint32_t GetHeight() const override { return m_Height; }

        private:
            [[nodiscard]] bool Initialize();
            [[nodiscard]] bool CreateSwapChain();
            bool Recreate();
            void RemoveImages();
            void Report(VkResult result, const char* operation);

            VulkanDevice& m_Device;
            SwapChainSpecification m_Specification;

            VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
            VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
            Format m_Format = Format::Unknown;
            VkPresentModeKHR m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
            std::uint32_t m_Width = 0;
            std::uint32_t m_Height = 0;

            std::vector<TextureHandle> m_Textures;
            std::vector<VkSemaphore> m_PresentSemaphores;
            std::array<VkSemaphore, c_FramesInFlight> m_AcquireSemaphores{};
            std::uint32_t m_ImageIndex = 0;
            bool m_Acquired = false;
            bool m_OutOfDate = false;
            bool m_ReportedFailure = false;
        };
    }
}