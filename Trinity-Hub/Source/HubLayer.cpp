#include "HubLayer.hpp"

HubLayer::HubLayer() : Layer("Hub")
{

}

void HubLayer::OnAttach()
{
    TR_INFO("Trinity Hub (engine {})", Trinity::GetVersionString());
}
