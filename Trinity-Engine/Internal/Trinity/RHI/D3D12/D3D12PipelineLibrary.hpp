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

        // Started from its file, and saved to it at exit once a pipeline state was added
        class D3D12PipelineLibrary
        {
        public:
            D3D12PipelineLibrary(const D3D12Factory& factory, const D3D12Device& device);
            ~D3D12PipelineLibrary();

            D3D12PipelineLibrary(const D3D12PipelineLibrary&) = delete;
            D3D12PipelineLibrary& operator=(const D3D12PipelineLibrary&) = delete;

            // A file from another adapter, driver or engine version, or a damaged one, starts it empty
            [[nodiscard]] bool Initialize(std::string_view path, std::string& error);
            // Saves when a pipeline state was added, then releases before the file's data. Once only
            void Destroy();

            // The pipeline state stored under name, or one made and stored under it. Counted either way
            [[nodiscard]] HRESULT CreateGraphics(const std::wstring& name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& description, Microsoft::WRL::ComPtr<ID3D12PipelineState>& state);
            [[nodiscard]] HRESULT CreateCompute(const std::wstring& name, const D3D12_COMPUTE_PIPELINE_STATE_DESC& description, Microsoft::WRL::ComPtr<ID3D12PipelineState>& state);

        private:
            template<typename Load, typename Make>
            [[nodiscard]] HRESULT Create(const std::wstring& name, Microsoft::WRL::ComPtr<ID3D12PipelineState>& state, Load load, Make make);

            const D3D12Factory& m_Factory;
            const D3D12Device& m_Device;

            PipelineCacheFile m_File;
            Microsoft::WRL::ComPtr<ID3D12PipelineLibrary1> m_Library;
            // The library wants one thread at a time, and two must never store the same name
            std::mutex m_Mutex;
        };
    }
}