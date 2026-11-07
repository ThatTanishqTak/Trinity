#include "Trinity/Core/Platform.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <dlfcn.h>

namespace Trinity
{
    namespace
    {
        // libcurl's own numbers for what this uses, as curl.h gives them, so it is loaded where it is installed without its headers at build time. Its options and results have kept these numbers since they were added
        constexpr int c_OptionWriteData = 10001;
        constexpr int c_OptionUrl = 10002;
        constexpr int c_OptionErrorBuffer = 10010;
        constexpr int c_OptionUserAgent = 10018;
        constexpr int c_OptionHeaders = 10023;
        constexpr int c_OptionHeaderData = 10029;
        constexpr int c_OptionProgressData = 10057;
        constexpr int c_OptionProtocols = 10318;
        constexpr int c_OptionRedirectProtocols = 10319;
        constexpr int c_OptionWriteFunction = 20011;
        constexpr int c_OptionHeaderFunction = 20079;
        constexpr int c_OptionProgressFunction = 20219;
        constexpr int c_OptionLowSpeedLimit = 19;
        constexpr int c_OptionLowSpeedTime = 20;
        constexpr int c_OptionNoProgress = 43;
        constexpr int c_OptionFollowLocation = 52;
        constexpr int c_OptionMaximumRedirects = 68;
        constexpr int c_OptionConnectTimeout = 78;
        constexpr int c_OptionNoSignal = 99;
        constexpr int c_InfoResponseCode = 0x200000 + 2;
        constexpr long c_GlobalDefault = 3;
        constexpr std::size_t c_ErrorSize = 256;

        constexpr int c_ResultOk = 0;
        constexpr int c_ResultCouldNotResolveProxy = 5;
        constexpr int c_ResultCouldNotResolveHost = 6;
        constexpr int c_ResultCouldNotConnect = 7;
        constexpr int c_ResultWriteError = 23;
        constexpr int c_ResultTimedOut = 28;
        constexpr int c_ResultAbortedByCallback = 42;

        // A transfer that sends nothing for this long is taken as no answer, as when the network goes away part way
        constexpr long c_ConnectSeconds = 15;
        constexpr long c_StallSeconds = 30;

        struct CurlList;

        struct Curl
        {
            void* Library = nullptr;
            int (*GlobalInit)(long) = nullptr;
            void* (*EasyInit)() = nullptr;
            int (*EasySetopt)(void*, int, ...) = nullptr;
            int (*EasyPerform)(void*) = nullptr;
            int (*EasyGetinfo)(void*, int, ...) = nullptr;
            void (*EasyCleanup)(void*) = nullptr;
            const char* (*EasyStrerror)(int) = nullptr;
            CurlList* (*ListAppend)(CurlList*, const char*) = nullptr;
            void (*ListFreeAll)(CurlList*) = nullptr;
        };

        template<typename T>
        bool Bind(void* library, const char* name, T& function)
        {
            function = reinterpret_cast<T>(::dlsym(library, name));

            return function != nullptr;
        }

        // Loaded once, the first time it is needed, and kept until the process ends, as libcurl's global state must be. Each platform's usual names for it are tried in turn
        const Curl* LoadCurl()
        {
            static const Curl s_Curl = []
            {
#if defined(TR_PLATFORM_MACOS)
                constexpr std::array<const char*, 2> c_Names{ "libcurl.4.dylib", "/usr/lib/libcurl.4.dylib" };
#else
                constexpr std::array<const char*, 3> c_Names{ "libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so" };
#endif
                Curl l_Curl;
                for (const char* it_Name : c_Names)
                {
                    l_Curl.Library = ::dlopen(it_Name, RTLD_NOW | RTLD_LOCAL);
                    if (l_Curl.Library != nullptr)
                    {
                        break;
                    }
                }

                if (l_Curl.Library == nullptr)
                {
                    return l_Curl;
                }

                const bool l_Bound = Bind(l_Curl.Library, "curl_global_init", l_Curl.GlobalInit) && Bind(l_Curl.Library, "curl_easy_init", l_Curl.EasyInit)
                    && Bind(l_Curl.Library, "curl_easy_setopt", l_Curl.EasySetopt) && Bind(l_Curl.Library, "curl_easy_perform", l_Curl.EasyPerform)
                    && Bind(l_Curl.Library, "curl_easy_getinfo", l_Curl.EasyGetinfo) && Bind(l_Curl.Library, "curl_easy_cleanup", l_Curl.EasyCleanup)
                    && Bind(l_Curl.Library, "curl_easy_strerror", l_Curl.EasyStrerror) && Bind(l_Curl.Library, "curl_slist_append", l_Curl.ListAppend)
                    && Bind(l_Curl.Library, "curl_slist_free_all", l_Curl.ListFreeAll);
                if (!l_Bound || l_Curl.GlobalInit(c_GlobalDefault) != c_ResultOk)
                {
                    ::dlclose(l_Curl.Library);
                    l_Curl.Library = nullptr;
                }

                return l_Curl;
            }();

            return s_Curl.Library != nullptr ? &s_Curl : nullptr;
        }

        struct Transfer
        {
            const std::function<bool(std::span<const std::byte>)>* Sink = nullptr;
            const std::atomic<bool>* Cancel = nullptr;
            std::vector<HttpHeader> Headers;
            bool Stopped = false;
        };

        std::size_t OnWrite(char* data, std::size_t size, std::size_t count, void* user)
        {
            Transfer& l_Transfer = *static_cast<Transfer*>(user);
            const std::size_t l_Size = size * count;
            if (!(*l_Transfer.Sink)(std::as_bytes(std::span(data, l_Size))))
            {
                l_Transfer.Stopped = true;

                return 0;
            }

            return l_Size;
        }

