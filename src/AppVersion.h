#pragma once

#include <string>

inline constexpr const wchar_t* kAppVersion = L"1.0.0";

inline constexpr const wchar_t* AppBuildTypeLabel()
{
#ifdef _DEBUG
    return L"Debug build";
#else
    return L"Release build";
#endif
}

inline std::wstring AppVersionLine()
{
    return std::wstring(L"VRec ") + kAppVersion;
}
