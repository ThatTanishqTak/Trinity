#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/PipelineCacheFile.hpp"

#include <mutex>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;
        class D3D12Factory;

        // The pipeline library, started from its file and saved to it at exit once a pipeline state was added, which every pipeline state is loaded from by its name, or else made and stored in under it
        class D3D12PipelineLibrary
        {
        public:
            D3D12PipelineLibrary(const D3D12Factory& factory, const D3D12Device& device);
            ~D3D12PipelineLibrary();

            D3D12PipelineLibrary(const D3D12PipelineLibrary&) = delete;
            D3D12PipelineLibrary& operator=(const D3D12PipelineLibrary&) = delete;

            // Starts from the file at path, or empty when there is none, it was made by another adapter, driver or engine version, or it is damaged. Without a path the library lasts as long as the device, and a runtime without pipeline libraries makes every pipeline state without one. False, with the reason in error, when the library cannot be created
            [[nodiscard]] bool Initialize(std::string_view path, std::string& error);
            // Saves the library when a pipeline state was added to it, then releases it, before the file's data it reads from. Does nothing a second time
            void Destroy();

            // The pipeline state stored under name, or else one made from the description and stored under it, counted either way
            [[nodiscard]] HRESULT CreateGraphics(const std::wstring& name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& description, Microsoft::WRL::ComPtr<ID3D12PipelineState>& state);
            [[nodiscard]] HRESULT CreateCompute(const std::wstring& name, const D3D12_COMPUTE_PIPELINE_STATE_DESC& description, Microsoft::WRL::ComPtr<ID3D12PipelineState>& state);

        private:
            template<typename Load, typename Make>
            [[nodiscard]] HRESULT Create(const std::wstring& name, Microsoft::WRL::ComPtr<ID3D12PipelineState>& state, Load load, Make make);

            const D3D12Factory& m_Factory;
            const D3D12Device& m_Device;

            PipelineCacheFile m_File;
            Microsoft::WRL::ComPtr<ID3D12PipelineLibrary1> m_Library;
            // One thread at a time loads a pipeline state, as the library asks, and stores one, so two never store the same name
            std::mutex m_Mutex;
        };
    }
}