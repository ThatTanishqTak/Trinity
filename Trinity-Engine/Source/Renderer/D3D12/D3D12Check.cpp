#include "Trinity/Renderer/D3D12/D3D12Check.hpp"

#include "Trinity/Core/Log.hpp"

#include <dxgi.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>
#include <vector>

namespace Trinity
{
	namespace
	{
		std::string_view GetHResultName(HRESULT result)
		{
			switch (result)
			{
				case S_OK:
				{
					return "S_OK";
				}
				case S_FALSE:
				{
					return "S_FALSE";
				}
				case E_FAIL:
				{
					return "E_FAIL";
				}
				case E_INVALIDARG:
				{
					return "E_INVALIDARG";
				}
				case E_OUTOFMEMORY:
				{
					return "E_OUTOFMEMORY";
				}
				case E_NOTIMPL:
				{
					return "E_NOTIMPL";
				}
				case E_NOINTERFACE:
				{
					return "E_NOINTERFACE";
				}
				case E_POINTER:
				{
					return "E_POINTER";
				}
				case E_ACCESSDENIED:
				{
					return "E_ACCESSDENIED";
				}
				case DXGI_ERROR_DEVICE_HUNG:
				{
					return "DXGI_ERROR_DEVICE_HUNG";
				}
				case DXGI_ERROR_DEVICE_REMOVED:
				{
					return "DXGI_ERROR_DEVICE_REMOVED";
				}
				case DXGI_ERROR_DEVICE_RESET:
				{
					return "DXGI_ERROR_DEVICE_RESET";
				}
				case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
				{
					return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
				}
				case DXGI_ERROR_INVALID_CALL:
				{
					return "DXGI_ERROR_INVALID_CALL";
				}
				case DXGI_ERROR_NOT_FOUND:
				{
					return "DXGI_ERROR_NOT_FOUND";
				}
				case DXGI_ERROR_UNSUPPORTED:
				{
					return "DXGI_ERROR_UNSUPPORTED";
				}
				case DXGI_ERROR_WAS_STILL_DRAWING:
				{
					return "DXGI_ERROR_WAS_STILL_DRAWING";
				}
				case DXGI_ERROR_SDK_COMPONENT_MISSING:
				{
					return "DXGI_ERROR_SDK_COMPONENT_MISSING";
				}
				default:
				{
					return "HRESULT";
				}
			}
		}

		std::string GetSystemMessage(HRESULT result)
		{
			char* l_Buffer = nullptr;
			const DWORD l_Length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr, static_cast<DWORD>(result), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&l_Buffer), 0, nullptr);

			std::string l_Message = l_Length > 0 && l_Buffer ? std::string(l_Buffer, l_Length) : std::string{};
			LocalFree(l_Buffer);

			while (!l_Message.empty() && (l_Message.back() == '\n' || l_Message.back() == '\r' || l_Message.back() == ' '))
			{
				l_Message.pop_back();
			}

			return l_Message;
		}

		std::string_view GetCategoryName(D3D12_MESSAGE_CATEGORY category)
		{
			switch (category)
			{
				case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED:
				{
					return "Application";
				}
				case D3D12_MESSAGE_CATEGORY_INITIALIZATION:
				{
					return "Initialization";
				}
				case D3D12_MESSAGE_CATEGORY_CLEANUP:
				{
					return "Cleanup";
				}
				case D3D12_MESSAGE_CATEGORY_COMPILATION:
				{
					return "Compilation";
				}
				case D3D12_MESSAGE_CATEGORY_STATE_CREATION:
				{
					return "State creation";
				}
				case D3D12_MESSAGE_CATEGORY_STATE_SETTING:
				{
					return "State setting";
				}
				case D3D12_MESSAGE_CATEGORY_STATE_GETTING:
				{
					return "State getting";
				}
				case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION:
				{
					return "Resource manipulation";
				}
				case D3D12_MESSAGE_CATEGORY_EXECUTION:
				{
					return "Execution";
				}
				case D3D12_MESSAGE_CATEGORY_SHADER:
				{
					return "Shader";
				}
				default:
				{
					return "Miscellaneous";
				}
			}
		}

		bool LogDebugMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, const char* description)
		{
			const std::string_view l_Category = GetCategoryName(category);
			const char* l_Description = description ? description : "(no description)";
			const int l_Id = static_cast<int>(id);

			switch (severity)
			{
				case D3D12_MESSAGE_SEVERITY_CORRUPTION:
				case D3D12_MESSAGE_SEVERITY_ERROR:
				{
					TR_CORE_ERROR("[D3D12 {}] {} (ID {})", l_Category, l_Description, l_Id);

					return true;
				}
				case D3D12_MESSAGE_SEVERITY_WARNING:
				{
					TR_CORE_WARN("[D3D12 {}] {} (ID {})", l_Category, l_Description, l_Id);

					return false;
				}
				case D3D12_MESSAGE_SEVERITY_INFO:
				{
					TR_CORE_INFO("[D3D12 {}] {} (ID {})", l_Category, l_Description, l_Id);

					return false;
				}
				default:
				{
					TR_CORE_TRACE("[D3D12 {}] {} (ID {})", l_Category, l_Description, l_Id);

					return false;
				}
			}
		}

