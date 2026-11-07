#include "Trinity/Core/Platform.hpp"

#include "Trinity/Platform/Windows/WindowsHeaders.hpp"

#include <winhttp.h>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        // A transfer that sends nothing for this long is taken as no answer, as when the network goes away part way
        constexpr int c_ConnectMilliseconds = 15000;
        constexpr int c_StallMilliseconds = 30000;
        constexpr DWORD c_ReadSize = 64 * 1024;

        std::wstring Widen(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int l_Size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring l_Wide(static_cast<std::size_t>(l_Size), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), l_Wide.data(), l_Size);

            return l_Wide;
        }

        std::string Narrow(std::wstring_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int l_Size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            std::string l_Narrow(static_cast<std::size_t>(l_Size), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), l_Narrow.data(), l_Size, nullptr, nullptr);

            return l_Narrow;
        }

        // WinHTTP's own errors have their messages in winhttp.dll, and the system's in the system
        std::string GetHttpErrorMessage(DWORD error)
        {
            const HMODULE l_Module = ::GetModuleHandleW(L"winhttp.dll");
            const DWORD l_Flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | (l_Module != nullptr ? FORMAT_MESSAGE_FROM_HMODULE : 0);
            wchar_t* l_Buffer = nullptr;
            const DWORD l_Length = ::FormatMessageW(l_Flags, l_Module, error, 0, reinterpret_cast<LPWSTR>(&l_Buffer), 0, nullptr);
            std::string l_Message = l_Length > 0 ? Narrow(std::wstring_view(l_Buffer, l_Length)) : std::string();
            if (l_Buffer != nullptr)
            {
                ::LocalFree(l_Buffer);
            }

            while (!l_Message.empty() && (l_Message.back() == '\r' || l_Message.back() == '\n' || l_Message.back() == '.' || l_Message.back() == ' '))
            {
                l_Message.pop_back();
            }

            return l_Message.empty() ? std::format("error {}", error) : std::format("{} (error {})", l_Message, error);
        }

        bool IsUnreachable(DWORD error)
        {
            return error == ERROR_WINHTTP_NAME_NOT_RESOLVED || error == ERROR_WINHTTP_CANNOT_CONNECT || error == ERROR_WINHTTP_TIMEOUT || error == ERROR_WINHTTP_CONNECTION_ERROR;
        }

        Unexpected<HttpError> Fail(DWORD error)
        {
            return Unexpected{ HttpError{ IsUnreachable(error) ? HttpFailure::Unreachable : HttpFailure::Failed, GetHttpErrorMessage(error) } };
        }

        // Closed as it goes out of scope, the request before its connection and the connection before its session
        class HttpHandle
        {
        public:
            explicit HttpHandle(HINTERNET handle) : m_Handle(handle)
            {

            }

            ~HttpHandle()
            {
                if (m_Handle != nullptr)
                {
                    ::WinHttpCloseHandle(m_Handle);
                }
            }

            HttpHandle(const HttpHandle&) = delete;
            HttpHandle& operator=(const HttpHandle&) = delete;

            [[nodiscard]] HINTERNET Get() const { return m_Handle; }
            explicit operator bool() const { return m_Handle != nullptr; }

        private:
            HINTERNET m_Handle = nullptr;
        };

        // The headers as one block, the status line first and each header after it on a line of its own
        std::vector<HttpHeader> ReadHeaders(HINTERNET request)
        {
            std::vector<HttpHeader> l_Headers;
            DWORD l_Size = 0;
            ::WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &l_Size, WINHTTP_NO_HEADER_INDEX);
            if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || l_Size == 0)
            {
                return l_Headers;
            }

            std::wstring l_Text(l_Size / sizeof(wchar_t), L'\0');
            if (!::WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, l_Text.data(), &l_Size, WINHTTP_NO_HEADER_INDEX))
            {
                return l_Headers;
            }

            l_Text.resize(l_Size / sizeof(wchar_t));
            const std::string l_Block = Narrow(l_Text);
            std::string_view l_Rest = l_Block;
            bool l_First = true;
            while (!l_Rest.empty())
            {
                const std::size_t l_End = l_Rest.find("\r\n");
                const std::string_view l_Line = l_Rest.substr(0, l_End);
                l_Rest = l_End == std::string_view::npos ? std::string_view() : l_Rest.substr(l_End + 2);
                if (std::exchange(l_First, false))
                {
                    continue;
                }

                const std::size_t l_Colon = l_Line.find(':');
                if (l_Colon == std::string_view::npos)
                {
                    continue;
                }

                std::string_view l_Value = l_Line.substr(l_Colon + 1);
                while (!l_Value.empty() && (l_Value.front() == ' ' || l_Value.front() == '\t'))
                {
                    l_Value.remove_prefix(1);
                }

                l_Headers.push_back({ std::string(l_Line.substr(0, l_Colon)), std::string(l_Value) });
            }

            return l_Headers;
        }
    }

    namespace Platform
    {
        bool HasHttp()
        {
            return true;
        }

        // Synchronous WinHTTP, whose reads return as soon as anything arrives, so Cancel is read between pieces. Redirects are followed by WinHTTP itself, except from HTTPS to HTTP
        Expected<HttpResponse, HttpError> HttpGet(const HttpRequest& request, const std::function<bool(std::span<const std::byte>)>& sink)
        {
            const std::wstring l_Url = Widen(request.Url);
            URL_COMPONENTS l_Parts{};
            l_Parts.dwStructSize = sizeof(l_Parts);
            l_Parts.dwSchemeLength = static_cast<DWORD>(-1);
            l_Parts.dwHostNameLength = static_cast<DWORD>(-1);
            l_Parts.dwUrlPathLength = static_cast<DWORD>(-1);
            l_Parts.dwExtraInfoLength = static_cast<DWORD>(-1);
            if (!::WinHttpCrackUrl(l_Url.c_str(), static_cast<DWORD>(l_Url.size()), 0, &l_Parts) || (l_Parts.nScheme != INTERNET_SCHEME_HTTP && l_Parts.nScheme != INTERNET_SCHEME_HTTPS))
            {
                return Unexpected{ HttpError{ HttpFailure::Failed, std::format("{} is not an HTTP or HTTPS address", request.Url) } };
            }

            const std::wstring l_Host(l_Parts.lpszHostName, l_Parts.dwHostNameLength);
            const std::wstring l_Path = std::wstring(l_Parts.lpszUrlPath, l_Parts.dwUrlPathLength) + std::wstring(l_Parts.lpszExtraInfo, l_Parts.dwExtraInfoLength);
            const std::wstring l_Agent = Widen(request.UserAgent);

            // The system's proxy as Windows' settings give it, where Windows is 8.1 or later, and WinHTTP's own setting before
            HINTERNET l_Opened = ::WinHttpOpen(l_Agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (l_Opened == nullptr)
            {
                l_Opened = ::WinHttpOpen(l_Agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            }

            const HttpHandle l_Session(l_Opened);
            if (!l_Session)
            {
                return Fail(::GetLastError());
            }

            ::WinHttpSetTimeouts(l_Session.Get(), 0, c_ConnectMilliseconds, c_StallMilliseconds, c_StallMilliseconds);

            const HttpHandle l_Connection(::WinHttpConnect(l_Session.Get(), l_Host.c_str(), l_Parts.nPort, 0));
            if (!l_Connection)
            {
                return Fail(::GetLastError());
            }

            const HttpHandle l_Request(::WinHttpOpenRequest(l_Connection.Get(), L"GET", l_Path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, l_Parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
            if (!l_Request)
            {
                return Fail(::GetLastError());
            }

            std::string l_HeaderText;
            for (const HttpHeader& it_Header : request.Headers)
            {
                l_HeaderText += std::format("{}: {}\r\n", it_Header.Name, it_Header.Value);
            }

            if (!l_HeaderText.empty())
            {
                const std::wstring l_Headers = Widen(l_HeaderText);
                ::WinHttpAddRequestHeaders(l_Request.Get(), l_Headers.c_str(), static_cast<DWORD>(l_Headers.size()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            }

            if (!::WinHttpSendRequest(l_Request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !::WinHttpReceiveResponse(l_Request.Get(), nullptr))
            {
                return Fail(::GetLastError());
            }

            DWORD l_Status = 0;
            DWORD l_StatusSize = sizeof(l_Status);
            ::WinHttpQueryHeaders(l_Request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &l_Status, &l_StatusSize, WINHTTP_NO_HEADER_INDEX);

            HttpResponse l_Response{ static_cast<int>(l_Status), ReadHeaders(l_Request.Get()),{} };
            std::vector<std::byte> l_Buffer(c_ReadSize);
            while (true)
            {
                if (request.Cancel != nullptr && request.Cancel->load(std::memory_order_relaxed))
                {
                    return Unexpected{ HttpError{ HttpFailure::Cancelled, "it was cancelled" } };
                }

                DWORD l_Read = 0;
                if (!::WinHttpReadData(l_Request.Get(), l_Buffer.data(), c_ReadSize, &l_Read))
                {
                    return Fail(::GetLastError());
                }

                if (l_Read == 0)
                {
                    break;
                }

                if (!sink(std::span<const std::byte>(l_Buffer.data(), l_Read)))
                {
                    return Unexpected{ HttpError{ HttpFailure::Cancelled, "it was cancelled" } };
                }
            }

            return l_Response;
        }
    }
}