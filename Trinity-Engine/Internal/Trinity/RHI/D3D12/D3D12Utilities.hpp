#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/Types.hpp"

#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        [[nodiscard]] DXGI_FORMAT ToDXGIFormat(Format format);
        [[nodiscard]] std::string FormatResult(HRESULT result);
        [[nodiscard]] std::wstring ToWide(std::string_view text);
        // Names a resource for the debug layer and PIX, when there is a name
        void SetDebugName(ID3D12Resource* resource, std::string_view name);
    }
}