#if defined(__ID3D12InfoQueue1_INTERFACE_DEFINED__)
		void __stdcall OnD3D12DebugMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* /*context*/)
		{
			const bool l_IsError = LogDebugMessage(category, severity, id, description);

#if defined(TR_ENABLE_ASSERTS)
			if (l_IsError)
			{
				TR_DEBUGBREAK();
			}
#else
			(void)l_IsError;
#endif
		}
#endif
	}

	std::string HResultToString(HRESULT result)
	{
		const std::string l_SystemMessage = GetSystemMessage(result);
		const std::string l_Code = std::format("{} (0x{:08X})", GetHResultName(result), static_cast<uint32_t>(result));

		return l_SystemMessage.empty() ? l_Code : std::format("{}: {}", l_Code, l_SystemMessage);
	}

	bool EnableD3D12DebugLayer()
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> l_Debug;
		if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(l_Debug.GetAddressOf()))))
		{
			TR_CORE_WARN("D3D12 debug layer unavailable. Install the Graphics Tools optional feature in Windows Settings");

			return false;
		}

		l_Debug->EnableDebugLayer();
		TR_CORE_INFO("D3D12 debug layer enabled");

		return true;
	}

	D3D12DebugMessages::~D3D12DebugMessages()
	{
		Detach();
	}

	void D3D12DebugMessages::Attach(ID3D12Device* device)
	{
		Detach();

		if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(m_InfoQueue.GetAddressOf()))))
		{
			TR_CORE_WARN("D3D12 debug messages unavailable: enable the debug layer before creating the device");

			return;
		}

#if defined(__ID3D12InfoQueue1_INTERFACE_DEFINED__)
		Microsoft::WRL::ComPtr<ID3D12InfoQueue1> l_InfoQueue1;
		if (SUCCEEDED(m_InfoQueue.As(&l_InfoQueue1)) && SUCCEEDED(l_InfoQueue1->RegisterMessageCallback(&OnD3D12DebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &m_CallbackCookie)))
		{
			TR_CORE_INFO("D3D12 debug messages go to the log as they happen");

			return;
		}

		m_CallbackCookie = 0;
#endif

#if defined(TR_ENABLE_ASSERTS)
		m_InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		m_InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
#endif

		TR_CORE_INFO("D3D12 debug messages go to the log once per frame (ID3D12InfoQueue1 unavailable)");
	}

	void D3D12DebugMessages::Poll()
	{
		if (!m_InfoQueue || m_CallbackCookie != 0)
		{
			return;
		}

		const UINT64 l_MessageCount = m_InfoQueue->GetNumStoredMessages();
		std::vector<std::byte> l_Buffer;

		for (UINT64 l_Index = 0; l_Index < l_MessageCount; ++l_Index)
		{
			SIZE_T l_Size = 0;
			if (FAILED(m_InfoQueue->GetMessage(l_Index, nullptr, &l_Size)) || l_Size == 0)
			{
				continue;
			}

			l_Buffer.resize(l_Size);
			D3D12_MESSAGE* l_Message = reinterpret_cast<D3D12_MESSAGE*>(l_Buffer.data());

			if (SUCCEEDED(m_InfoQueue->GetMessage(l_Index, l_Message, &l_Size)))
			{
				LogDebugMessage(l_Message->Category, l_Message->Severity, l_Message->ID, l_Message->pDescription);
			}
		}

		m_InfoQueue->ClearStoredMessages();
	}

	void D3D12DebugMessages::Detach()
	{
		if (!m_InfoQueue)
		{
			return;
		}

		Poll();

#if defined(__ID3D12InfoQueue1_INTERFACE_DEFINED__)
		if (m_CallbackCookie != 0)
		{
			Microsoft::WRL::ComPtr<ID3D12InfoQueue1> l_InfoQueue1;
			if (SUCCEEDED(m_InfoQueue.As(&l_InfoQueue1)))
			{
				l_InfoQueue1->UnregisterMessageCallback(m_CallbackCookie);
			}

			m_CallbackCookie = 0;
		}
#endif

		m_InfoQueue.Reset();
	}

	namespace D3D12
	{
		bool CheckHResult(HRESULT result, const char* expression, const char* file, int line)
		{
			if (SUCCEEDED(result))
			{
				return true;
			}

			TR_CORE_ERROR("{} failed with {} ({}:{})", expression, HResultToString(result), file, line);

			return false;
		}
	}
}