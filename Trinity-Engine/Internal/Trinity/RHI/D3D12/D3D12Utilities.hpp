#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/Types.hpp"

#include <string>

namespace Trinity
{
    namespace RHI
    {
        [[nodiscard]] DXGI_FORMAT ToDXGIFormat(Format format);
        [[nodiscard]] std::string FormatResult(HRESULT result);
    }
}