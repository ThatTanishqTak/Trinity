#pragma once

#include "Trinity/Core/Export.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // Typed by extension. RmlUi reads each by its project path, so none has a loader
    namespace UIAssets
    {
        // An .rml document
        inline constexpr std::string_view c_DocumentType = "UIDocument";
        // An .rcss stylesheet
        inline constexpr std::string_view c_StyleSheetType = "UIStyleSheet";
        // A .ttf or .otf font face
        inline constexpr std::string_view c_FontType = "Font";
        // Attributes naming a clip or sound event, each a link as an image's src is
        inline constexpr std::string_view c_SoundHoverAttribute = "sound-hover";
        inline constexpr std::string_view c_SoundClickAttribute = "sound-click";

        [[nodiscard]] constexpr bool IsUIAsset(std::string_view assetType)
        {
            return assetType == c_DocumentType || assetType == c_StyleSheetType || assetType == c_FontType;
        }

        // A path from the root stays. Any other is from the linking file's folder. . and .. are resolved
        [[nodiscard]] TRINITY_API std::string ResolveLink(std::string_view from, std::string_view link);
        // Each file linked, once, in the order met. A word with a scheme, such as a URL, is not a path
        [[nodiscard]] TRINITY_API std::vector<std::string> FindLinks(std::string_view text, std::string_view path);
        // RmlUi 6.3 passes an @font-face's src to the file system unresolved, so stylesheets go through it
        [[nodiscard]] TRINITY_API std::string ResolveFontFaceSources(std::string_view text, std::string_view path);
    }
}