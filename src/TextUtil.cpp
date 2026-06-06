#include "TextUtil.h"

#include <Windows.h>
#include <ShlObj.h>

#include <sstream>
#include <vector>

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

std::wstring HResultToString(long hr)
{
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    FormatMessageW(flags, nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message = buffer ? buffer : L"";
    if (buffer) {
        LocalFree(buffer);
    }

    std::wstringstream stream;
    stream << L"0x" << std::hex << static_cast<unsigned long>(hr);
    if (!message.empty()) {
        stream << L" " << message;
    }
    return stream.str();
}

std::wstring GetLastErrorString(unsigned long error)
{
    return HResultToString(HRESULT_FROM_WIN32(error));
}

std::filesystem::path KnownFolderPath(const GUID& folderId)
{
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &raw))) {
        return {};
    }

    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

std::filesystem::path ExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD size = 0;
    for (;;) {
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            return {};
        }
        if (size < buffer.size() - 1) {
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(std::wstring(buffer.data(), size));
}

std::filesystem::path ExecutableDirectory()
{
    const auto exe = ExecutablePath();
    return exe.empty() ? std::filesystem::current_path() : exe.parent_path();
}
