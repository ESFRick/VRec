#pragma once

#include "AppTypes.h"

#include <chrono>
#include <optional>
#include <string>
#include <tuple>

struct OverlayVisualState {
    RecorderState recorderState = RecorderState::Idle;
    ObsConnState obsConnState = ObsConnState::Disconnected;
    std::chrono::seconds recordingTime{0};
    std::wstring error;
    Language language = Language::English;
    OverlayPanelPage panelPage = OverlayPanelPage::Recording;
    int hideAngleDegrees = kOverlayHideAngleDefaultDegrees;
    OverlayHotspot hoverHotspot = OverlayHotspot::None;
    OverlayHotspot pressedHotspot = OverlayHotspot::None;
    bool debugChecker = false;
};

inline bool operator==(
    const OverlayVisualState& left,
    const OverlayVisualState& right)
{
    return std::tie(
               left.recorderState,
               left.obsConnState,
               left.recordingTime,
               left.error,
               left.language,
               left.panelPage,
               left.hideAngleDegrees,
               left.hoverHotspot,
               left.pressedHotspot,
               left.debugChecker) ==
        std::tie(
               right.recorderState,
               right.obsConnState,
               right.recordingTime,
               right.error,
               right.language,
               right.panelPage,
               right.hideAngleDegrees,
               right.hoverHotspot,
               right.pressedHotspot,
               right.debugChecker);
}

inline bool operator!=(
    const OverlayVisualState& left,
    const OverlayVisualState& right)
{
    return !(left == right);
}

inline OverlayVisualState MakeOverlayVisualState(
    const StatusSnapshot& status,
    const Settings& settings,
    OverlayPanelPage panelPage,
    OverlayHotspot hoverHotspot,
    OverlayHotspot pressedHotspot,
    bool debugChecker)
{
    const bool obsConnected = status.obsConnState == ObsConnState::Connected;
    return {
        status.recorderState,
        obsConnected ? ObsConnState::Connected : ObsConnState::Disconnected,
        status.recorderState == RecorderState::Recording
            ? status.recordingTime
            : std::chrono::seconds{0},
        obsConnected ? status.lastError : std::wstring(),
        settings.language,
        panelPage,
        settings.overlay.hideAngleDegrees,
        hoverHotspot,
        pressedHotspot,
        debugChecker,
    };
}

inline bool OverlayNeedsRender(
    const std::optional<OverlayVisualState>& previous,
    const OverlayVisualState& current,
    bool dirty)
{
    return dirty || !previous || *previous != current;
}

class OverlayRefreshState {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr auto MinSubmitInterval = std::chrono::milliseconds(100);
    static constexpr auto DynamicUpdateDelay = std::chrono::milliseconds(140);
    static constexpr auto RetryDelay = std::chrono::milliseconds(250);

    void SetDesired(
        const OverlayVisualState& state,
        Clock::time_point now)
    {
        if (!desired_ || *desired_ != state) {
            desired_ = state;
            desiredChangedAt_ = now;
        }
    }

    bool ShouldSubmit(Clock::time_point now) const
    {
        if (!desired_ || loading_ || now < retryAfter_) {
            return false;
        }
        if (displayed_ && *displayed_ == *desired_) {
            return false;
        }
        if (lastSubmitAt_ != Clock::time_point{} &&
            now - lastSubmitAt_ < MinSubmitInterval) {
            return false;
        }
        if (IsDynamicOnlyChange(displayed_, *desired_) &&
            now - desiredChangedAt_ < DynamicUpdateDelay) {
            return false;
        }
        return true;
    }

    void MarkSubmitted(
        const OverlayVisualState& state,
        Clock::time_point now)
    {
        submitted_ = state;
        loading_ = true;
        lastSubmitAt_ = now;
    }

    void MarkImageLoaded()
    {
        if (submitted_) {
            displayed_ = submitted_;
        }
        submitted_.reset();
        loading_ = false;
    }

    void MarkDisplayed(
        const OverlayVisualState& state,
        Clock::time_point now)
    {
        displayed_ = state;
        submitted_.reset();
        loading_ = false;
        lastSubmitAt_ = now;
        retryAfter_ = {};
    }

    void MarkImageFailed(Clock::time_point now)
    {
        submitted_.reset();
        loading_ = false;
        retryAfter_ = now + RetryDelay;
    }

    void Reset()
    {
        desired_.reset();
        submitted_.reset();
        displayed_.reset();
        loading_ = false;
        retryAfter_ = {};
        lastSubmitAt_ = {};
        desiredChangedAt_ = {};
    }

private:
    static bool IsDynamicOnlyChange(
        const std::optional<OverlayVisualState>& displayed,
        const OverlayVisualState& desired)
    {
        if (!displayed) {
            return false;
        }
        return std::tie(
                   displayed->recorderState,
                   displayed->obsConnState,
                   displayed->recordingTime,
                   displayed->error,
                   displayed->language,
                   displayed->panelPage,
                   displayed->hoverHotspot,
                   displayed->pressedHotspot,
                   displayed->debugChecker) ==
            std::tie(
                   desired.recorderState,
                   desired.obsConnState,
                   desired.recordingTime,
                   desired.error,
                   desired.language,
                   desired.panelPage,
                   desired.hoverHotspot,
                   desired.pressedHotspot,
                   desired.debugChecker) &&
            displayed->hideAngleDegrees != desired.hideAngleDegrees;
    }

    std::optional<OverlayVisualState> desired_;
    std::optional<OverlayVisualState> submitted_;
    std::optional<OverlayVisualState> displayed_;
    bool loading_ = false;
    Clock::time_point retryAfter_{};
    Clock::time_point lastSubmitAt_{};
    Clock::time_point desiredChangedAt_{};
};
