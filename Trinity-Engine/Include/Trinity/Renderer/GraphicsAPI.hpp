#pragma once

#include <cstdint>
#include <string_view>

namespace Trinity
{
	enum class GraphicsAPI : uint8_t
	{
		Vulkan,
		DirectX12,
		Metal
	};

	constexpr std::string_view GraphicsAPIToString(GraphicsAPI api)
	{
		switch (api)
		{
			case GraphicsAPI::Vulkan:
			{
				return "Vulkan";
			}
			case GraphicsAPI::DirectX12:
			{
				return "DirectX 12";
			}
			case GraphicsAPI::Metal:
			{
				return "Metal";
			}
		}

		return "Unknown";
	}

	constexpr bool IsGraphicsAPISupported(GraphicsAPI api)
	{
#if defined(_WIN32)
		return api == GraphicsAPI::Vulkan || api == GraphicsAPI::DirectX12;
#elif defined(__APPLE__)
		return api == GraphicsAPI::Vulkan || api == GraphicsAPI::Metal;
#elif defined(__linux__)
		return api == GraphicsAPI::Vulkan;
#else
		return false;
#endif
	}
}