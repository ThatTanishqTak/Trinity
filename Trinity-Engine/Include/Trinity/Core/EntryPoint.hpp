#pragma once

#include "Trinity/Core/Application.hpp"

#if defined(TR_AGILITY_SDK_VERSION)
extern "C"
{
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion;
    __declspec(dllexport) extern const char* D3D12SDKPath;

    const unsigned int D3D12SDKVersion = TR_AGILITY_SDK_VERSION;
    const char* D3D12SDKPath = ".\\D3D12\\";
}
#endif

int main(int argc, char** argv)
{
    return Trinity::Main(argc, argv, &Trinity::CreateApplication);
}