        std::string_view Trim(std::string_view text)
        {
            const std::size_t l_First = text.find_first_not_of(" \t\r\n");
            if (l_First == std::string_view::npos)
            {
                return {};
            }

            return text.substr(l_First, text.find_last_not_of(" \t\r\n") - l_First + 1);
        }

        // Each answer's status line begins its headers again, so only the final answer's are kept after redirects
        std::size_t OnHeader(char* data, std::size_t size, std::size_t count, void* user)
        {
            Transfer& l_Transfer = *static_cast<Transfer*>(user);
            const std::string_view l_Line(data, size * count);
            if (l_Line.starts_with("HTTP/"))
            {
                l_Transfer.Headers.clear();
            }
            else if (const std::size_t l_Colon = l_Line.find(':'); l_Colon != std::string_view::npos)
            {
                l_Transfer.Headers.push_back({ std::string(Trim(l_Line.substr(0, l_Colon))), std::string(Trim(l_Line.substr(l_Colon + 1))) });
            }

            return size * count;
        }

        // Called now and then even while nothing arrives, so Cancel ends a transfer that has stalled too
        int OnProgress(void* user, std::int64_t, std::int64_t, std::int64_t, std::int64_t)
        {
            const Transfer& l_Transfer = *static_cast<const Transfer*>(user);

            return l_Transfer.Cancel != nullptr && l_Transfer.Cancel->load(std::memory_order_relaxed) ? 1 : 0;
        }
    }

    namespace Platform
    {
        bool HasHttp()
        {
            return LoadCurl() != nullptr;
        }

        Expected<HttpResponse, HttpError> HttpGet(const HttpRequest& request, const std::function<bool(std::span<const std::byte>)>& sink)
        {
            const Curl* l_Curl = LoadCurl();
            if (l_Curl == nullptr)
            {
                return Unexpected{ HttpError{ HttpFailure::Failed, "libcurl, which Trinity reaches the network through on this platform, is not installed" } };
            }

            void* l_Handle = l_Curl->EasyInit();
            if (l_Handle == nullptr)
            {
                return Unexpected{ HttpError{ HttpFailure::Failed, "libcurl could not start a transfer" } };
            }

            CurlList* l_Headers = nullptr;
            for (const HttpHeader& it_Header : request.Headers)
            {
                l_Headers = l_Curl->ListAppend(l_Headers, std::format("{}: {}", it_Header.Name, it_Header.Value).c_str());
            }

            Transfer l_Transfer{ &sink, request.Cancel,{}, false };
            std::array<char, c_ErrorSize> l_ErrorText{};
            l_Curl->EasySetopt(l_Handle, c_OptionUrl, request.Url.c_str());
            l_Curl->EasySetopt(l_Handle, c_OptionUserAgent, request.UserAgent.c_str());
            l_Curl->EasySetopt(l_Handle, c_OptionHeaders, l_Headers);
            l_Curl->EasySetopt(l_Handle, c_OptionProtocols, "http,https");
            l_Curl->EasySetopt(l_Handle, c_OptionRedirectProtocols, "http,https");
            l_Curl->EasySetopt(l_Handle, c_OptionFollowLocation, 1L);
            l_Curl->EasySetopt(l_Handle, c_OptionMaximumRedirects, 10L);
            l_Curl->EasySetopt(l_Handle, c_OptionNoSignal, 1L);
            l_Curl->EasySetopt(l_Handle, c_OptionConnectTimeout, c_ConnectSeconds);
            l_Curl->EasySetopt(l_Handle, c_OptionLowSpeedLimit, 1L);
            l_Curl->EasySetopt(l_Handle, c_OptionLowSpeedTime, c_StallSeconds);
            l_Curl->EasySetopt(l_Handle, c_OptionErrorBuffer, l_ErrorText.data());
            l_Curl->EasySetopt(l_Handle, c_OptionWriteFunction, &OnWrite);
            l_Curl->EasySetopt(l_Handle, c_OptionWriteData, &l_Transfer);
            l_Curl->EasySetopt(l_Handle, c_OptionHeaderFunction, &OnHeader);
            l_Curl->EasySetopt(l_Handle, c_OptionHeaderData, &l_Transfer);
            l_Curl->EasySetopt(l_Handle, c_OptionProgressFunction, &OnProgress);
            l_Curl->EasySetopt(l_Handle, c_OptionProgressData, &l_Transfer);
            l_Curl->EasySetopt(l_Handle, c_OptionNoProgress, 0L);

            const int l_Result = l_Curl->EasyPerform(l_Handle);
            long l_Status = 0;
            l_Curl->EasyGetinfo(l_Handle, c_InfoResponseCode, &l_Status);
            l_Curl->EasyCleanup(l_Handle);
            l_Curl->ListFreeAll(l_Headers);

            if (l_Result == c_ResultOk)
            {
                return HttpResponse{ static_cast<int>(l_Status), std::move(l_Transfer.Headers),{} };
            }

            const std::string l_Reason = l_ErrorText[0] != '\0' ? std::string(l_ErrorText.data()) : std::string(l_Curl->EasyStrerror(l_Result));
            if (l_Result == c_ResultAbortedByCallback || (l_Result == c_ResultWriteError && l_Transfer.Stopped))
            {
                return Unexpected{ HttpError{ HttpFailure::Cancelled, "it was cancelled" } };
            }

            const bool l_Unreachable = l_Result == c_ResultCouldNotResolveProxy || l_Result == c_ResultCouldNotResolveHost || l_Result == c_ResultCouldNotConnect || l_Result == c_ResultTimedOut;

            return Unexpected{ HttpError{ l_Unreachable ? HttpFailure::Unreachable : HttpFailure::Failed, l_Reason } };
        }
    }
}