#pragma once

#include <mutex>
#include <string>
#include <vector>

enum class DiagnosticLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

class Diagnostics {
public:
    void Log(std::wstring message);
    void Log(DiagnosticLevel level, std::wstring message);
    void LogDebug(std::wstring message);
    void LogInfo(std::wstring message);
    void LogWarning(std::wstring message);
    void LogError(std::wstring message);
    std::vector<std::wstring> LastLines(size_t maxLines = 80, DiagnosticLevel minimumLevel = DiagnosticLevel::Info) const;

private:
    struct Entry {
        DiagnosticLevel level = DiagnosticLevel::Info;
        std::wstring line;
    };

    mutable std::mutex mutex_;
    std::vector<Entry> lines_;
};
