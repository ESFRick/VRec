#include "OverlayRenderer.h"

#include "Localization.h"
#include "TextUtil.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <openvr.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace color {
constexpr COLORREF Panel = RGB(28, 26, 24);
constexpr COLORREF PanelEdge = RGB(53, 50, 47);
constexpr COLORREF TextHigh = RGB(234, 228, 219);
constexpr COLORREF TextMid = RGB(157, 149, 138);
constexpr COLORREF TextDim = RGB(107, 100, 90);
constexpr COLORREF Accent = RGB(214, 143, 87);
constexpr COLORREF Ready = RGB(111, 174, 126);
constexpr COLORREF Warn = RGB(199, 154, 78);
constexpr COLORREF WarnSoft = RGB(59, 49, 34);
constexpr COLORREF Danger = RGB(224, 122, 112);
constexpr COLORREF Record = RGB(216, 66, 58);
constexpr COLORREF RecordInk = RGB(255, 255, 255);
constexpr COLORREF RecordSoft = RGB(58, 32, 29);
constexpr COLORREF Inset = RGB(19, 18, 16);
constexpr COLORREF Border = RGB(61, 58, 54);
} // namespace color

RECT GearButtonRect()
{
    return RECT{ 458, 14, 492, 48 };
}

RECT SettingsBackButtonRect()
{
    return RECT{ 20, 16, 54, 50 };
}

RECT HideAngleMinusRect()
{
    return RECT{ 32, 118, 76, 162 };
}

RECT HideAnglePlusRect()
{
    return RECT{ 436, 118, 480, 162 };
}

RECT HideAngleSliderRect()
{
    return RECT{ 96, 124, 416, 156 };
}

RECT HideAngleResetRect()
{
    return RECT{ 352, 192, 480, 230 };
}

enum class PanelState {
    Ready,
    Recording,
    Connecting,
    Offline,
    Error
};

class GdiObject {
public:
    explicit GdiObject(HGDIOBJ handle = nullptr)
        : handle_(handle)
    {
    }

    ~GdiObject()
    {
        if (handle_) {
            DeleteObject(handle_);
        }
    }

    GdiObject(const GdiObject&) = delete;
    GdiObject& operator=(const GdiObject&) = delete;

