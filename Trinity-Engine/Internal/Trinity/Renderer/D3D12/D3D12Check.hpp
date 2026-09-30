#pragma once

#include "Trinity/Core/Assert.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include <string>

namespace Trinity
{
	std::string HResultToString(HRESULT result);

	bool EnableD3D12DebugLayer();

	class D3D12DebugMessages
	{
	public:
		D3D12DebugMessages() = default;
		~D3D12DebugMessages();

		D3D12DebugMessages(const D3D12DebugMessages&) = delete;
		D3D12DebugMessages& operator=(const D3D12DebugMessages&) = delete;
		D3D12DebugMessages(D3D12DebugMessages&&) = delete;
		D3D12DebugMessages& operator=(D3D12DebugMessages&&) = delete;

		void Attach(ID3D12Device* device);
		void Poll();
		void Detach();

	private:
		Microsoft::WRL::ComPtr<ID3D12InfoQueue> m_InfoQueue;
		DWORD m_CallbackCookie = 0;
	};

	namespace D3D12
	{
		bool CheckHResult(HRESULT result, const char* expression, const char* file, int line);
	}
}

#if defined(TR_ENABLE_ASSERTS)
#define TR_HR_CHECK(expression) (::Trinity::D3D12::CheckHResult((expression), #expression, __FILE__, __LINE__) || (TR_DEBUGBREAK(), false))
#else
#define TR_HR_CHECK(expression) ::Trinity::D3D12::CheckHResult((expression), #expression, __FILE__, __LINE__)
#endif