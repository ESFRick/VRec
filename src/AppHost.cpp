#include "AppHost.h"

#include "AppVersion.h"
#include "TextUtil.h"

#include <ShlObj.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace {

std::wstring BoolStatus(bool value)
{
    return value ? L"OK" : L"Unavailable";
}

std::wstring BoolEnabled(bool value)
{
    return value ? L"enabled" : L"disabled";
}

std::wstring CurrentTimestamp()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);

    std::wstringstream out;
    out << time.wYear
        << std::setfill(L'0') << std::setw(2) << time.wMonth
        << std::setw(2) << time.wDay << L"_"
        << std::setw(2) << time.wHour
        << std::setw(2) << time.wMinute
        << std::setw(2) << time.wSecond;
    return out.str();
}

void WriteUtf8File(const std::filesystem::path& path, const std::wstring& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Failed to open support report file");
    }
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };  // BOM so Notepad opens it as utf-8
    file.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    const std::string utf8 = WideToUtf8(text);
    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    // re-check the stream after writing - the user opens the report straight away, so a
    // half-written file is worse than a clear error.
    if (!file) {
        throw std::runtime_error("Failed to write support report file");
    }
}

DiagnosticLevel MinimumLogLevelFromSettings(const Settings& settings)
{
    return settings.advanced.logLevel == L"debug" ? DiagnosticLevel::Debug : DiagnosticLevel::Info;
}

bool SettingsEqual(const Settings& a, const Settings& b)
{
    return std::tie(a.version, a.language,
               a.obs.host, a.obs.port, a.obs.password, a.obsConfigured,
               a.overlay.hand, a.overlay.placement,
               a.advanced.logLevel)
        == std::tie(b.version, b.language,
               b.obs.host, b.obs.port, b.obs.password, b.obsConfigured,
               b.overlay.hand, b.overlay.placement,
               b.advanced.logLevel);
}

} // namespace

AppHost::AppHost()
    : settingsStore_(&diagnostics_)
{
}

AppHost::~AppHost()
{
    Shutdown();
}

bool AppHost::Initialize()
{
    {
        std::scoped_lock lock(mutex_);
        settings_ = settingsStore_.Load();
        initialized_ = true;
    }

    diagnostics_.LogInfo(L"VRec initialized: " + AppVersionLine() + L", " + AppBuildTypeLabel());
    const Settings settings = GetSettings();
    obs_.Start(settings.obs, settings.obsConfigured, &diagnostics_);
    overlay_.Start(
        settings,
        &diagnostics_,
        [this](bool shouldRecord) {
            return shouldRecord
                ? StartRecording()
                : StopRecording();
        },
        [this]() { return Status(); });
    return true;
}

void AppHost::Shutdown()
{
    if (!initialized_) {
        return;
    }

    obs_.Stop();
    overlay_.Stop();
    settingsStore_.Save(GetSettings());
    initialized_ = false;
}

Settings AppHost::GetSettings() const
{
    std::scoped_lock lock(mutex_);
    return settings_;
}

bool AppHost::ApplySettings(const Settings& settings)
{
    Settings normalized = settings;
    SettingsStore::Normalize(normalized);
    bool obsChanged = false;
    {
        std::scoped_lock lock(mutex_);
        if (SettingsEqual(settings_, normalized)) {
            return true;
        }
        obsChanged =
            settings_.obs.host != normalized.obs.host ||
            settings_.obs.port != normalized.obs.port ||
            settings_.obs.password != normalized.obs.password ||
            settings_.obsConfigured != normalized.obsConfigured;
        settings_ = normalized;
    }
    const bool saved = settingsStore_.Save(normalized);
    overlay_.UpdateSettings(normalized);
    if (obsChanged) {
        obs_.ApplySettings(
            normalized.obs,
            normalized.obsConfigured);
    }
    return saved;
}

bool AppHost::ResetSettings()
{
    return ApplySettings(SettingsStore::Defaults());
}

