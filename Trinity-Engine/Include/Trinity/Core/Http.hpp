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
        // Each piece of the body as it arrives, which false stops, failing as cancelled. Without one, the body is kept in the response, up to BodyLimit
        std::function<bool(std::span<const std::byte>)> OnBody;
        std::size_t BodyLimit = 16 * 1024 * 1024;
        // Read now and then while it runs, and true ends it, failing as cancelled
        const std::atomic<bool>* Cancel = nullptr;
    };

    // The answer after any redirects, whatever its status
    struct TRINITY_API HttpResponse
    {
        int Status = 0;
        std::vector<HttpHeader> Headers;
        // Only when the request gave no OnBody
        std::string Body;

        // By its name, in any case, as HTTP's header names are
        [[nodiscard]] std::optional<std::string_view> FindHeader(std::string_view name) const;
    };

    enum class HttpFailure : std::uint8_t
    {
        // No answer came: the host could not be found or reached, or stopped answering, as when the machine is offline
        Unreachable,
        Cancelled,
        // Anything else, such as a URL that is none, or a certificate that does not hold
        Failed
    };

    struct HttpError
    {
        HttpFailure Failure = HttpFailure::Failed;
        std::string Reason;
    };

    // GET over HTTP and HTTPS through the platform's own client: WinHTTP on Windows, through the system's proxy, and libcurl elsewhere, loaded where it is installed, through the proxy the environment names. Redirects are followed, and a status of any kind is an answer, so only a request that got none fails
    namespace Http
    {
        // Always on Windows, and elsewhere where libcurl is installed
        [[nodiscard]] TRINITY_API bool IsAvailable();
        [[nodiscard]] TRINITY_API Expected<HttpResponse, HttpError> Get(const HttpRequest& request);
    }
}