#include "HubReleases.hpp"

#include "HubInstalls.hpp"
#include "HubText.hpp"
#include "HubVersion.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace
{
    constexpr std::size_t c_ListLimit = 8 * 1024 * 1024;
    constexpr std::size_t c_HashLength = 64;

    std::string ReadScalar(const YAML::Node& map, const char* key)
    {
        const YAML::Node l_Value = map[key];

        return l_Value && l_Value.IsScalar() ? l_Value.Scalar() : std::string();
    }

    // The host a URL names, as messages name it
    std::string_view GetHost(std::string_view url)
    {
        const std::size_t l_Scheme = url.find("://");
        std::string_view l_Rest = l_Scheme == std::string_view::npos ? url : url.substr(l_Scheme + 3);

        return l_Rest.substr(0, l_Rest.find_first_of("/?#"));
    }

    void AppendUtf8(std::string& text, std::uint32_t codePoint)
    {
        if (codePoint < 0x80)
        {
            text += static_cast<char>(codePoint);
        }
        else if (codePoint < 0x800)
        {
            text += static_cast<char>(0xC0 | (codePoint >> 6));
            text += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
        else if (codePoint < 0x10000)
        {
            text += static_cast<char>(0xE0 | (codePoint >> 12));
            text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
        else
        {
            text += static_cast<char>(0xF0 | (codePoint >> 18));
            text += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
    }

    std::optional<std::uint32_t> ReadEscape(std::string_view json, std::size_t at)
    {
        std::uint32_t l_Value = 0;
        if (at + 6 > json.size() || json[at] != '\\' || json[at + 1] != 'u' || std::from_chars(json.data() + at + 2, json.data() + at + 6, l_Value, 16).ptr != json.data() + at + 6)
        {
            return std::nullopt;
        }

        return l_Value;
    }

    // JSON writes a character beyond the first 65536 as two escapes, a surrogate pair, which yaml-cpp refuses, so each pair becomes the character itself, in UTF-8, and a surrogate alone becomes U+FFFD, before the JSON is read as the YAML it otherwise is
    std::string JoinSurrogates(std::string_view json)
    {
        std::string l_Text;
        l_Text.reserve(json.size());
        bool l_InString = false;
        for (std::size_t it_Index = 0; it_Index < json.size(); ++it_Index)
        {
            const char l_Character = json[it_Index];
            if (l_Character == '"')
            {
                l_InString = !l_InString;
            }

            if (!l_InString || l_Character != '\\' || it_Index + 1 >= json.size())
            {
                l_Text += l_Character;

                continue;
            }

            const std::optional<std::uint32_t> l_Escape = ReadEscape(json, it_Index);
            if (!l_Escape || *l_Escape < 0xD800 || *l_Escape > 0xDFFF)
            {
                l_Text += l_Character;
                l_Text += json[++it_Index];

                continue;
            }

            const std::optional<std::uint32_t> l_Low = ReadEscape(json, it_Index + 6);
            if (*l_Escape <= 0xDBFF && l_Low && *l_Low >= 0xDC00 && *l_Low <= 0xDFFF)
            {
                AppendUtf8(l_Text, 0x10000 + ((*l_Escape - 0xD800) << 10) + (*l_Low - 0xDC00));
                it_Index += 11;

                continue;
            }

            l_Text += "\\uFFFD";
            it_Index += 5;
        }

        return l_Text;
    }

    // A name Hub writes as a file of its own, nothing above or beside it
    bool IsPlainName(std::string_view name)
    {
        return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string_view::npos;
    }

    std::string ToLower(std::string text)
    {
        std::ranges::transform(text, text.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });

        return text;
    }

    bool IsHash(std::string_view text)
    {
        return text.size() == c_HashLength && std::ranges::all_of(text, [](char character) { return std::isxdigit(static_cast<unsigned char>(character)) != 0; });
    }

    // GitHub's message in an answer that is no list, such as one refusing the request
    std::string ReadMessage(std::string_view body)
    {
        try
        {
            const YAML::Node l_Root = YAML::Load(JoinSurrogates(body));

            return l_Root.IsMap() ? ReadScalar(l_Root, "message") : std::string();
        }
        catch (const YAML::Exception&)
        {
            return {};
        }
    }
}

HubReleases::HubReleases(std::string url) : m_Url(std::move(url))
{

}

// An ask still waiting is let finish, and its answer dropped
HubReleases::~HubReleases()
{
    if (m_Fetch)
    {
        m_Fetch->Stop.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_Jobs);
    }
}

std::string HubReleases::GetDefaultUrl()
{
    return std::format("https://api.github.com/repos/{}/releases?per_page=100", c_Repository);
}

std::string HubReleases::GetUserAgent()
{
    return std::format("Trinity-Hub/{}", HubVersion::c_Version);
}

std::string HubReleases::GetReleasesPage()
{
    return std::format("https://github.com/{}/releases", c_Repository);
}

// Drafts and Hub's own releases are left out, and so is a zip of another platform or a name that is no plain file name. Of two releases of one version, the newer is kept
Trinity::Expected<std::vector<HubRelease>, std::string> HubReleases::Parse(std::string_view json, std::string_view platform, std::string_view architecture, std::size_t* others)
{
    YAML::Node l_Root;
    try
    {
        l_Root = YAML::Load(JoinSurrogates(json));
    }
    catch (const YAML::Exception& exception)
    {
        return Trinity::Unexpected{ std::format("it is not valid JSON: {}", exception.what()) };
    }

    if (!l_Root.IsSequence())
    {
        return Trinity::Unexpected{ std::string("it is no list of releases") };
    }

    const std::string l_Suffix = std::format("-{}-{}.zip", platform, architecture);
    std::vector<HubRelease> l_Releases;
    std::size_t l_Others = 0;
    for (const YAML::Node& it_Item : l_Root)
    {
        if (!it_Item.IsMap() || ReadScalar(it_Item, "draft") == "true" || ReadScalar(it_Item, "tag_name").starts_with(c_HubTagPrefix))
        {
            continue;
        }

        HubRelease l_Release;
        const YAML::Node l_Assets = it_Item["assets"];
        if (l_Assets && l_Assets.IsSequence())
        {
            for (const YAML::Node& it_Asset : l_Assets)
            {
                const std::string l_Name = it_Asset.IsMap() ? ReadScalar(it_Asset, "name") : std::string();
                if (!IsPlainName(l_Name) || !l_Name.starts_with("Trinity-") || !l_Name.ends_with(l_Suffix) || l_Name.size() <= l_Suffix.size() + 8)
                {
                    continue;
                }

                l_Release.ZipName = l_Name;
                l_Release.Version = l_Name.substr(8, l_Name.size() - 8 - l_Suffix.size());
                l_Release.ZipUrl = ReadScalar(it_Asset, "browser_download_url");
                const std::string l_Size = ReadScalar(it_Asset, "size");
                std::from_chars(l_Size.data(), l_Size.data() + l_Size.size(), l_Release.ZipSize);
                const std::string l_Digest = ReadScalar(it_Asset, "digest");
                if (l_Digest.starts_with("sha256:") && IsHash(std::string_view(l_Digest).substr(7)))
                {
                    l_Release.Digest = ToLower(l_Digest.substr(7));
                }
            }

            for (const YAML::Node& it_Asset : l_Assets)
            {
                if (!l_Release.ZipName.empty() && it_Asset.IsMap() && ReadScalar(it_Asset, "name") == l_Release.ZipName + ".sha256")
                {
                    l_Release.Sha256Url = ReadScalar(it_Asset, "browser_download_url");
                }
            }
        }

        if (l_Release.ZipName.empty() || l_Release.ZipUrl.empty())
        {
            ++l_Others;

            continue;
        }

        l_Release.Tag = ReadScalar(it_Item, "tag_name");
        l_Release.Name = ReadScalar(it_Item, "name");
        if (l_Release.Name.empty())
        {
            l_Release.Name = l_Release.Tag;
        }

        l_Release.PublishedAt = ReadScalar(it_Item, "published_at");
        l_Release.Page = ReadScalar(it_Item, "html_url");
        l_Release.Prerelease = ReadScalar(it_Item, "prerelease") == "true" || l_Release.Version.find('-') != std::string::npos;
        l_Releases.push_back(std::move(l_Release));
    }

    std::ranges::stable_sort(l_Releases, [](const HubRelease& left, const HubRelease& right)
    {
        if (const std::strong_ordering l_Order = Trinity::CompareVersions(left.Version, right.Version); l_Order != 0)
        {
            return l_Order > 0;
        }

        return left.PublishedAt > right.PublishedAt;
    });

    const auto a_Repeated = std::ranges::unique(l_Releases, {}, &HubRelease::Version);
    l_Releases.erase(a_Repeated.begin(), a_Repeated.end());
    if (others != nullptr)
    {
        *others = l_Others;
    }

    return l_Releases;
}

void HubReleases::Load()
{
    m_Releases.clear();
    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(c_Path);
    if (!l_Text)
    {
        return;
    }

    YAML::Node l_Root;
    try
    {
        l_Root = YAML::Load(*l_Text);
    }
    catch (const YAML::Exception& exception)
    {
        TR_WARN("Hub: {} is not valid YAML, so Hub has no list of releases until it asks GitHub again: {}", c_Path, exception.what());

        return;
    }

    std::uint32_t l_Format = 0;
    const std::string l_FormatText = l_Root.IsMap() ? ReadScalar(l_Root, "Format") : std::string();
    std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Format);
    if (l_Format == 0 || l_Format > c_FormatVersion || ReadScalar(l_Root, "Url") != m_Url)
    {
        return;
    }

    m_FetchedAt = ReadScalar(l_Root, "FetchedAt");
    m_ETag = ReadScalar(l_Root, "ETag");
    const std::string l_Others = ReadScalar(l_Root, "Others");
    std::from_chars(l_Others.data(), l_Others.data() + l_Others.size(), m_Others);
    if (const std::optional<std::chrono::sys_seconds> l_Fetched = HubText::ParseTime(m_FetchedAt))
    {
        m_LastAsked = *l_Fetched;
    }

    const YAML::Node l_List = l_Root["Releases"];
    if (!l_List || !l_List.IsSequence())
    {
        return;
    }

    for (const YAML::Node& it_Item : l_List)
    {
        if (!it_Item.IsMap())
        {
            continue;
        }

        HubRelease l_Release;
        l_Release.Version = ReadScalar(it_Item, "Version");
        l_Release.Tag = ReadScalar(it_Item, "Tag");
        l_Release.Name = ReadScalar(it_Item, "Name");
        l_Release.PublishedAt = ReadScalar(it_Item, "PublishedAt");
        l_Release.Page = ReadScalar(it_Item, "Page");
        l_Release.Prerelease = ReadScalar(it_Item, "Prerelease") == "true";
        l_Release.ZipName = ReadScalar(it_Item, "Zip");
        l_Release.ZipUrl = ReadScalar(it_Item, "ZipUrl");
        const std::string l_Size = ReadScalar(it_Item, "ZipSize");
        std::from_chars(l_Size.data(), l_Size.data() + l_Size.size(), l_Release.ZipSize);
        l_Release.Sha256Url = ReadScalar(it_Item, "Sha256Url");
        l_Release.Digest = ReadScalar(it_Item, "Digest");
        if (!l_Release.Version.empty() && IsPlainName(l_Release.ZipName) && !l_Release.ZipUrl.empty() && (l_Release.Digest.empty() || IsHash(l_Release.Digest)))
        {
            m_Releases.push_back(std::move(l_Release));
        }
    }
}

