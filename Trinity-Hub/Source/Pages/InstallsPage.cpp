#include "Pages/InstallsPage.hpp"

#include <Trinity.hpp>

const char* InstallsPage::GetIcon() const
{
    return Trinity::Icons::c_Cube;
}

void InstallsPage::Draw()
{
    HubPages::DrawTitle("Installs");
    HubPages::DrawEmpty("No engine versions yet", "Every version of Trinity installed or located is listed here, with the platforms it exports games to.");
}