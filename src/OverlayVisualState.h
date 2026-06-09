#pragma once

#include "AppTypes.h"

#include <chrono>
#include <optional>
#include <string>
#include <tuple>

struct OverlayVisualState {
    RecorderState recorderState = RecorderState::Idle;
    ObsConnState obsConnState = ObsConnState::Disconnected;
    std::wstring error;
    Language language = Language::English;
    OverlayPanelPage panelPage = OverlayPanelPage::Recording;
    int hideAngleDegrees = kOverlayHideAngleDefaultDegrees;
    bool debugChecker = false;
};

inline bool operator==(
    const OverlayVisualState& left,
    const OverlayVisualState& right)
{
    return std::tie(
               left.recorderState,
               left.obsConnState,
               left.error,
               left.language,
               left.panelPage,
               left.hideAngleDegrees,
               left.debugChecker) ==
        std::tie(
               right.recorderState,
               right.obsConnState,
               right.error,
               right.language,
               right.panelPage,
               right.hideAngleDegrees,
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
    bool debugChecker)
{
    return {
        status.recorderState,
        status.obsConnState,
        status.lastError,
        settings.language,
        panelPage,
        settings.overlay.hideAngleDegrees,
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

    void SetDesired(const OverlayVisualState& state)
    {
        desired_ = state;
    }

    bool ShouldSubmit(Clock::time_point now) const
    {
        if (!desired_ || loading_ || now < retryAfter_) {
            return false;
        }
        return !displayed_ || *displayed_ != *desired_;
    }

    void MarkSubmitted(
        const OverlayVisualState& state,
        Clock::time_point)
    {
        submitted_ = state;
        loading_ = true;
    }

    void MarkImageLoaded()
    {
        if (submitted_) {
            displayed_ = submitted_;
        }
        submitted_.reset();
        loading_ = false;
    }

    void MarkImageFailed(Clock::time_point now)
    {
        submitted_.reset();
        loading_ = false;
        retryAfter_ = now + std::chrono::milliseconds(250);
    }

    void Reset()
    {
        desired_.reset();
        submitted_.reset();
        displayed_.reset();
        loading_ = false;
        retryAfter_ = {};
    }

private:
    std::optional<OverlayVisualState> desired_;
    std::optional<OverlayVisualState> submitted_;
    std::optional<OverlayVisualState> displayed_;
    bool loading_ = false;
    Clock::time_point retryAfter_{};
};
