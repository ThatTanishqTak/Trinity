#pragma once

#include "Pages/HubPage.hpp"

// The engine versions Hub knows, installed or located, which Hub lists from Installs.yaml once it has one
class InstallsPage final : public HubPage
{
public:
    [[nodiscard]] std::string_view GetName() const override { return "Installs"; }
    [[nodiscard]] const char* GetIcon() const override;
    void Draw() override;
};