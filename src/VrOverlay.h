#pragma once

#include "AppTypes.h"
#include "Diagnostics.h"
#include "OverlayRenderer.h"
#include "OverlayVisualState.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class VrOverlay {
public:
    using RecordingCommandCallback = std::function<bool(bool shouldRecord)>;
    using SettingsUpdateCallback = std::function<bool(const Settings& settings)>;
    using StatusProvider = std::function<StatusSnapshot()>;

    VrOverlay() = default;
    ~VrOverlay();

    VrOverlay(const VrOverlay&) = delete;
    VrOverlay& operator=(const VrOverlay&) = delete;

    bool Start(
        const Settings& settings,
        Diagnostics* diagnostics,
        RecordingCommandCallback recordingCommand,
        SettingsUpdateCallback settingsUpdateCommand,
        StatusProvider statusProvider);
    void Stop();
    void UpdateSettings(const Settings& settings);

    bool SteamVrReady() const { return steamVrReady_; }
    bool HeadsetReady() const { return headsetReady_; }
    bool ControllerReady() const { return controllerReady_; }
    bool OverlayReady() const { return overlayReady_; }
    std::wstring LastError() const;

private:
    void Run();
    bool Initialize();
    void ShutdownOpenVr();
    void Tick();
    void AttachToHand();
    void UpdateMainOverlayVisibility();
    void BeginMainOverlayFade(bool visible);
    void UpdateMainOverlayFade();
    bool PollSystemEvents();
    void PollEvents();
    bool InitializeInput();
    void UpdateControllerInteraction();
    bool HandleOverlayClick(float x, float y, bool steamVrInput, uint64_t inputSource, uint32_t controller);
    bool SetOverlayPage(OverlayPanelPage page);
    bool ApplyHideAngleDegrees(int value);
    void HideCursorOverlay();
    void LogInputErrorOnce(const wchar_t* action, int error);
    void RenderOverlay(const StatusSnapshot& status);
    void ApplyManifest();

    Settings SettingsSnapshot() const;
    void SetError(std::wstring message);

    mutable std::mutex mutex_;
    Settings settings_;
    Diagnostics* diagnostics_ = nullptr;
    RecordingCommandCallback recordingCommand_;
    SettingsUpdateCallback settingsUpdateCommand_;
    StatusProvider statusProvider_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> steamVrReady_{false};
    std::atomic<bool> headsetReady_{false};
    std::atomic<bool> controllerReady_{false};
    std::atomic<bool> overlayReady_{false};
    std::wstring lastError_;
    std::wstring lastLoggedOverlayError_;
    uint64_t overlayHandle_ = 0;
    OverlayRenderer renderer_;
    bool mainOverlayVisible_ = false;
    bool manifestApplied_ = false;
    bool manifestErrorLogged_ = false;
    bool reconnectBlockedUntilSteamVrExit_ = false;
    bool reconnectBlockLogged_ = false;
    bool attachDirty_ = true;
    uint32_t lastAttachedController_ = 0xFFFFFFFFu;
    uint64_t cursorOverlayHandle_ = 0;
    bool cursorOverlayVisible_ = false;
    bool inputReady_ = false;
    uint64_t actionSetHandle_ = 0;
    uint64_t pointerPoseAction_ = 0;
    uint64_t clickAction_ = 0;
    uint64_t hapticAction_ = 0;
    uint64_t leftHandSource_ = 0;
    uint64_t rightHandSource_ = 0;
    uint32_t lastInputErrorCode_ = 0;
    std::wstring lastInputErrorContext_;
    bool poseInactiveLogged_ = false;
    bool clickInactiveLogged_ = false;
    bool pointerPolicyLogged_ = false;
    bool cursorTransformFailureLogged_ = false;
    bool steamVrStrictRayLogged_ = false;
    bool legacyStrictRayLogged_ = false;
    bool legacyFallbackReasonLogged_ = false;
    bool lastLegacyLeftTrigger_ = false;
    bool lastLegacyRightTrigger_ = false;
    Hand lastPointerPolicyHand_ = Hand::Right;
    Hand lastAttachedHand_ = Hand::Right;
    OverlayPlacement lastAttachedPlacement_ = OverlayPlacement::WristOutside;
    OverlayRefreshState refreshState_;
    bool overlayImageFailureLogged_ = false;
    bool cursorVisible_ = false;
    bool cursorPressed_ = false;
    float cursorX_ = 0.0f;
    float cursorY_ = 0.0f;
    OverlayHotspot hoverHotspot_ = OverlayHotspot::None;
    OverlayHotspot pressedHotspot_ = OverlayHotspot::None;
    bool hideByAngleVisible_ = true;
    OverlayPanelPage overlayPage_ = OverlayPanelPage::Recording;
    bool mainOverlayFadeActive_ = false;
    bool mainOverlayFadeHideWhenDone_ = false;
    float mainOverlayAlpha_ = 1.0f;
    float mainOverlayFadeStartAlpha_ = 1.0f;
    float mainOverlayTargetAlpha_ = 1.0f;
    std::chrono::steady_clock::time_point mainOverlayFadeStart_{};
};
