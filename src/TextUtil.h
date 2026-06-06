#pragma once

#include <guiddef.h>

#include <filesystem>
#include <string>

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::wstring HResultToString(long hr);
std::wstring GetLastErrorString(unsigned long error);
std::filesystem::path KnownFolderPath(const GUID& folderId);
std::filesystem::path ExecutablePath();
std::filesystem::path ExecutableDirectory();
