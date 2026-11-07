#include "Pages/ProjectsPage.hpp"

#include <Trinity.hpp>

const char* ProjectsPage::GetIcon() const
{
    return Trinity::Icons::c_FolderOpen;
}

void ProjectsPage::Draw()
{
    HubPages::DrawTitle("Projects");
    HubPages::DrawEmpty("No projects yet", "Every project opened or created with Trinity is listed here, with the engine version it opens in.");
}