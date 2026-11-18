#pragma once

// The Agility SDK's headers from DirectX-Headers, ahead of the older copies in the Windows SDK
#include "Trinity/Platform/Windows/WindowsHeaders.hpp"

#include <directx/d3d12.h>
#include <directx/d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

// MSVC finds interface IDs through __uuidof; other compilers, such as MinGW, need them spelled out
#if !defined(_MSC_VER)
#include <dxguids/dxguids.h>
#endif

// D3D12MemoryAllocator on the headers above, rather than including the D3D12 headers again itself
#define D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED
#include <D3D12MemAlloc.h>