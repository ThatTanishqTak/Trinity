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

// An engine version Hub lists: one it installed from a zip, whose files it owns, or one the person located, such as a build tree's bin, which Hub only lists
struct HubInstall
{
    enum class Origin : std::uint8_t
    {
        Installed,
        Located
    };

    std::filesystem::path Directory;
    Origin Source = Origin::Located;
    // As its manifest was last read, which Installs.yaml keeps, so a version whose folder is gone still shows what it was
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

// Hub's registry of versions, Installs.yaml in its folder, newest first. Each version's manifest is read again from its folder as Hub starts and whenever the Installs page is shown, so a build tree built again shows its new commit
class HubInstalls
{
public:
    static constexpr std::uint32_t c_FormatVersion = 1;
    static constexpr std::string_view c_Path = "/saves/Installs.yaml";

    HubInstalls() = default;
    ~HubInstalls();

    HubInstalls(const HubInstalls&) = delete;
    HubInstalls& operator=(const HubInstalls&) = delete;

    // Each version not readable from Installs.yaml is told once and left out. A file of a newer format is left as it is, never written over
    void Load();
    // Each manifest read again, and every size counted again
    void Refresh();
    // Once a frame, taking the sizes counted
    void Update();

    [[nodiscard]] const std::vector<HubInstall>& GetInstalls() const { return m_Installs; }
    // Installs.yaml was written by a newer Hub, so it lists no version here, and none located or installed is kept in it
    [[nodiscard]] bool IsFromNewerHub() const { return !m_Writable; }
    [[nodiscard]] const HubInstall* Find(const std::filesystem::path& directory) const;
    // The installed version of this name, as Trinity-<version>-<platform>-<architecture>, wherever it is
    [[nodiscard]] const HubInstall* FindInstalled(std::string_view name) const;
    // The usable version a project naming this one opens in: the one built last when two are of it, as a build tree built again beside the version installed from its zip, and the installed one when both were built at once. None when no usable one is of it
    [[nodiscard]] const HubInstall* FindUsable(std::string_view version) const;
    // The one of the newest version that is usable, as FindUsable chooses among those of one version, which New Project offers first. None when no version is usable
    [[nodiscard]] const HubInstall* FindNewestUsable() const;

    // Replaces the version listed in the same folder, if one is, and saves
    void Add(const std::filesystem::path& directory, HubInstall::Origin origin, Trinity::EngineManifest manifest);
    void Remove(const std::filesystem::path& directory);

    // The folder an installed version of this manifest is named, as its zip is
    [[nodiscard]] static std::string GetInstallName(const Trinity::EngineManifest& manifest);
    // As Hub names a version in what it says, Trinity 0.11.0 (209a33d), as GetVersionDescription names the running one
    [[nodiscard]] static std::string Describe(const Trinity::EngineManifest& manifest);
    // Two folders are the same where both are there and are one on the disk, and otherwise when their paths are, so a version is listed once however its folder was named
    [[nodiscard]] static bool IsSameFolder(const std::filesystem::path& left, const std::filesystem::path& right);
    // One that can open and make projects on this machine: installed or located, not missing or unreadable, and built for this platform and architecture
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