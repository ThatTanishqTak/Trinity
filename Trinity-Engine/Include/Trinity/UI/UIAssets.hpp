#pragma once

#include "Trinity/Core/Export.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // The files a game's UI is made of, as the asset registry types them by their extensions. RmlUi reads each by its path in the project, through the virtual file system, so none has a loader, and a scene's UIDocuments name their documents without the asset manager holding them
    namespace UIAssets
    {
        // An .rml document
        inline constexpr std::string_view c_DocumentType = "UIDocument";
        // An .rcss stylesheet
        inline constexpr std::string_view c_StyleSheetType = "UIStyleSheet";
        // A .ttf or .otf font face
        inline constexpr std::string_view c_FontType = "Font";
        // An element's attributes naming the clip or sound event played as the pointer enters it, or Navigate gives it the focus, and as a click passes it, each a link as an image's src is
        inline constexpr std::string_view c_SoundHoverAttribute = "sound-hover";
        inline constexpr std::string_view c_SoundClickAttribute = "sound-click";

        [[nodiscard]] constexpr bool IsUIAsset(std::string_view assetType)
        {
            return assetType == c_DocumentType || assetType == c_StyleSheetType || assetType == c_FontType;
        }

        // A link from a document or stylesheet to another file, as RmlUi resolves it: a path from the root of the virtual file system stays as it is, and any other is from the linking file's folder, so files find what lies beside them wherever their folder is. Either way its . and .. are resolved, which the file system does not take, and a .. above the root stays at the root
        [[nodiscard]] TRINITY_API std::string ResolveLink(std::string_view from, std::string_view link);
        // The files a document or stylesheet links to, resolved from its path, each once in the order met: every quoted, bracketed or bare word ending in a UI file's, an image's or a sound's extension, as a link's href, an img's src, an @font-face's or @spritesheet's src, a decorator's image and a sound attribute name them. A word with a scheme, such as a URL, is not a path
        [[nodiscard]] TRINITY_API std::vector<std::string> FindLinks(std::string_view text, std::string_view path);
        // The stylesheet text with each @font-face's src resolved as ResolveLink resolves a link from the file, quoted. RmlUi 6.3 passes an @font-face's src to the file system as it is written, where it resolves every other link from the file it is in, so the UI reads stylesheets through this, and a font beside its stylesheet is found as an image beside it is. Text without an @font-face comes back as it is
        [[nodiscard]] TRINITY_API std::string ResolveFontFaceSources(std::string_view text, std::string_view path);
    }
}