void HubReleases::Refresh()
{
    if (m_Fetch || GetWait().count() > 0)
    {
        return;
    }

    m_LastAsked = std::chrono::system_clock::now();
    m_Fetch = std::make_unique<Fetch>();
    m_Fetch->Url = m_Url;
    m_Fetch->ETag = m_Releases.empty() && m_FetchedAt.empty() ? std::string() : m_ETag;

    Fetch* l_Fetch = m_Fetch.get();
    Trinity::JobSystem::Submit([l_Fetch] { Run(*l_Fetch); }, &m_Jobs);
}

void HubReleases::Update()
{
    if (!m_Fetch || !m_Fetch->Done.load(std::memory_order_acquire))
    {
        return;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    const std::unique_ptr<Fetch> l_Fetch = std::move(m_Fetch);
    switch (l_Fetch->Result)
    {
        case Fetch::Outcome::Listed:
        {
            m_Releases = std::move(l_Fetch->Releases);
            m_Others = l_Fetch->Others;
            m_ETag = l_Fetch->NewETag;
            m_FetchedAt = HubText::FormatTime(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
            m_Note.clear();
            TR_INFO("Hub: GitHub lists {} release(s) of Trinity for {} on {}{}", m_Releases.size(), Trinity::GetBuildPlatform(), Trinity::GetBuildArchitecture(), m_Others > 0 ? std::format(", and {} for other platforms only", m_Others) : std::string());
            Save();

            break;
        }
        case Fetch::Outcome::Unchanged:
        {
            m_FetchedAt = HubText::FormatTime(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
            m_Note.clear();
            Save();

            break;
        }
        case Fetch::Outcome::Refused:
        {
            m_Note = l_Fetch->Note;
            TR_WARN("Hub: {}", m_Note);

            break;
        }
    }
}

std::chrono::seconds HubReleases::GetWait() const
{
    if (!m_LastAsked)
    {
        return std::chrono::seconds(0);
    }

    const auto l_Since = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - *m_LastAsked);

    return l_Since >= c_Interval || l_Since.count() < 0 ? std::chrono::seconds(0) : c_Interval - l_Since;
}

// Asked with the list's ETag, which GitHub answers with 304 and no list when nothing has changed. A refusal for the hour's requests being used says when they are given again
void HubReleases::Run(Fetch& fetch)
{
    const std::string_view l_Host = GetHost(fetch.Url);
    Trinity::HttpRequest l_Request;
    l_Request.Url = fetch.Url;
    l_Request.UserAgent = GetUserAgent();
    l_Request.Headers = { { "Accept", "application/vnd.github+json" },{ "X-GitHub-Api-Version", "2022-11-28" } };
    if (!fetch.ETag.empty())
    {
        l_Request.Headers.push_back({ "If-None-Match", fetch.ETag });
    }

    l_Request.BodyLimit = c_ListLimit;
    l_Request.Cancel = &fetch.Stop;

    const auto l_Refuse = [&fetch](std::string note)
    {
        fetch.Result = Fetch::Outcome::Refused;
        fetch.Note = std::move(note);
        fetch.Done.store(true, std::memory_order_release);
    };

    const Trinity::Expected<Trinity::HttpResponse, Trinity::HttpError> l_Response = Trinity::Http::Get(l_Request);
    if (!l_Response)
    {
        const Trinity::HttpError& l_Error = l_Response.GetError();
        l_Refuse(l_Error.Failure == Trinity::HttpFailure::Unreachable ? std::format("Could not reach {}, as this machine may be offline: {}", l_Host, l_Error.Reason) : std::format("Could not ask {} for Trinity's releases: {}", l_Host, l_Error.Reason));

        return;
    }

    if (l_Response->Status == 304)
    {
        fetch.Result = Fetch::Outcome::Unchanged;
        fetch.Done.store(true, std::memory_order_release);

        return;
    }

    const std::string l_Message = l_Response->Status == 200 ? std::string() : ReadMessage(l_Response->Body);
    if ((l_Response->Status == 403 || l_Response->Status == 429) && l_Response->FindHeader("x-ratelimit-remaining").value_or("") == "0")
    {
        std::int64_t l_Reset = 0;
        const std::string_view l_ResetText = l_Response->FindHeader("x-ratelimit-reset").value_or("");
        std::from_chars(l_ResetText.data(), l_ResetText.data() + l_ResetText.size(), l_Reset);
        const std::chrono::sys_seconds l_At{ std::chrono::seconds(l_Reset) };
        const auto l_Minutes = std::chrono::ceil<std::chrono::minutes>(l_At - std::chrono::system_clock::now()).count();
        l_Refuse(std::format("GitHub answers 60 requests an hour from this address without a token, and this hour's are used, until {}{}", l_Reset > 0 ? std::format("{:%H:%M} UTC", l_At) : std::string("the hour is out"), l_Reset > 0 && l_Minutes > 0 ? std::format(", in {} minute{}", l_Minutes, l_Minutes == 1 ? "" : "s") : std::string()));

        return;
    }

    if (const std::optional<std::string_view> l_RetryAfter = l_Response->FindHeader("retry-after"); l_RetryAfter && (l_Response->Status == 403 || l_Response->Status == 429))
    {
        l_Refuse(std::format("GitHub asks Hub to wait {} seconds before it asks again{}", *l_RetryAfter, l_Message.empty() ? std::string() : std::format(": {}", l_Message)));

        return;
    }

    if (l_Response->Status != 200)
    {
        l_Refuse(std::format("{} answered {} for Trinity's releases{}", l_Host, l_Response->Status, l_Message.empty() ? std::string() : std::format(": {}", l_Message)));

        return;
    }

    std::size_t l_Others = 0;
    Trinity::Expected<std::vector<HubRelease>, std::string> l_Releases = Parse(l_Response->Body, Trinity::GetBuildPlatform(), Trinity::GetBuildArchitecture(), &l_Others);
    if (!l_Releases)
    {
        l_Refuse(std::format("{}'s list of Trinity's releases could not be read, as {}", l_Host, l_Releases.GetError()));

        return;
    }

    fetch.Result = Fetch::Outcome::Listed;
    fetch.Releases = std::move(*l_Releases);
    fetch.Others = l_Others;
    fetch.NewETag = std::string(l_Response->FindHeader("etag").value_or(""));
    fetch.Done.store(true, std::memory_order_release);
}

void HubReleases::Save() const
{
    YAML::Emitter l_Emitter;
    l_Emitter << YAML::BeginMap;
    l_Emitter << YAML::Key << "Format" << YAML::Value << c_FormatVersion;
    l_Emitter << YAML::Key << "Url" << YAML::Value << YAML::DoubleQuoted << m_Url;
    l_Emitter << YAML::Key << "FetchedAt" << YAML::Value << YAML::DoubleQuoted << m_FetchedAt;
    l_Emitter << YAML::Key << "ETag" << YAML::Value << YAML::DoubleQuoted << m_ETag;
    l_Emitter << YAML::Key << "Others" << YAML::Value << m_Others;
    l_Emitter << YAML::Key << "Releases" << YAML::Value << YAML::BeginSeq;
    for (const HubRelease& it_Release : m_Releases)
    {
        l_Emitter << YAML::BeginMap;
        l_Emitter << YAML::Key << "Version" << YAML::Value << YAML::DoubleQuoted << it_Release.Version;
        l_Emitter << YAML::Key << "Tag" << YAML::Value << YAML::DoubleQuoted << it_Release.Tag;
        l_Emitter << YAML::Key << "Name" << YAML::Value << YAML::DoubleQuoted << it_Release.Name;
        l_Emitter << YAML::Key << "PublishedAt" << YAML::Value << YAML::DoubleQuoted << it_Release.PublishedAt;
        l_Emitter << YAML::Key << "Page" << YAML::Value << YAML::DoubleQuoted << it_Release.Page;
        l_Emitter << YAML::Key << "Prerelease" << YAML::Value << it_Release.Prerelease;
        l_Emitter << YAML::Key << "Zip" << YAML::Value << YAML::DoubleQuoted << it_Release.ZipName;
        l_Emitter << YAML::Key << "ZipUrl" << YAML::Value << YAML::DoubleQuoted << it_Release.ZipUrl;
        l_Emitter << YAML::Key << "ZipSize" << YAML::Value << it_Release.ZipSize;
        l_Emitter << YAML::Key << "Sha256Url" << YAML::Value << YAML::DoubleQuoted << it_Release.Sha256Url;
        l_Emitter << YAML::Key << "Digest" << YAML::Value << YAML::DoubleQuoted << it_Release.Digest;
        l_Emitter << YAML::EndMap;
    }

    l_Emitter << YAML::EndSeq;
    l_Emitter << YAML::EndMap;

    const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(c_Path, std::string(l_Emitter.c_str()) + "\n");
    if (!l_Written)
    {
        TR_WARN("Hub: {} could not be written, so the list of releases is asked for again as Hub next starts: {}", c_Path, Trinity::ToString(l_Written.GetError()));
    }
}