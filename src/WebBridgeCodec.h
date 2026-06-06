#pragma once

#include "AppTypes.h"

#include <string>

enum class WebBridgeCommandType {
    GetSettings,
    ApplySettings,
    TestConnection,
    ExportSupportReport,
    ResetSettings,
    WindowMinimize,
    WindowClose,
    WindowDragStart,
    OpenRepository
};

struct WebBridgeCommand {
    WebBridgeCommandType type = WebBridgeCommandType::GetSettings;
    Settings settings;
    ObsSettings obsSettings;
};

bool ParseWebBridgeCommand(
    const std::wstring& message,
    const Settings& currentSettings,
    WebBridgeCommand& command,
    std::wstring& error);

std::wstring EncodeSettingsMessage(const Settings& settings);
std::wstring EncodeAboutMessage(const std::wstring& version, const std::wstring& buildType);
std::wstring EncodeStatusMessage(const StatusSnapshot& status);
std::wstring EncodeOperationResult(
    const char* type,
    bool ok,
    const std::wstring& error = {});
