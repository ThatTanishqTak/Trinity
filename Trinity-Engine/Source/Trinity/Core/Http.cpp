#include "Trinity/Core/Http.hpp"

#include "Trinity/Core/Platform.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"

#include <algorithm>
#include <format>

namespace Trinity
{
    std::optional<std::string_view> HttpResponse::FindHeader(std::string_view name) const
    {
        const auto a_Header = std::ranges::find_if(Headers, [name](const HttpHeader& header) { return FileSystemUtilities::EqualsIgnoreAsciiCase(header.Name, name); });
        if (a_Header == Headers.end())
        {
            return std::nullopt;
        }

        return std::string_view(a_Header->Value);
    }

    namespace Http
    {
        bool IsAvailable()
        {
            return Platform::HasHttp();
        }

        // Without a sink of its own, the body is gathered up to its limit, past which it fails rather than holding what a server sent without end
        Expected<HttpResponse, HttpError> Get(const HttpRequest& request)
        {
            if (request.OnBody)
            {
                return Platform::HttpGet(request, request.OnBody);
            }

            std::string l_Body;
            bool l_TooLarge = false;
            Expected<HttpResponse, HttpError> l_Response = Platform::HttpGet(request, [&request, &l_Body, &l_TooLarge](std::span<const std::byte> piece)
            {
                if (l_Body.size() + piece.size() > request.BodyLimit)
                {
                    l_TooLarge = true;

                    return false;
                }

                l_Body.append(reinterpret_cast<const char*>(piece.data()), piece.size());

                return true;
            });

            if (l_TooLarge)
            {
                return Unexpected{ HttpError{ HttpFailure::Failed, std::format("{} answered with more than the {} bytes it may", request.Url, request.BodyLimit) } };
            }

            if (l_Response)
            {
                l_Response->Body = std::move(l_Body);
            }

            return l_Response;
        }
    }
}