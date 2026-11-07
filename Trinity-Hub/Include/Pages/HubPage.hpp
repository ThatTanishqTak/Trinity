#pragma once

#include <string_view>

// One of Hub's pages, which the sidebar lists by its icon and name and draws beside itself while it is chosen, as Unity Hub's are
class HubPage
{
public:
    virtual ~HubPage() = default;

    [[nodiscard]] virtual std::string_view GetName() const = 0;
    [[nodiscard]] virtual const char* GetIcon() const = 0;
    virtual void Draw() = 0;
};

namespace HubPages
{
    // The page's name, large and bold, as every page begins
    void DrawTitle(std::string_view title);
    // What a page with nothing in it says, centred in the space below its title
    void DrawEmpty(std::string_view headline, std::string_view detail);
    // A section's heading in bold, with space before it
    void DrawSection(std::string_view title);
    // A setting's label, with what follows it on the same line, after a column every label shares
    void DrawLabel(std::string_view label);
}