#pragma once

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace Trinity
{
    namespace Tools
    {
        // Pictures on disk as ImGui textures, each read when first asked for and again once its file changes
        class Thumbnails
        {
        public:
            Thumbnails() = default;
            ~Thumbnails();

            Thumbnails(const Thumbnails&) = delete;
            Thumbnails& operator=(const Thumbnails&) = delete;

            // The PNG as a texture ImGui's renderer creates next frame, or none, with a warning, if unreadable
            [[nodiscard]] static std::unique_ptr<ImTextureData> Load(const std::filesystem::path& file);
            // The picture fitted into the box, or, with none ready to draw, the icon in its middle
            static void Draw(const ImTextureData* texture, const char* icon, const ImVec2& minimum, const ImVec2& maximum);

            // The file's picture once it can be drawn. None while it is being made, or where no file is
            [[nodiscard]] const ImTextureData* Get(const std::filesystem::path& file);
            // Each file is looked at again the next time it is asked for, as it may have been written since
            void Recheck();
            // Every picture let go of, as when what showed them is put away
            void Clear();
            // Between frames: pixels ImGui has uploaded are let go of, and so are textures it has destroyed
            void Update();

        private:
            struct Entry
            {
                std::filesystem::path File;
                // When the file read was written, or none where there was no file
                std::optional<std::filesystem::file_time_type> Time{};
                std::unique_ptr<ImTextureData> Texture{};
                bool Checked = false;
            };

            void Release(std::unique_ptr<ImTextureData>& texture);

            std::vector<Entry> m_Entries;
            std::vector<std::unique_ptr<ImTextureData>> m_Released;
            // Read this frame, which a page of pictures shown at once is spread over
            std::uint32_t m_Loads = 0;
        };
    }
}