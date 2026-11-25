#include "HelpLinks.hpp"

#include <Trinity.hpp>

#include <filesystem>
#include <string>

namespace HelpLinks
{
    void Open(std::string_view address)
    {
        if (const Trinity::Expected<void, std::string> l_Opened = Trinity::Process::OpenWithSystem(std::filesystem::path(address)); !l_Opened)
        {
            TR_WARN("Forge: {} could not be opened in a browser: {}", address, l_Opened.GetError());
        }
    }
}