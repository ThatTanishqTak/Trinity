#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    struct HttpHeader
    {
        std::string Name;
        std::string Value;
    };

    struct HttpRequest
    {
        std::string Url;
        std::vector<HttpHeader> Headers;
        // Servers such as GitHub's API refuse a request without one
        std::string UserAgent = "Trinity";
        // Returning false cancels. Without one the body is kept in the response, up to BodyLimit
        std::function<bool(std::span<const std::byte>)> OnBody;
        std::size_t BodyLimit = 16 * 1024 * 1024;
        // Polled while the request runs. True cancels it
        const std::atomic<bool>* Cancel = nullptr;
    };

    // The answer after any redirects, whatever its status
    struct TRINITY_API HttpResponse
    {
        int Status = 0;
        std::vector<HttpHeader> Headers;
        // Only when the request gave no OnBody
        std::string Body;

        // Matches the name in any case
        [[nodiscard]] std::optional<std::string_view> FindHeader(std::string_view name) const;
    };

    enum class HttpFailure : std::uint8_t
    {
        // No answer: the host was not found or reached, as when offline
        Unreachable,
        Cancelled,
        // Anything else, such as a bad URL or certificate
        Failed
    };

    struct HttpError
    {
        HttpFailure Failure = HttpFailure::Failed;
        std::string Reason;
    };

    // WinHTTP on Windows, libcurl elsewhere. Redirects are followed, and any status is an answer
    namespace Http
    {
        // False where libcurl is not installed. Always true on Windows
        [[nodiscard]] TRINITY_API bool IsAvailable();
        [[nodiscard]] TRINITY_API Expected<HttpResponse, HttpError> Get(const HttpRequest& request);
    }
}