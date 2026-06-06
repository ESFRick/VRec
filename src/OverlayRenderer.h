#pragma once

#include "AppTypes.h"
#include "Diagnostics.h"

#include <cstdint>
#include <memory>

class OverlayRenderer {
public:
    static constexpr int Width = 512;
    static constexpr int Height = 256;
    static constexpr int ButtonFontHeight = -44;
    static constexpr int ButtonLetterSpacing = 1;

    OverlayRenderer();
    ~OverlayRenderer();

    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;

    bool Render(
        std::uint64_t overlayHandle,
        const StatusSnapshot& status,
        Language language,
        bool debugChecker,
        Diagnostics* diagnostics);
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
