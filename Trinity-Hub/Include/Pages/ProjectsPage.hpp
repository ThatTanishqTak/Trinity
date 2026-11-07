#pragma once

#include "Pages/HubPage.hpp"

// The person's projects, each with the engine version it opens in, which Hub lists from Projects.yaml once it has one
class ProjectsPage final : public HubPage
{
public:
    [[nodiscard]] std::string_view GetName() const override { return "Projects"; }
    [[nodiscard]] const char* GetIcon() const override;
    void Draw() override;
};