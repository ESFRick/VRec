#include "Diagnostics.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

const wchar_t* LevelText(DiagnosticLevel level)
{
    switch (level) {
    case DiagnosticLevel::Debug:
        return L"DEBUG";
    case DiagnosticLevel::Info:
        return L"INFO";
    case DiagnosticLevel::Warning:
        return L"WARN";
    case DiagnosticLevel::Error:
        return L"ERROR";
    default:
        return L"INFO";
    }
}

bool ShouldInclude(DiagnosticLevel level, DiagnosticLevel minimumLevel)
{
    return static_cast<int>(level) >= static_cast<int>(minimumLevel);
}

} // namespace

void Diagnostics::Log(std::wstring message)
{
    Log(DiagnosticLevel::Info, std::move(message));
}

void Diagnostics::Log(DiagnosticLevel level, std::wstring message)
{
    SYSTEMTIME time{};
    GetLocalTime(&time);

    std::wstringstream line;
    line << L"["
         << std::setfill(L'0') << std::setw(2) << time.wHour << L":"
         << std::setw(2) << time.wMinute << L":"
         << std::setw(2) << time.wSecond << L"] "
         << L"[" << LevelText(level) << L"] "
         << message;

    std::scoped_lock lock(mutex_);
    lines_.push_back(Entry{ level, line.str() });
    if (lines_.size() > 400) {
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - 400));
    }
}

void Diagnostics::LogDebug(std::wstring message)
{
    Log(DiagnosticLevel::Debug, std::move(message));
}

void Diagnostics::LogInfo(std::wstring message)
{
    Log(DiagnosticLevel::Info, std::move(message));
}

void Diagnostics::LogWarning(std::wstring message)
{
    Log(DiagnosticLevel::Warning, std::move(message));
}

void Diagnostics::LogError(std::wstring message)
{
    Log(DiagnosticLevel::Error, std::move(message));
}

std::vector<std::wstring> Diagnostics::LastLines(size_t maxLines, DiagnosticLevel minimumLevel) const
{
    std::scoped_lock lock(mutex_);

    std::vector<std::wstring> result;
    result.reserve(std::min(maxLines, lines_.size()));

    for (auto it = lines_.rbegin(); it != lines_.rend() && result.size() < maxLines; ++it) {
        if (ShouldInclude(it->level, minimumLevel)) {
            result.push_back(it->line);
        }
    }

    std::reverse(result.begin(), result.end());
    return result;
}