    GdiObject(GdiObject&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    GdiObject& operator=(GdiObject&& other) noexcept
    {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    HGDIOBJ get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    void reset(HGDIOBJ handle = nullptr)
    {
        if (handle_ && handle_ != handle) {
            DeleteObject(handle_);
        }
        handle_ = handle;
    }

private:
    HGDIOBJ handle_ = nullptr;
};

class ScreenDc {
public:
    ScreenDc()
        : dc_(GetDC(nullptr))
    {
    }

    ~ScreenDc()
    {
        if (dc_) {
            ReleaseDC(nullptr, dc_);
        }
    }

    HDC get() const { return dc_; }
    explicit operator bool() const { return dc_ != nullptr; }

private:
    HDC dc_ = nullptr;
};

class MemoryDc {
public:
    explicit MemoryDc(HDC compatibleWith)
        : dc_(CreateCompatibleDC(compatibleWith))
    {
    }

    ~MemoryDc()
    {
        if (dc_) {
            DeleteDC(dc_);
        }
    }

    HDC get() const { return dc_; }
    explicit operator bool() const { return dc_ != nullptr; }

private:
    HDC dc_ = nullptr;
};

class DcSelection {
public:
    DcSelection(HDC dc, HGDIOBJ object)
        : dc_(dc)
        , previous_(object ? SelectObject(dc, object) : nullptr)
    {
    }

    ~DcSelection()
    {
        if (previous_ && previous_ != HGDI_ERROR) {
            SelectObject(dc_, previous_);
        }
    }

    DcSelection(const DcSelection&) = delete;
    DcSelection& operator=(const DcSelection&) = delete;

private:
    HDC dc_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

COLORREF Blend(COLORREF base, COLORREF overlay, double amount)
{
    const auto channel = [amount](int from, int to) {
        return static_cast<int>(from + (to - from) * amount + 0.5);
    };
    return RGB(
        channel(GetRValue(base), GetRValue(overlay)),
        channel(GetGValue(base), GetGValue(overlay)),
        channel(GetBValue(base), GetBValue(overlay)));
}

std::wstring OverlayErrorToString(vr::EVROverlayError error)
{
    const char* name = vr::VROverlay()
        ? vr::VROverlay()->GetOverlayErrorNameFromEnum(error)
        : nullptr;
    std::stringstream stream;
    stream << (name ? name : "VROverlayError")
           << " (" << static_cast<int>(error) << ")";
    return Utf8ToWide(stream.str());
}

std::wstring FormatHResult(HRESULT hr)
{
    std::wstringstream stream;
    stream << L"0x" << std::hex << static_cast<unsigned long>(hr);
    return stream.str();
}

void LogOverlayError(
    Diagnostics* diagnostics,
    const wchar_t* action,
    vr::EVROverlayError error)
{
    if (diagnostics && error != vr::VROverlayError_None) {
        diagnostics->LogWarning(
            std::wstring(action) + L" failed: " +
            OverlayErrorToString(error));
    }
}

void DrawRoundedRect(
    HDC dc,
    const RECT& rect,
    int radius,
    COLORREF fill,
    COLORREF border,
    int borderWidth)
{
    GdiObject brush(CreateSolidBrush(fill));
    GdiObject pen(borderWidth > 0
        ? CreatePen(PS_SOLID, borderWidth, border)
        : nullptr);
    DcSelection brushSelection(dc, brush.get());
    DcSelection penSelection(
        dc,
        pen ? pen.get() : GetStockObject(NULL_PEN));
    RoundRect(
        dc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        radius,
        radius);
}


void DrawRoundedOutline(
    HDC dc,
    const RECT& rect,
    int radius,
    COLORREF border,
    int borderWidth)
{
    GdiObject pen(CreatePen(PS_SOLID, borderWidth, border));
    DcSelection brushSelection(dc, GetStockObject(NULL_BRUSH));
    DcSelection penSelection(dc, pen.get());
    RoundRect(
        dc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        radius,
        radius);
}

void DrawDebugChecker(HDC dc)
{
    constexpr int cellSize = 32;
    GdiObject magenta(CreateSolidBrush(RGB(255, 0, 255)));
    GdiObject green(CreateSolidBrush(RGB(0, 255, 64)));
    for (int y = 0; y < OverlayRenderer::Height; y += cellSize) {
        for (int x = 0; x < OverlayRenderer::Width; x += cellSize) {
            RECT cell{
                x,
                y,
                std::min(x + cellSize, OverlayRenderer::Width),
                std::min(y + cellSize, OverlayRenderer::Height),
            };
            FillRect(
                dc,
                &cell,
                reinterpret_cast<HBRUSH>(
                    ((x / cellSize) + (y / cellSize)) % 2 == 0
                        ? magenta.get()
                        : green.get()));
        }
    }
}

GdiObject MakeFont(int height, int weight, const wchar_t* family)
{
    return GdiObject(CreateFontW(
        height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        family));
}

PanelState GetPanelState(const StatusSnapshot& status)
{
    if (status.recorderState == RecorderState::Recording) {
        return PanelState::Recording;
    }
    if (status.obsConnState == ObsConnState::Connected) {
        return PanelState::Ready;
    }
    return PanelState::Offline;
}


std::wstring FormatRecordingTime(std::chrono::seconds elapsed)
{
    const auto totalSeconds = std::max<long long>(0, elapsed.count());
    const long long hours = totalSeconds / 3600;
    const long long minutes = (totalSeconds / 60) % 60;
    const long long seconds = totalSeconds % 60;

    std::wstringstream stream;
    stream << std::setfill(L'0');
    if (hours > 0) {
        stream << hours << L":"
               << std::setw(2) << minutes << L":"
               << std::setw(2) << seconds;
    } else {
        stream << std::setw(2) << minutes << L":"
               << std::setw(2) << seconds;
    }
    return stream.str();
}

void ApplyHoverStyle(
    bool hovered,
    bool pressed,
    COLORREF& fill,
    COLORREF& border,
    COLORREF& text,
    int& borderWidth)
{
    if (pressed) {
        fill = Blend(fill, color::Inset, 0.28);
        border = Blend(border, color::TextHigh, 0.32);
        text = Blend(text, color::TextHigh, 0.16);
        borderWidth = std::max(borderWidth, 2);
        return;
    }
    if (hovered) {
        fill = Blend(fill, color::Accent, 0.13);
        border = Blend(border, color::Accent, 0.55);
        text = Blend(text, color::TextHigh, 0.18);
        borderWidth = std::max(borderWidth, 2);
    }
}

void DrawIconButton(
    HDC dc,
    const RECT& rect,
    bool hovered,
    bool pressed,
    const wchar_t* glyph,
    int fontHeight,
    COLORREF glyphColor,
    int glyphTopOffset = 0)
{
    COLORREF fill = color::Panel;
    COLORREF border = color::PanelEdge;
    COLORREF text = glyphColor;
    int borderWidth = 1;
    ApplyHoverStyle(hovered, pressed, fill, border, text, borderWidth);
    DrawRoundedRect(dc, rect, 8, fill, border, borderWidth);

    GdiObject font = MakeFont(fontHeight, FW_NORMAL, L"Segoe UI Symbol");
    if (!font) {
        font = MakeFont(fontHeight, FW_NORMAL, L"Segoe UI");
    }
    if (font) {
        SelectObject(dc, font.get());
        SetTextColor(dc, text);
        RECT glyphRect{ rect.left, rect.top + glyphTopOffset, rect.right, rect.bottom + glyphTopOffset };
        DrawTextW(
            dc,
            glyph,
            -1,
            &glyphRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

bool DrawRecordingPanel(
    HDC dc,
    const StatusSnapshot& status,
    Language language,
    OverlayHotspot hoverHotspot,
    OverlayHotspot pressedHotspot,
    Diagnostics* diagnostics)
{
    GdiObject titleFont = MakeFont(-21, FW_BOLD, L"Segoe UI");
    GdiObject chipFont = MakeFont(-15, FW_SEMIBOLD, L"Segoe UI");
    GdiObject buttonLargeFont = MakeFont(
        OverlayRenderer::ButtonFontHeight,
        FW_BOLD,
        L"Segoe UI");
    GdiObject buttonMediumFont = MakeFont(-23, FW_BOLD, L"Segoe UI");
    GdiObject hintFont = MakeFont(-14, FW_NORMAL, L"Segoe UI");
    GdiObject timerFont = MakeFont(-30, FW_BOLD, L"Segoe UI");
    if (!titleFont || !chipFont ||
        !buttonLargeFont || !buttonMediumFont || !hintFont ||
        !timerFont) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay font creation failed");
        }
        return false;
    }

    const PanelState panel = GetPanelState(status);
    const bool russian = language == Language::Russian;
    const bool recording = panel == PanelState::Recording;
    const bool showHint = panel == PanelState::Offline;

    DrawRoundedRect(
        dc,
        RECT{0, 0, OverlayRenderer::Width, OverlayRenderer::Height},
        18,
        color::Panel,
        color::Panel,
        0);
    {
        DcSelection brushSelection(dc, GetStockObject(NULL_BRUSH));
        GdiObject border(CreatePen(PS_SOLID, 1, color::PanelEdge));
        DcSelection penSelection(dc, border.get());
        RoundRect(
            dc,
            1,
            1,
            OverlayRenderer::Width - 1,
            OverlayRenderer::Height - 1,
            18,
            18);
    }

    SetBkMode(dc, TRANSPARENT);
    DcSelection fontSelection(dc, titleFont.get());

    SIZE vSize{};
    GetTextExtentPoint32W(dc, L"V", 1, &vSize);
    RECT brand{22, 16, 220, 48};
    SetTextColor(dc, color::Accent);
    DrawTextW(dc, L"V", -1, &brand, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    brand.left += vSize.cx;
    SetTextColor(dc, color::TextHigh);
    DrawTextW(dc, L"Rec", -1, &brand, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    constexpr int chipRight = 446;
    constexpr int chipCenterY = 32;
    constexpr int chipHeight = 26;
    constexpr int chipPadding = 11;
    constexpr int chipTop = chipCenterY - chipHeight / 2;
    constexpr int chipBottom = chipTop + chipHeight;

    if (recording) {
        const wchar_t* recordText =
            russian ? L"\u25CF \u0417\u0430\u043F\u0438\u0441\u044C" : L"\u25CF Recording";

        SIZE recordSize{};
        SelectObject(dc, chipFont.get());
        GetTextExtentPoint32W(
            dc,
            recordText,
            static_cast<int>(wcslen(recordText)),
            &recordSize);
        const int chipLeft = chipRight - (recordSize.cx + chipPadding * 2);

        DrawRoundedRect(
            dc,
            RECT{chipLeft, chipTop, chipRight, chipBottom},
            8,
            color::RecordSoft,
            color::RecordSoft,
            0);
        SetTextColor(dc, color::Record);
        RECT recordRect{
            chipLeft + chipPadding,
            chipTop,
            chipRight - chipPadding,
            chipBottom
        };
        SelectObject(dc, chipFont.get());
        DrawTextW(
            dc,
            recordText,
            -1,
            &recordRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        COLORREF tone = color::Warn;
        const wchar_t* label = russian ? L"● OBS не в сети" : L"● OBS Offline";
        switch (panel) {
        case PanelState::Ready:
            tone = color::Ready;
            label = russian ? L"● Готов" : L"● Ready";
            break;
        default:
            break;
        }

        SelectObject(dc, chipFont.get());
        SIZE size{};
        GetTextExtentPoint32W(
            dc,
            label,
            static_cast<int>(wcslen(label)),
            &size);
        const int chipLeft = chipRight - (size.cx + chipPadding * 2);
        DrawRoundedRect(
            dc,
            RECT{chipLeft, chipTop, chipRight, chipBottom},
            8,
            color::Panel,
            Blend(color::Panel, tone, 0.45),
            1);
        SetTextColor(dc, tone);
        RECT labelRect{
            chipLeft + chipPadding,
            chipTop,
            chipRight - chipPadding,
            chipBottom,
        };
        DrawTextW(
            dc,
            label,
            -1,
            &labelRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    const RECT buttonRect{22, 62, 490, showHint ? 200 : 236};
    COLORREF buttonFill = color::Inset;
    COLORREF buttonBorder = color::Border;
    COLORREF buttonText = color::TextDim;
    int buttonBorderWidth = 1;
    HGDIOBJ buttonFont = buttonMediumFont.get();
    std::wstring buttonLabel;
    const bool recordButtonInteractive =
        panel == PanelState::Ready || panel == PanelState::Recording;
    const bool recordHovered =
        recordButtonInteractive && hoverHotspot == OverlayHotspot::RecordButton;
    const bool recordPressed =
        recordButtonInteractive && pressedHotspot == OverlayHotspot::RecordButton;

    switch (panel) {
    case PanelState::Ready:
        buttonFill = color::Record;
        buttonBorder = color::Record;
        buttonBorderWidth = 0;
        buttonText = color::RecordInk;
        buttonFont = buttonLargeFont.get();
        buttonLabel = std::wstring(L"●  ") + Tr(language, TextId::OverlayRec);
        break;
    case PanelState::Recording:
        buttonFill = color::RecordSoft;
        buttonBorder = color::Record;
        buttonBorderWidth = 2;
        buttonText = color::Record;
        buttonFont = buttonLargeFont.get();
        buttonLabel = std::wstring(L"■  ") + Tr(language, TextId::OverlayStop);
        break;
    case PanelState::Connecting:
    case PanelState::Error:
    default:
        buttonFill = color::WarnSoft;
        buttonBorder = color::Warn;
        buttonText = color::Warn;
        buttonLabel = russian ? L"OBS не в сети" : L"OBS Offline";
        break;
    }

    ApplyHoverStyle(
        recordHovered,
        recordPressed,
        buttonFill,
        buttonBorder,
        buttonText,
        buttonBorderWidth);

    DrawRoundedRect(
        dc,
        buttonRect,
        14,
        buttonFill,
        buttonBorder,
        buttonBorderWidth);
    SelectObject(dc, buttonFont);
    SetTextColor(dc, buttonText);
    SetTextCharacterExtra(
        dc,
        panel == PanelState::Ready || panel == PanelState::Recording
            ? OverlayRenderer::ButtonLetterSpacing
            : 1);
    RECT mutableButtonRect = buttonRect;
    if (recording) {
        mutableButtonRect.bottom = 150;
    }
    DrawTextW(
        dc,
        buttonLabel.c_str(),
        -1,
        &mutableButtonRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextCharacterExtra(dc, 0);

    if (recording) {
        const std::wstring elapsed = FormatRecordingTime(status.recordingTime);
        RECT timerPill{ 166, 154, 346, 202 };
        DrawRoundedRect(
            dc,
            timerPill,
            12,
            Blend(color::RecordSoft, color::Inset, 0.30),
            Blend(color::Record, color::TextHigh, recordHovered ? 0.25 : 0.08),
            1);

        SelectObject(dc, timerFont.get());
        SetTextColor(dc, color::TextHigh);
        RECT timerText{ timerPill.left, timerPill.top - 1, timerPill.right, timerPill.bottom - 1 };
        DrawTextW(
            dc,
            elapsed.c_str(),
            -1,
            &timerText,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (showHint) {
        const wchar_t* hint = russian
            ? L"Повторное подключение..."
            : L"Trying to reconnect...";
        SelectObject(dc, hintFont.get());
        SetTextColor(dc, color::TextDim);
        RECT hintRect{22, 206, 490, 238};
        DrawTextW(
            dc,
            hint,
            -1,
            &hintRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    DrawIconButton(
        dc,
        GearButtonRect(),
        hoverHotspot == OverlayHotspot::SettingsButton,
        pressedHotspot == OverlayHotspot::SettingsButton,
        L"\u2699",
        -24,
        color::TextMid,
        -1);
    return true;
}

void DrawSlider(HDC dc, const RECT& track, int value)
{
    constexpr int trackHeight = 6;
    const int centerY = (track.top + track.bottom) / 2;
    const int trackLeft = track.left + 10;
    const int trackRight = track.right - 10;
    const int range = kOverlayHideAngleMaxDegrees - kOverlayHideAngleMinDegrees;
    const double t = static_cast<double>(value - kOverlayHideAngleMinDegrees) / static_cast<double>(range);
    const int knobX = static_cast<int>(trackLeft + (trackRight - trackLeft) * std::clamp(t, 0.0, 1.0) + 0.5);

    DrawRoundedRect(
        dc,
        RECT{ trackLeft, centerY - trackHeight / 2, trackRight, centerY + trackHeight / 2 },
        5,
        color::Inset,
        color::Border,
        1);
    DrawRoundedRect(
        dc,
        RECT{ trackLeft, centerY - trackHeight / 2, knobX, centerY + trackHeight / 2 },
        5,
        color::Accent,
        color::Accent,
        0);
    DrawRoundedRect(
        dc,
        RECT{ knobX - 8, centerY - 13, knobX + 8, centerY + 13 },
        8,
        color::TextHigh,
        color::TextHigh,
        0);
}

bool DrawSettingsPanel(
    HDC dc,
    const Settings& settings,
    OverlayHotspot hoverHotspot,
    OverlayHotspot pressedHotspot,
    Diagnostics* diagnostics)
{
    GdiObject titleFont = MakeFont(-21, FW_BOLD, L"Segoe UI");
    GdiObject labelFont = MakeFont(-20, FW_SEMIBOLD, L"Segoe UI");
    GdiObject valueFont = MakeFont(-28, FW_BOLD, L"Segoe UI");
    GdiObject buttonFont = MakeFont(-18, FW_SEMIBOLD, L"Segoe UI");
    if (!titleFont || !labelFont || !valueFont || !buttonFont) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay settings font creation failed");
        }
        return false;
    }

    const bool russian = settings.language == Language::Russian;
    DrawRoundedRect(
        dc,
        RECT{0, 0, OverlayRenderer::Width, OverlayRenderer::Height},
        18,
        color::Panel,
        color::Panel,
        0);
    {
        DcSelection brushSelection(dc, GetStockObject(NULL_BRUSH));
        GdiObject border(CreatePen(PS_SOLID, 1, color::PanelEdge));
        DcSelection penSelection(dc, border.get());
        RoundRect(
            dc,
            1,
            1,
            OverlayRenderer::Width - 1,
            OverlayRenderer::Height - 1,
            18,
            18);
    }

    SetBkMode(dc, TRANSPARENT);

    const RECT back = SettingsBackButtonRect();
    COLORREF backFill = color::Panel;
    COLORREF backBorder = color::PanelEdge;
    COLORREF backText = color::TextMid;
    int backBorderWidth = 1;
    ApplyHoverStyle(
        hoverHotspot == OverlayHotspot::SettingsBack,
        pressedHotspot == OverlayHotspot::SettingsBack,
        backFill,
        backBorder,
        backText,
        backBorderWidth);
    DrawRoundedRect(dc, back, 8, backFill, backBorder, backBorderWidth);
    GdiObject backPen(CreatePen(PS_SOLID, 2, backText));
    DcSelection backPenSelection(dc, backPen.get());
    MoveToEx(dc, back.left + 21, back.top + 10, nullptr);
    LineTo(dc, back.left + 11, back.top + 17);
    LineTo(dc, back.left + 21, back.top + 24);
    MoveToEx(dc, back.left + 12, back.top + 17, nullptr);
    LineTo(dc, back.right - 10, back.top + 17);

    SelectObject(dc, titleFont.get());
    SetTextColor(dc, color::TextHigh);
    RECT titleRect{ 66, 16, 320, 50 };
    DrawTextW(
        dc,
        russian ? L"Настройки" : L"Settings",
        -1,
        &titleRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, labelFont.get());
    SetTextColor(dc, color::TextMid);
    RECT labelRect{ 32, 78, 250, 110 };
    DrawTextW(
        dc,
        russian ? L"Угол скрытия" : L"Hide angle",
        -1,
        &labelRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, valueFont.get());
    SetTextColor(dc, color::Accent);
    std::wstring value = std::to_wstring(settings.overlay.hideAngleDegrees) + L"°";
    RECT valueRect{ 320, 74, 480, 112 };
    DrawTextW(
        dc,
        value.c_str(),
        -1,
        &valueRect,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const RECT minus = HideAngleMinusRect();
    const RECT plus = HideAnglePlusRect();
    COLORREF minusFill = color::Inset;
    COLORREF minusBorder = color::Border;
    COLORREF minusTextColor = color::TextHigh;
    int minusBorderWidth = 1;
    ApplyHoverStyle(
        hoverHotspot == OverlayHotspot::HideAngleMinus,
        pressedHotspot == OverlayHotspot::HideAngleMinus,
        minusFill,
        minusBorder,
        minusTextColor,
        minusBorderWidth);
    COLORREF plusFill = color::Inset;
    COLORREF plusBorder = color::Border;
    COLORREF plusTextColor = color::TextHigh;
    int plusBorderWidth = 1;
    ApplyHoverStyle(
        hoverHotspot == OverlayHotspot::HideAnglePlus,
        pressedHotspot == OverlayHotspot::HideAnglePlus,
        plusFill,
        plusBorder,
        plusTextColor,
        plusBorderWidth);
    DrawRoundedRect(dc, minus, 10, minusFill, minusBorder, minusBorderWidth);
    DrawRoundedRect(dc, plus, 10, plusFill, plusBorder, plusBorderWidth);
    SelectObject(dc, valueFont.get());
    RECT minusText = minus;
    RECT plusText = plus;
    SetTextColor(dc, minusTextColor);
    DrawTextW(dc, L"−", -1, &minusText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(dc, plusTextColor);
    DrawTextW(dc, L"+", -1, &plusText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DrawSlider(dc, HideAngleSliderRect(), settings.overlay.hideAngleDegrees);
    if (hoverHotspot == OverlayHotspot::HideAngleSlider ||
        pressedHotspot == OverlayHotspot::HideAngleSlider) {
        const RECT track = HideAngleSliderRect();
        DrawRoundedOutline(
            dc,
            RECT{ track.left + 2, track.top + 2, track.right - 2, track.bottom - 2 },
            9,
            color::Accent,
            pressedHotspot == OverlayHotspot::HideAngleSlider ? 2 : 1);
    }

    const RECT reset = HideAngleResetRect();
    COLORREF resetFill = color::Panel;
    COLORREF resetBorder = color::PanelEdge;
    COLORREF resetTextColor = color::TextHigh;
    int resetBorderWidth = 1;
    ApplyHoverStyle(
        hoverHotspot == OverlayHotspot::HideAngleReset,
        pressedHotspot == OverlayHotspot::HideAngleReset,
        resetFill,
        resetBorder,
        resetTextColor,
        resetBorderWidth);
    DrawRoundedRect(dc, reset, 9, resetFill, resetBorder, resetBorderWidth);
    SelectObject(dc, buttonFont.get());
    SetTextColor(dc, resetTextColor);
    RECT resetText = reset;
    DrawTextW(
        dc,
        russian ? L"Сброс" : L"Reset",
        -1,
        &resetText,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    return true;
}

bool DrawPanel(
    HDC dc,
    const StatusSnapshot& status,
    const Settings& settings,
    OverlayPanelPage panelPage,
    OverlayHotspot hoverHotspot,
    OverlayHotspot pressedHotspot,
    Diagnostics* diagnostics)
{
    if (panelPage == OverlayPanelPage::Settings) {
        return DrawSettingsPanel(
            dc,
            settings,
            hoverHotspot,
            pressedHotspot,
            diagnostics);
    }
    return DrawRecordingPanel(
        dc,
        status,
        settings.language,
        hoverHotspot,
        pressedHotspot,
        diagnostics);
}

bool RenderPixels(
    std::vector<std::uint8_t>& output,
    const StatusSnapshot& status,
    const Settings& settings,
    OverlayPanelPage panelPage,
    OverlayHotspot hoverHotspot,
    OverlayHotspot pressedHotspot,
    bool debugChecker,
    Diagnostics* diagnostics)
{
    ScreenDc screen;
    if (!screen) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay screen DC creation failed");
        }
        return false;
    }
    MemoryDc dc(screen.get());
    if (!dc) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay memory DC creation failed");
        }
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = OverlayRenderer::Width;
    info.bmiHeader.biHeight = -OverlayRenderer::Height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    GdiObject bitmap(CreateDIBSection(
        screen.get(),
        &info,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0));
    if (!bitmap || !bits) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay DIB creation failed");
        }
        return false;
    }
    DcSelection bitmapSelection(dc.get(), bitmap.get());
    std::memset(
        bits,
        0,
        static_cast<size_t>(OverlayRenderer::Width) *
            OverlayRenderer::Height * 4);

    if (debugChecker) {
        DrawDebugChecker(dc.get());
    } else if (!DrawPanel(
                   dc.get(),
                   status,
                   settings,
                   panelPage,
                   hoverHotspot,
                   pressedHotspot,
                   diagnostics)) {
        return false;
    }

    const auto* source = static_cast<const std::uint8_t*>(bits);
    output.resize(
        static_cast<size_t>(OverlayRenderer::Width) *
        OverlayRenderer::Height * 4);
    for (size_t pixel = 0;
         pixel < static_cast<size_t>(OverlayRenderer::Width) *
             OverlayRenderer::Height;
         ++pixel) {
        const std::uint8_t blue = source[pixel * 4];
        const std::uint8_t green = source[pixel * 4 + 1];
        const std::uint8_t red = source[pixel * 4 + 2];
        output[pixel * 4] = red;
        output[pixel * 4 + 1] = green;
        output[pixel * 4 + 2] = blue;
        output[pixel * 4 + 3] = (red | green | blue) ? 255 : 0;
    }
    return true;
}

} // namespace

struct OverlayRenderer::Impl {
    struct D3dTexture {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HANDLE sharedHandle = nullptr;
    };

    bool EnsureD3d(Diagnostics* diagnostics)
    {
        if (d3dDisabled) {
            return false;
        }
        if (device && context && textures[0].texture && textures[1].texture) {
            return true;
        }

        ResetD3d();

        static constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        D3D_FEATURE_LEVEL createdLevel{};
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            kFeatureLevels,
            static_cast<UINT>(sizeof(kFeatureLevels) / sizeof(kFeatureLevels[0])),
            D3D11_SDK_VERSION,
            device.GetAddressOf(),
            &createdLevel,
            context.GetAddressOf());
        if (FAILED(hr)) {
            d3dDisabled = true;
            if (diagnostics && !d3dInitFailureLogged) {
                diagnostics->LogWarning(
                    std::wstring(L"D3D11 overlay device creation failed: ") +
                    FormatHResult(hr) + L"; using SetOverlayRaw fallback");
                d3dInitFailureLogged = true;
            }
            return false;
        }

        if (!CreateTextures(true, diagnostics) &&
            !CreateTextures(false, diagnostics)) {
            d3dDisabled = true;
            ResetD3d();
            return false;
        }

        if (diagnostics && !d3dReadyLogged) {
            diagnostics->LogInfo(
                textures[0].sharedHandle
                    ? L"Overlay renderer using D3D11 shared texture path"
                    : L"Overlay renderer using D3D11 texture path");
            d3dReadyLogged = true;
        }
        return true;
    }

    bool CreateTextures(bool shared, Diagnostics* diagnostics)
    {
        for (auto& texture : textures) {
            texture = {};
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = OverlayRenderer::Width;
        desc.Height = OverlayRenderer::Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = shared ? D3D11_RESOURCE_MISC_SHARED : 0;

        for (auto& frame : textures) {
            const HRESULT createResult = device->CreateTexture2D(
                &desc,
                nullptr,
                frame.texture.GetAddressOf());
            if (FAILED(createResult)) {
                if (diagnostics && !d3dTextureFailureLogged) {
                    diagnostics->LogWarning(
                        std::wstring(shared
                            ? L"D3D11 shared overlay texture creation failed: "
                            : L"D3D11 overlay texture creation failed: ") +
                        FormatHResult(createResult));
                    d3dTextureFailureLogged = true;
                }
                return false;
            }

            if (shared) {
                Microsoft::WRL::ComPtr<IDXGIResource> dxgiResource;
                const HRESULT queryResult = frame.texture.As(&dxgiResource);
                if (FAILED(queryResult)) {
                    if (diagnostics && !d3dTextureFailureLogged) {
                        diagnostics->LogWarning(
                            std::wstring(L"D3D11 overlay texture cannot be shared: ") +
                            FormatHResult(queryResult));
                        d3dTextureFailureLogged = true;
                    }
                    return false;
                }

                const HRESULT handleResult = dxgiResource->GetSharedHandle(
                    &frame.sharedHandle);
                if (FAILED(handleResult) || !frame.sharedHandle) {
                    if (diagnostics && !d3dTextureFailureLogged) {
                        diagnostics->LogWarning(
                            std::wstring(L"D3D11 overlay texture shared handle creation failed: ") +
                            FormatHResult(handleResult));
                        d3dTextureFailureLogged = true;
                    }
                    return false;
                }
            }
        }

        d3dUseDirectTexture = !shared;
        textureIndex = 0;
        return true;
    }

    OverlayRenderResult SubmitD3d(
        std::uint64_t overlayHandle,
        Diagnostics* diagnostics)
    {
        if (!EnsureD3d(diagnostics)) {
            return OverlayRenderResult::Failed;
        }

        D3dTexture& frame = textures[textureIndex];
        context->UpdateSubresource(
            frame.texture.Get(),
            0,
            nullptr,
            pixels.data(),
            OverlayRenderer::Width * 4,
            0);
        context->Flush();

        vr::EVROverlayError error = vr::VROverlayError_None;
        if (!d3dUseDirectTexture && frame.sharedHandle) {
            error = SubmitD3dTexture(
                overlayHandle,
                frame.sharedHandle,
                vr::TextureType_DXGISharedHandle);
            if (error != vr::VROverlayError_None) {
                if (error != lastTextureError) {
                    LogOverlayError(diagnostics, L"SetOverlayTexture DXGISharedHandle", error);
                }
                lastTextureError = error;
                d3dUseDirectTexture = true;
            }
        }

        if (d3dUseDirectTexture) {
            error = SubmitD3dTexture(
                overlayHandle,
                frame.texture.Get(),
                vr::TextureType_DirectX);
            if (error != vr::VROverlayError_None) {
                if (error != lastTextureError) {
                    LogOverlayError(diagnostics, L"SetOverlayTexture DirectX", error);
                }
                lastTextureError = error;
                d3dDisabled = true;
                return OverlayRenderResult::Failed;
            }
        }

        lastTextureError = vr::VROverlayError_None;
        textureIndex = (textureIndex + 1) % textures.size();
        return OverlayRenderResult::TextureSubmitted;
    }

    vr::EVROverlayError SubmitD3dTexture(
        std::uint64_t overlayHandle,
        void* handle,
        vr::ETextureType type)
    {
        vr::Texture_t texture{};
        texture.handle = handle;
        texture.eType = type;
        texture.eColorSpace = vr::ColorSpace_Gamma;
        return vr::VROverlay()->SetOverlayTexture(overlayHandle, &texture);
    }

    OverlayRenderResult SubmitRaw(
        std::uint64_t overlayHandle,
        Diagnostics* diagnostics)
    {
        const auto error = vr::VROverlay()->SetOverlayRaw(
            overlayHandle,
            pixels.data(),
            OverlayRenderer::Width,
            OverlayRenderer::Height,
            4);
        if (error != vr::VROverlayError_None) {
            if (error != lastRawError) {
                LogOverlayError(diagnostics, L"SetOverlayRaw", error);
            }
            lastRawError = error;
            return OverlayRenderResult::Failed;
        }
        lastRawError = vr::VROverlayError_None;
        return OverlayRenderResult::RawSubmitted;
    }

    void ResetD3d()
    {
        for (auto& texture : textures) {
            texture = {};
        }
        context.Reset();
        device.Reset();
        textureIndex = 0;
        d3dUseDirectTexture = false;
        lastTextureError = vr::VROverlayError_None;
    }

    std::vector<std::uint8_t> pixels;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    std::array<D3dTexture, 2> textures;
    size_t textureIndex = 0;
    bool d3dDisabled = false;
    bool d3dUseDirectTexture = false;
    bool d3dInitFailureLogged = false;
    bool d3dTextureFailureLogged = false;
    bool d3dReadyLogged = false;
    vr::EVROverlayError lastTextureError = vr::VROverlayError_None;
    vr::EVROverlayError lastRawError = vr::VROverlayError_None;
};

OverlayRenderer::OverlayRenderer()
    : impl_(std::make_unique<Impl>())
{
}

OverlayRenderer::~OverlayRenderer() = default;

OverlayRenderResult OverlayRenderer::Render(
    std::uint64_t overlayHandle,
    const StatusSnapshot& status,
    const Settings& settings,
    OverlayPanelPage panelPage,
    OverlayHotspot hoverHotspot,
    OverlayHotspot pressedHotspot,
    bool debugChecker,
    Diagnostics* diagnostics)
{
    if (!vr::VROverlay() ||
        overlayHandle == vr::k_ulOverlayHandleInvalid ||
        overlayHandle == 0) {
        return OverlayRenderResult::Failed;
    }
    if (!RenderPixels(
            impl_->pixels,
            status,
            settings,
            panelPage,
            hoverHotspot,
            pressedHotspot,
            debugChecker,
            diagnostics)) {
        return OverlayRenderResult::Failed;
    }

    const OverlayRenderResult textureResult = impl_->SubmitD3d(
        overlayHandle,
        diagnostics);
    if (textureResult == OverlayRenderResult::TextureSubmitted) {
        return textureResult;
    }

    return impl_->SubmitRaw(
        overlayHandle,
        diagnostics);
}

void OverlayRenderer::Reset()
{
    impl_->pixels.clear();
    impl_->ResetD3d();
    impl_->d3dDisabled = false;
    impl_->d3dUseDirectTexture = false;
    impl_->d3dInitFailureLogged = false;
    impl_->d3dTextureFailureLogged = false;
    impl_->d3dReadyLogged = false;
    impl_->lastTextureError = vr::VROverlayError_None;
    impl_->lastRawError = vr::VROverlayError_None;
}