bool AppHost::ConfirmObsSettings(const ObsSettings& settings)
{
    Settings updated = GetSettings();
    updated.obs = settings;
    updated.obsConfigured = true;
    return ApplySettings(updated);
}

bool AppHost::StartRecording()
{
    return obs_.StartRecord();
}

bool AppHost::StopRecording()
{
    return obs_.StopRecord();
}

StatusSnapshot AppHost::Status() const
{
    StatusSnapshot status;
    status.steamVrReady = overlay_.SteamVrReady();
    status.headsetReady = overlay_.HeadsetReady();
    status.controllerReady = overlay_.ControllerReady();
    status.overlayReady = overlay_.OverlayReady();
    status.obsConnState = obs_.ConnectionState();
    status.recorderReady = status.obsConnState == ObsConnState::Connected;
    status.recorderState = obs_.Recording() ? RecorderState::Recording : RecorderState::Idle;
    status.recordingTime = obs_.Elapsed();
    status.lastError = obs_.LastError();
    if (status.lastError.empty()) {
        status.lastError = overlay_.LastError();
    }
    status.logLines = diagnostics_.LastLines(80, MinimumLogLevelFromSettings(GetSettings()));
    return status;
}

bool AppHost::TestObsConnection(
    const ObsSettings& settings,
    std::wstring& error,
    const std::atomic<bool>* cancel)
{
    return ObsClient::TestConnection(settings, error, cancel);
}

std::filesystem::path AppHost::ExportSupportReport()
{
    const Settings settings = GetSettings();
    const StatusSnapshot status = Status();
    const std::wstring timestamp = CurrentTimestamp();

    const std::filesystem::path appData = KnownFolderPath(FOLDERID_RoamingAppData);
    if (appData.empty()) {
        throw std::runtime_error("Roaming AppData folder was not found");
    }

    const std::filesystem::path reportDir = appData / L"VRec" / L"SupportReports" / timestamp;
    std::filesystem::create_directories(reportDir);

    std::wstringstream report;
    report << L"VRec support report\n"
           << L"Timestamp: " << timestamp << L"\n"
           << L"App version: " << kAppVersion << L"\n"
           << L"Build type: " << AppBuildTypeLabel() << L"\n"
           << L"\n"
           << L"Runtime status\n"
           << L"SteamVR: " << BoolStatus(status.steamVrReady) << L"\n"
           << L"Headset: " << BoolStatus(status.headsetReady) << L"\n"
           << L"Controller: " << BoolStatus(status.controllerReady) << L"\n"
           << L"Overlay: " << BoolStatus(status.overlayReady) << L"\n"
           << L"OBS connection: " << ToDisplayString(status.obsConnState) << L"\n"
           << L"Recorder state: " << ToDisplayString(status.recorderState) << L"\n"
           << L"Recording time seconds: " << status.recordingTime.count() << L"\n"
           << L"Last error: " << (status.lastError.empty() ? L"-" : status.lastError) << L"\n";

    std::wstringstream settingsSummary;
    settingsSummary << L"Safe settings summary\n"
                    << L"App version: " << kAppVersion << L"\n"
                    << L"Build type: " << AppBuildTypeLabel() << L"\n"
                    << L"OBS host: " << settings.obs.host << L"\n"
                    << L"OBS port: " << settings.obs.port << L"\n"
                    << L"OBS password set: " << BoolEnabled(!settings.obs.password.empty()) << L"\n"
                    << L"Overlay hand: " << ToDisplayString(settings.overlay.hand) << L"\n"
                    << L"Overlay placement: " << ToDisplayString(settings.overlay.placement) << L"\n";

    std::wstringstream log;
    log << L"Diagnostics log\n";
    for (const auto& line : diagnostics_.LastLines(400, DiagnosticLevel::Debug)) {
        log << line << L"\n";
    }

    WriteUtf8File(reportDir / L"report.txt", report.str());
    WriteUtf8File(reportDir / L"settings_summary.txt", settingsSummary.str());
    WriteUtf8File(reportDir / L"diagnostics.log", log.str());

    diagnostics_.LogInfo(L"Support report exported");
    return reportDir;
}
