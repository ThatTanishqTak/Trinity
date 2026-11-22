#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// An engine version Hub lists: one it installed from a zip, or one the person located
struct HubInstall
{
    enum class Origin : std::uint8_t
    {
        Installed,
        Located
    };

    std::filesystem::path Directory;
    Origin Source = Origin::Located;
    // As its manifest was last read, which Installs.yaml keeps, so a missing version still shows
    Trinity::EngineManifest Manifest;
    // Its folder is gone
    bool Missing = false;
    // Why its manifest could not be read, though its folder is there
    std::string Problem;
    // Its manifest is of a format newer than this Hub reads, which a newer Hub does
    bool NeedsNewerHub = false;
    // What every file under its folder holds, once counted
    std::optional<std::uint64_t> Size;
};

// Installs.yaml in Hub's folder, newest first. Manifests are read again as the page is shown
class HubInstalls
{
public:
    static constexpr std::uint32_t c_FormatVersion = 1;
    static constexpr std::string_view c_Path = "/saves/Installs.yaml";

    HubInstalls() = default;
    ~HubInstalls();

    HubInstalls(const HubInstalls&) = delete;
    HubInstalls& operator=(const HubInstalls&) = delete;

    // Each version not readable from Installs.yaml is left out. A newer format's file is never written
    void Load();
    // Each manifest read again, and every size counted again
    void Refresh();
    // Once a frame, taking the sizes counted
    void Update();

    [[nodiscard]] const std::vector<HubInstall>& GetInstalls() const { return m_Installs; }
    // Installs.yaml was written by a newer Hub, so it lists no version here and is not written
    [[nodiscard]] bool IsFromNewerHub() const { return !m_Writable; }
    [[nodiscard]] const HubInstall* Find(const std::filesystem::path& directory) const;
    // The installed version of this name, as Trinity-<version>-<platform>-<architecture>
    [[nodiscard]] const HubInstall* FindInstalled(std::string_view name) const;
    // The usable version a project naming this one opens in: the one built last when two are of it
    [[nodiscard]] const HubInstall* FindUsable(std::string_view version) const;
    // The one of the newest version that is usable, which New Project offers first. None if none is
    [[nodiscard]] const HubInstall* FindNewestUsable() const;

    // Replaces the version listed in the same folder, if one is, and saves
    void Add(const std::filesystem::path& directory, HubInstall::Origin origin, Trinity::EngineManifest manifest);
    void Remove(const std::filesystem::path& directory);

    // The folder an installed version of this manifest is named, as its zip is
    [[nodiscard]] static std::string GetInstallName(const Trinity::EngineManifest& manifest);
    // As Hub names a version in what it says, Trinity 0.11.0 (209a33d)
    [[nodiscard]] static std::string Describe(const Trinity::EngineManifest& manifest);
    // The same where both are there and are one on the disk, and otherwise when their paths are
    [[nodiscard]] static bool IsSameFolder(const std::filesystem::path& left, const std::filesystem::path& right);
    // Installed or located, not missing or unreadable, and built for this platform and architecture
    [[nodiscard]] static bool IsUsable(const HubInstall& install);

private:
    struct SizeCount
    {
        std::vector<std::filesystem::path> Directories;
        std::vector<std::optional<std::uint64_t>> Sizes;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Done = false;
    };

    void Save() const;
    void Read(HubInstall& install) const;
    void Sort();
    void CountSizes();
    static void Count(SizeCount& count);

    std::vector<HubInstall> m_Installs;
    // False once Installs.yaml was found of a newer format, which this Hub never writes over
    bool m_Writable = true;
    std::unique_ptr<SizeCount> m_Count;
    bool m_CountAgain = false;
    Trinity::JobCounter m_Jobs;
};