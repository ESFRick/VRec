#pragma once

#include "AppTypes.h"
#include "Diagnostics.h"
#include "ObsClient.h"
#include "SettingsStore.h"
#include "VrOverlay.h"

#include <atomic>
#include <mutex>
#include <filesystem>
#include <vector>

class AppHost {
public:
    AppHost();
    ~AppHost();

    bool Initialize();
    void Shutdown();

    Settings GetSettings() const;
    bool ApplySettings(const Settings& settings);
    bool ResetSettings();
    bool ConfirmObsSettings(const ObsSettings& settings);

    bool StartRecording();
    bool StopRecording();

    StatusSnapshot Status() const;
    std::filesystem::path ExportSupportReport();
    bool TestObsConnection(
        const ObsSettings& settings,
        std::wstring& error,
        const std::atomic<bool>* cancel = nullptr);
    Diagnostics& Log() { return diagnostics_; }

private:
    mutable std::mutex mutex_;
    Diagnostics diagnostics_;
    SettingsStore settingsStore_;
    Settings settings_;
    ObsClient obs_;
    VrOverlay overlay_;
    bool initialized_ = false;
};
