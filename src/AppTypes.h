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

enum class RecorderState {
    Idle,
    Recording
};

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
};

struct AdvancedSettings {
    std::wstring logLevel = L"info";
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
