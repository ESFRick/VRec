#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

enum class Hand {
    Left,
    Right
};

enum class OverlayPlacement {
    UnderController,
    WristOutside
};

enum class OverlayPanelPage {
    Recording,
    Settings,
    PositionEdit
};

enum class OverlayHotspot {
    None,
    RecordButton,
    SettingsButton,
    SettingsBack,
    HideAngleMinus,
    HideAnglePlus,
    HideAngleSlider,
    HideAngleReset,
    EditPosition,
    ResetPosition
};

enum class RecorderState {
    Idle,
    Recording
};

inline constexpr int kOverlayHideAngleDefaultDegrees = 80;
inline constexpr int kOverlayHideAngleMinDegrees = 45;
inline constexpr int kOverlayHideAngleMaxDegrees = 120;
inline constexpr double kPositionOffsetMinMeters = -0.30;
inline constexpr double kPositionOffsetMaxMeters = 0.30;
inline constexpr double kPositionScaleDefault = 1.0;
inline constexpr double kPositionScaleMin = 0.60;
inline constexpr double kPositionScaleMax = 1.60;
inline constexpr double kPositionYawDefaultDegrees = 0.0;
inline constexpr double kPositionYawMinDegrees = -90.0;
inline constexpr double kPositionYawMaxDegrees = 90.0;

enum class ObsConnState {
    Disconnected,
    Connecting,
    Connected
};

enum class Language {
    English,
    Russian
};

struct ObsSettings {
    std::wstring host = L"127.0.0.1";
    int port = 4455;  // obs-websocket default
    std::wstring password;
};

struct OverlaySettings {
    Hand hand = Hand::Right;
    OverlayPlacement placement = OverlayPlacement::WristOutside;
    int hideAngleDegrees = kOverlayHideAngleDefaultDegrees;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double offsetZ = 0.0;
    double scale = kPositionScaleDefault;
    double yawDegrees = kPositionYawDefaultDegrees;
};

struct AdvancedSettings {
    std::wstring logLevel = L"info";
    bool closeToTray = true;
};

struct Settings {
    int version = 3;
    Language language = Language::English;
    ObsSettings obs;
    bool obsConfigured = false;
    OverlaySettings overlay;
    AdvancedSettings advanced;
};

struct StatusSnapshot {
    bool steamVrReady = false;
    bool headsetReady = false;
    bool controllerReady = false;
    bool overlayReady = false;
    bool recorderReady = false;
    ObsConnState obsConnState = ObsConnState::Disconnected;
    RecorderState recorderState = RecorderState::Idle;
    std::chrono::seconds recordingTime{0};
    std::wstring lastError;
    std::vector<std::wstring> logLines;
};

inline const wchar_t* ToDisplayString(Hand hand)
{
    return hand == Hand::Left ? L"Left" : L"Right";
}

inline const wchar_t* ToDisplayString(ObsConnState state)
{
    switch (state) {
    case ObsConnState::Disconnected:
        return L"Disconnected";
    case ObsConnState::Connecting:
        return L"Connecting";
    case ObsConnState::Connected:
        return L"Connected";
    default:
        return L"Unknown";
    }
}

inline const wchar_t* ToDisplayString(OverlayPlacement placement)
{
    switch (placement) {
    case OverlayPlacement::UnderController:
        return L"Under controller";
    case OverlayPlacement::WristOutside:
        return L"Wrist outside";
    default:
        return L"Unknown";
    }
}

inline const wchar_t* ToDisplayString(RecorderState state)
{
    switch (state) {
    case RecorderState::Idle:
        return L"Idle";
    case RecorderState::Recording:
        return L"Recording";
    default:
        return L"Unknown";
    }
}
